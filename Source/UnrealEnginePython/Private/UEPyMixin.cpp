#include "UEPyMixin.h"

#include "PythonFunction.h"
#include "PythonMixin.h"
#include "UEPyModule.h"
#include "Async/Async.h"
#include "CoreGlobals.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace
{
	int32 GMixinCallbackDepth = 0;
	int32 GMixinMutationDepth = 0;
}

FUEPyScopedMixinCallback::FUEPyScopedMixinCallback()
{
	++GMixinCallbackDepth;
}

FUEPyScopedMixinCallback::~FUEPyScopedMixinCallback()
{
	check(GMixinCallbackDepth > 0);
	--GMixinCallbackDepth;
}

bool FUEPyScopedMixinCallback::IsActive()
{
	return GMixinCallbackDepth > 0;
}

namespace
{
	const FName DefaultMixinProfileName(TEXT("Default"));
	const FName MixinSetFunctionName(TEXT("GetPythonMixinSet"));
	const FName MixinProfileFunctionName(TEXT("GetPythonMixinProfile"));
	constexpr const char* MixinTeardownName = "__uep_mixin_teardown__";

	struct FMixinFunctionBinding
	{
		FName PublicName = NAME_None;
		FName StoredOriginalName = NAME_None;
		UFunction* OriginalFunction = nullptr;
		UPythonFunction* InjectedFunction = nullptr;
		bool bOriginalOwnedByTarget = false;
	};

	struct FMixinProfileBinding
	{
		uint64 GenerationId = 0;
		FName ProfileName = NAME_None;
		PyObject* PythonClass = nullptr;
		TMap<FName, PyObject*> Callables;
		TSet<TWeakObjectPtr<UObject>> InitializedObjects;
	};

	struct FMixinInstanceState
	{
		FName ExplicitProfile = NAME_None;
		FName ActiveProfile = NAME_None;
		uint64 ActiveGenerationId = 0;
		bool bResolving = false;
		bool bInitializing = false;
		bool bTearingDown = false;
	};

	struct FMixinBinding
	{
		uint64 RegistrationId = 0;
		UClass* TargetClass = nullptr;
		FName DefaultProfile = NAME_None;
		TMap<FName, TUniquePtr<FMixinProfileBinding>> Profiles;
		TArray<FMixinFunctionBinding> Functions;
		TMap<TWeakObjectPtr<UObject>, FMixinInstanceState> InstanceStates;
		TWeakObjectPtr<UUEPPythonMixinSet> DeclaredSet;
		uint32 DispatchesUntilCleanup = 256;
		bool bMutating = false;
		bool bUnregistering = false;
	};

	struct FMixinCandidate
	{
		FName PublicName = NAME_None;
		UFunction* OriginalFunction = nullptr;
		PyObject* PythonCallable = nullptr; // Owned until transferred to a profile.
	};

	struct FLoadedProfile
	{
		FName ProfileName = NAME_None;
		PyObject* PythonClass = nullptr;
	};

	TMap<UClass*, TUniquePtr<FMixinBinding>> GMixinsByClass;
	TMap<uint64, FMixinBinding*> GMixinsById;
	TSet<TWeakObjectPtr<UClass>> GDeclaringClasses;
	uint64 GNextMixinRegistrationId = 1;
	uint64 GNextMixinProfileGenerationId = 1;
	FDelegateHandle GMixinPackageLoadedHandle;

	class FScopedMixinMutation
	{
	public:
		FScopedMixinMutation()
		{
			++GMixinMutationDepth;
		}

		~FScopedMixinMutation()
		{
			check(GMixinMutationDepth > 0);
			--GMixinMutationDepth;
		}
	};

	bool EnsureMixinRegistryMutationAllowed(const char* api_name)
	{
		if (IsRunningCookCommandlet())
		{
			PyErr_Format(
				PyExc_RuntimeError,
				"%s cannot modify the mixin registry while Unreal is cooking assets",
				api_name);
			return false;
		}
		if (FUEPyScopedMixinCallback::IsActive())
		{
			PyErr_Format(
				PyExc_RuntimeError,
				"%s cannot modify the mixin registry while Python or Blueprint mixin logic is executing",
				api_name);
			return false;
		}
		if (GMixinMutationDepth > 0)
		{
			PyErr_Format(
				PyExc_RuntimeError,
				"%s cannot re-enter an in-progress mixin registry mutation",
				api_name);
			return false;
		}
		return true;
	}

	void ClearFunctionCaches(UClass* target_class)
	{
		if (!target_class)
		{
			return;
		}

		for (TObjectIterator<UClass> it; it; ++it)
		{
			UClass* loaded_class = *it;
			if (loaded_class == target_class || loaded_class->IsChildOf(target_class))
			{
				loaded_class->ClearFunctionMapsCaches();
			}
		}
	}

	bool UnlinkChildFunction(UClass* target_class, UFunction* function)
	{
		if (!target_class || !function)
		{
			return false;
		}

		UField* previous = nullptr;
		for (UField* child = target_class->Children; child; child = child->Next)
		{
			if (child == function)
			{
				if (previous)
				{
					previous->Next = child->Next;
				}
				else
				{
					target_class->Children = child->Next;
				}
				child->Next = nullptr;
				return true;
			}
			previous = child;
		}
		return false;
	}

	FMixinBinding* FindBindingForClass(UClass* object_class)
	{
		for (UClass* current = object_class; current; current = current->GetSuperClass())
		{
			if (TUniquePtr<FMixinBinding>* found = GMixinsByClass.Find(current))
			{
				return found->Get();
			}
		}
		return nullptr;
	}

	FMixinBinding* FindBindingById(uint64 registration_id)
	{
		FMixinBinding** found = GMixinsById.Find(registration_id);
		return found ? *found : nullptr;
	}

	FMixinFunctionBinding* FindFunctionBinding(FMixinBinding* binding, FName function_name)
	{
		if (!binding)
		{
			return nullptr;
		}
		return binding->Functions.FindByPredicate(
			[function_name](const FMixinFunctionBinding& item)
			{
				return item.PublicName == function_name;
			});
	}

	FMixinProfileBinding* FindProfile(FMixinBinding* binding, FName profile_name)
	{
		if (!binding)
		{
			return nullptr;
		}
		TUniquePtr<FMixinProfileBinding>* found = binding->Profiles.Find(profile_name);
		return found ? found->Get() : nullptr;
	}

	PyObject* GetLocalClassAttribute(PyObject* python_class, const char* attribute_name)
	{
		PyObject* class_dict = PyObject_GetAttrString(python_class, "__dict__");
		if (!class_dict)
		{
			return nullptr;
		}

		PyObject* key = PyUnicode_FromString(attribute_name);
		PyObject* value = key ? PyObject_GetItem(class_dict, key) : nullptr;
		Py_XDECREF(key);
		Py_DECREF(class_dict);
		if (!value && PyErr_ExceptionMatches(PyExc_KeyError))
		{
			PyErr_Clear();
		}
		return value;
	}

	void RemoveInvalidInstanceStates(FMixinBinding& binding)
	{
		for (auto it = binding.InstanceStates.CreateIterator(); it; ++it)
		{
			if (!it.Key().IsValid())
			{
				it.RemoveCurrent();
			}
		}
		for (TPair<FName, TUniquePtr<FMixinProfileBinding>>& pair : binding.Profiles)
		{
			for (auto it = pair.Value->InitializedObjects.CreateIterator(); it; ++it)
			{
				if (!it->IsValid())
				{
					it.RemoveCurrent();
				}
			}
		}
	}

	void ClearActiveState(FMixinInstanceState& state)
	{
		state.ActiveProfile = NAME_None;
		state.ActiveGenerationId = 0;
	}

	bool IsInstanceStateBusy(const FMixinInstanceState& state)
	{
		return state.bResolving || state.bInitializing || state.bTearingDown;
	}

	void SetBusyStateError(const TCHAR* operation, UObject* context)
	{
		PyErr_Format(
			PyExc_RuntimeError,
			"cannot %s Python mixin state for %s while that instance is resolving, initializing, or tearing down",
			TCHAR_TO_UTF8(operation),
			context ? TCHAR_TO_UTF8(*context->GetPathName()) : "<invalid>");
	}

	bool RunTeardownHook(PyObject* python_class, UObject* context)
	{
		if (!python_class || !context)
		{
			return true;
		}

		ue_PyUObject* py_object = FUnrealEnginePythonHouseKeeper::Get()->GetPyUObject(context);
		if (!py_object || !py_object->py_dict)
		{
			return true;
		}

		FUEPyScopedMixinCallback callback_scope;
		PyObject* teardown = GetLocalClassAttribute(python_class, MixinTeardownName);
		if (!teardown)
		{
			return !PyErr_Occurred();
		}

		PyObject* result = PyObject_CallFunctionObjArgs(
			teardown,
			reinterpret_cast<PyObject*>(py_object),
			nullptr);
		Py_DECREF(teardown);
		if (!result)
		{
			return false;
		}
		Py_DECREF(result);
		return true;
	}

	bool TeardownActiveProfile(uint64 registration_id, UObject* context)
	{
		FMixinBinding* binding = FindBindingById(registration_id);
		if (!binding || !context)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin binding disappeared while tearing down an instance");
			return false;
		}

		const TWeakObjectPtr<UObject> object_key(context);
		FMixinInstanceState* state = binding->InstanceStates.Find(object_key);
		if (!state || state->ActiveGenerationId == 0)
		{
			if (state)
			{
				ClearActiveState(*state);
			}
			return true;
		}
		if (IsInstanceStateBusy(*state))
		{
			SetBusyStateError(TEXT("tear down"), context);
			return false;
		}

		const FName active_name = state->ActiveProfile;
		const uint64 active_generation = state->ActiveGenerationId;
		FMixinProfileBinding* active = FindProfile(binding, active_name);
		const bool owns_state = active && active->GenerationId == active_generation;
		PyObject* python_class = owns_state ? active->PythonClass : nullptr;
		Py_XINCREF(python_class);
		if (owns_state)
		{
			active->InitializedObjects.Remove(object_key);
		}
		ClearActiveState(*state);
		state->bTearingDown = true;

		bool succeeded = true;
		if (python_class)
		{
			succeeded = RunTeardownHook(python_class, context);
		}
		Py_XDECREF(python_class);

		binding = FindBindingById(registration_id);
		state = binding ? binding->InstanceStates.Find(object_key) : nullptr;
		if (state)
		{
			state->bTearingDown = false;
		}
		else if (succeeded)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin instance state disappeared during teardown");
			succeeded = false;
		}
		return succeeded;
	}

	void TeardownProfile(uint64 registration_id, FName profile_name, uint64 generation_id)
	{
		FMixinBinding* binding = FindBindingById(registration_id);
		FMixinProfileBinding* profile = FindProfile(binding, profile_name);
		if (!profile || profile->GenerationId != generation_id)
		{
			return;
		}
		TArray<TWeakObjectPtr<UObject>> initialized = profile->InitializedObjects.Array();
		for (const TWeakObjectPtr<UObject>& weak_object : initialized)
		{
			UObject* object = weak_object.Get();
			if (!object)
			{
				continue;
			}
			binding = FindBindingById(registration_id);
			FMixinInstanceState* state = binding ? binding->InstanceStates.Find(weak_object) : nullptr;
			if (!state || state->ActiveGenerationId != generation_id)
			{
				continue;
			}
			if (!TeardownActiveProfile(registration_id, object) && PyErr_Occurred())
			{
				unreal_engine_py_log_error();
			}
		}
		binding = FindBindingById(registration_id);
		profile = FindProfile(binding, profile_name);
		if (profile && profile->GenerationId == generation_id)
		{
			profile->InitializedObjects.Empty();
		}
	}

	void ReleaseProfile(FMixinProfileBinding& profile)
	{
		for (TPair<FName, PyObject*>& pair : profile.Callables)
		{
			Py_XDECREF(pair.Value);
			pair.Value = nullptr;
		}
		profile.Callables.Empty();
		Py_CLEAR(profile.PythonClass);
	}

	bool ResolveRequestedProfile(uint64 registration_id, UObject* context, FName& out_profile_name)
	{
		out_profile_name = NAME_None;
		FMixinBinding* binding = FindBindingById(registration_id);
		if (!binding || !context || binding->bMutating || binding->bUnregistering)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin router is unavailable while its registration is changing");
			return false;
		}

		const TWeakObjectPtr<UObject> object_key(context);
		FMixinInstanceState* state = &binding->InstanceStates.FindOrAdd(object_key);
		if (IsInstanceStateBusy(*state))
		{
			SetBusyStateError(TEXT("resolve"), context);
			return false;
		}
		if (!state->ExplicitProfile.IsNone())
		{
			out_profile_name = state->ExplicitProfile;
			return true;
		}
		// The interface is an instance-selection hook, not a per-call policy VM.
		// Cache its answer after the first dispatch; set_mixin_profile switches it
		// explicitly and clear_mixin_profile invalidates it for re-resolution.
		if (state->ActiveGenerationId != 0 && !state->ActiveProfile.IsNone())
		{
			FMixinProfileBinding* active = FindProfile(binding, state->ActiveProfile);
			if (active && active->GenerationId == state->ActiveGenerationId)
			{
				out_profile_name = state->ActiveProfile;
				return true;
			}
		}

		FName selected = NAME_None;
		if (context && context->GetClass()->ImplementsInterface(UUEPPythonMixinInterface::StaticClass()))
		{
			state->bResolving = true;
			{
				FUEPyScopedMixinCallback callback_scope;
				selected = IUEPPythonMixinInterface::Execute_GetPythonMixinProfile(context);
			}

			binding = FindBindingById(registration_id);
			state = binding ? binding->InstanceStates.Find(object_key) : nullptr;
			if (!binding || !state)
			{
				PyErr_SetString(PyExc_RuntimeError, "mixin instance state disappeared while resolving its profile");
				return false;
			}
			state->bResolving = false;
		}
		if (selected.IsNone())
		{
			selected = binding->DefaultProfile;
		}
		if (selected.IsNone() && binding->Profiles.Num() == 1)
		{
			for (const TPair<FName, TUniquePtr<FMixinProfileBinding>>& pair : binding->Profiles)
			{
				selected = pair.Key;
				break;
			}
		}
		out_profile_name = selected;
		return true;
	}

	FMixinProfileBinding* ResolveAndInitializeProfile(FMixinBinding* binding, UObject* context)
	{
		if (!binding || !context || !context->IsA(binding->TargetClass) || binding->bMutating || binding->bUnregistering)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin dispatch target or router is not currently available");
			return nullptr;
		}
		const uint64 registration_id = binding->RegistrationId;

		if (binding->DispatchesUntilCleanup == 0)
		{
			RemoveInvalidInstanceStates(*binding);
			binding->DispatchesUntilCleanup = 256;
		}
		else
		{
			--binding->DispatchesUntilCleanup;
		}
		const TWeakObjectPtr<UObject> object_key(context);
		FName requested_name = NAME_None;
		if (!ResolveRequestedProfile(registration_id, context, requested_name))
		{
			return nullptr;
		}
		if (requested_name.IsNone())
		{
			PyErr_Format(
				PyExc_RuntimeError,
				"no Python mixin profile is selected for %s",
				TCHAR_TO_UTF8(*context->GetPathName()));
			return nullptr;
		}

		binding = FindBindingById(registration_id);
		FMixinInstanceState* state = binding ? binding->InstanceStates.Find(object_key) : nullptr;
		FMixinProfileBinding* profile = FindProfile(binding, requested_name);
		if (!binding || !state || !profile)
		{
			const FString target_path = binding && binding->TargetClass
				? binding->TargetClass->GetPathName()
				: TEXT("<unavailable>");
			PyErr_Format(
				PyExc_KeyError,
				"Python mixin profile '%s' is not registered on %s",
				TCHAR_TO_UTF8(*requested_name.ToString()),
				TCHAR_TO_UTF8(*target_path));
			return nullptr;
		}

		if (state->ActiveGenerationId == profile->GenerationId)
		{
			return profile;
		}

		if (state->ActiveGenerationId != 0 && !TeardownActiveProfile(registration_id, context))
		{
			return nullptr;
		}

		ue_PyUObject* py_context = ue_get_python_uobject(context);
		if (!py_context || !py_context->py_dict)
		{
			PyErr_SetString(PyExc_RuntimeError, "unable to create the UObject Python wrapper for mixin dispatch");
			return nullptr;
		}

		binding = FindBindingById(registration_id);
		state = binding ? &binding->InstanceStates.FindOrAdd(object_key) : nullptr;
		profile = FindProfile(binding, requested_name);
		if (!binding || !state || !profile || binding->bMutating || binding->bUnregistering)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin profile disappeared before initialization");
			return nullptr;
		}
		const uint64 generation_id = profile->GenerationId;
		PyObject* python_class = profile->PythonClass;
		Py_INCREF(python_class);
		state->ActiveProfile = requested_name;
		state->ActiveGenerationId = generation_id;
		state->bInitializing = true;

		bool initialized = true;
		{
			FUEPyScopedMixinCallback callback_scope;
			PyObject* initializer = GetLocalClassAttribute(python_class, "__init__");
			if (initializer)
			{
				PyObject* result = PyObject_CallFunctionObjArgs(
					initializer,
					reinterpret_cast<PyObject*>(py_context),
					nullptr);
				Py_DECREF(initializer);
				if (!result)
				{
					initialized = false;
				}
				Py_XDECREF(result);
			}
			else if (PyErr_Occurred())
			{
				initialized = false;
			}
		}
		Py_DECREF(python_class);

		binding = FindBindingById(registration_id);
		state = binding ? binding->InstanceStates.Find(object_key) : nullptr;
		profile = FindProfile(binding, requested_name);
		if (!binding || !state || !profile || profile->GenerationId != generation_id)
		{
			if (!PyErr_Occurred())
			{
				PyErr_SetString(PyExc_RuntimeError, "mixin registration changed during profile initialization");
			}
			return nullptr;
		}
		state->bInitializing = false;
		if (!initialized)
		{
			ClearActiveState(*state);
			profile->InitializedObjects.Remove(object_key);
			return nullptr;
		}
		profile->InitializedObjects.Add(object_key);
		return profile;
	}

	void RestoreFunction(FMixinFunctionBinding& function_binding, UClass* target_class, uint64 registration_id)
	{
		UPythonFunction* injected = function_binding.InjectedFunction;
		if (injected)
		{
			target_class->RemoveFunctionFromFunctionMap(injected);
			UnlinkChildFunction(target_class, injected);
			injected->ClearMixinRegistration();
			injected->SetPyCallable(nullptr);

			const FName discarded_name = MakeUniqueObjectName(
				target_class,
				UPythonFunction::StaticClass(),
				FName(*FString::Printf(TEXT("__UEP_Mixin_Discarded_%llu_%s"), registration_id, *function_binding.PublicName.ToString())));
			injected->Rename(*discarded_name.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
			if (injected->IsRooted())
			{
				injected->RemoveFromRoot();
			}
			injected->MarkAsGarbage();
		}

		if (function_binding.bOriginalOwnedByTarget && function_binding.OriginalFunction)
		{
			function_binding.OriginalFunction->Rename(
				*function_binding.PublicName.ToString(),
				target_class,
				REN_DontCreateRedirectors | REN_NonTransactional);
			target_class->AddFunctionToFunctionMap(function_binding.OriginalFunction, function_binding.PublicName);
		}
	}

	bool UnregisterBinding(UClass* target_class)
	{
		TUniquePtr<FMixinBinding>* found = GMixinsByClass.Find(target_class);
		if (!found)
		{
			return false;
		}

		FMixinBinding* binding = found->Get();
		if (binding->bUnregistering)
		{
			return false;
		}
		binding->bUnregistering = true;
		binding->bMutating = true;
		const uint64 registration_id = binding->RegistrationId;
		TArray<TPair<FName, uint64>> profiles_to_teardown;
		profiles_to_teardown.Reserve(binding->Profiles.Num());
		for (const TPair<FName, TUniquePtr<FMixinProfileBinding>>& pair : binding->Profiles)
		{
			profiles_to_teardown.Add(TPair<FName, uint64>(pair.Key, pair.Value->GenerationId));
		}
		for (const TPair<FName, uint64>& profile : profiles_to_teardown)
		{
			TeardownProfile(registration_id, profile.Key, profile.Value);
		}

		binding = FindBindingById(registration_id);
		if (!binding)
		{
			return false;
		}
		for (int32 index = binding->Functions.Num() - 1; index >= 0; --index)
		{
			RestoreFunction(binding->Functions[index], target_class, registration_id);
		}
		ClearFunctionCaches(target_class);

		GMixinsById.Remove(registration_id);
		for (TPair<FName, TUniquePtr<FMixinProfileBinding>>& pair : binding->Profiles)
		{
			ReleaseProfile(*pair.Value);
		}
		binding->Profiles.Empty();
		binding->InstanceStates.Empty();
		GMixinsByClass.Remove(target_class);
		UE_LOG(LogPython, Log, TEXT("Unregistered Python mixin router from %s"), *target_class->GetPathName());
		return true;
	}

	void PruneSupersededBindings(UClass* incoming_class)
	{
		TArray<UClass*> stale_classes;
		for (const TPair<UClass*, TUniquePtr<FMixinBinding>>& pair : GMixinsByClass)
		{
			UClass* registered_class = pair.Key;
			if (!registered_class || registered_class == incoming_class)
			{
				continue;
			}
			const bool newer_version_exists = registered_class->HasAnyClassFlags(CLASS_NewerVersionExists);
			bool same_blueprint = false;
#if WITH_EDITORONLY_DATA
			same_blueprint = incoming_class && incoming_class->ClassGeneratedBy &&
				registered_class->ClassGeneratedBy == incoming_class->ClassGeneratedBy;
#endif
			if (newer_version_exists || same_blueprint)
			{
				stale_classes.Add(registered_class);
			}
		}
		for (UClass* stale_class : stale_classes)
		{
			UnregisterBinding(stale_class);
		}
	}

	bool ValidateTargetClass(UClass* target_class)
	{
		if (!target_class)
		{
			PyErr_SetString(PyExc_TypeError, "mixin target must be a UClass");
			return false;
		}
		if (!target_class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			PyErr_SetString(PyExc_TypeError, "0.7 mixins are limited to Blueprint-generated classes");
			return false;
		}

		PruneSupersededBindings(target_class);
		for (const TPair<UClass*, TUniquePtr<FMixinBinding>>& pair : GMixinsByClass)
		{
			UClass* registered_class = pair.Key;
			if (registered_class != target_class &&
				(target_class->IsChildOf(registered_class) || registered_class->IsChildOf(target_class)))
			{
				PyErr_Format(
					PyExc_TypeError,
					"0.7 does not allow simultaneous mixin routers on related Blueprint classes (%s and %s)",
					TCHAR_TO_UTF8(*target_class->GetName()),
					TCHAR_TO_UTF8(*registered_class->GetName()));
				return false;
			}
		}
		return true;
	}

	bool ValidateOriginalFunction(UFunction* original, FString& error)
	{
		if (!original)
		{
			error = TEXT("target function does not exist");
			return false;
		}
		if (original->HasAnyFunctionFlags(FUNC_Static | FUNC_Net | FUNC_Delegate | FUNC_MulticastDelegate))
		{
			error = TEXT("static, RPC, and delegate functions are outside the 0.7 mixin contract");
			return false;
		}
#if WITH_METADATA
		if (original->HasMetaData(TEXT("Latent")))
		{
			error = TEXT("latent functions are outside the 0.7 mixin contract");
			return false;
		}
#endif
		for (TFieldIterator<FProperty> it(original); it && it->HasAnyPropertyFlags(CPF_Parm); ++it)
		{
			const FProperty* property = *it;
			if (property->HasAnyPropertyFlags(CPF_OutParm) &&
				!property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_ConstParm))
			{
				error = FString::Printf(
					TEXT("non-const output parameter '%s' is outside the 0.7 mixin contract"),
					*property->GetName());
				return false;
			}
		}
		return true;
	}

	bool ValidatePythonCallableSignature(PyObject* python_callable, UFunction* original, const FString& function_name)
	{
		Py_ssize_t positional_count = 1;
		for (TFieldIterator<FProperty> it(original); it && it->HasAnyPropertyFlags(CPF_Parm); ++it)
		{
			if (!it->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				++positional_count;
			}
		}

		PyObject* inspect = PyImport_ImportModule("inspect");
		if (!inspect)
		{
			return false;
		}
		PyObject* signature = PyObject_CallMethod(inspect, "signature", "O", python_callable);
		Py_DECREF(inspect);
		if (!signature)
		{
			return false;
		}
		PyObject* bind = PyObject_GetAttrString(signature, "bind");
		Py_DECREF(signature);
		if (!bind)
		{
			return false;
		}

		PyObject* probe_args = PyTuple_New(positional_count);
		if (!probe_args)
		{
			Py_DECREF(bind);
			return false;
		}
		for (Py_ssize_t index = 0; index < positional_count; ++index)
		{
			Py_INCREF(Py_None);
			PyTuple_SET_ITEM(probe_args, index, Py_None);
		}

		PyObject* bound_arguments = PyObject_CallObject(bind, probe_args);
		Py_DECREF(probe_args);
		Py_DECREF(bind);
		if (bound_arguments)
		{
			Py_DECREF(bound_arguments);
			return true;
		}

		if (PyErr_ExceptionMatches(PyExc_TypeError))
		{
			PyObject* exception_type = nullptr;
			PyObject* exception_value = nullptr;
			PyObject* exception_traceback = nullptr;
			PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
			PyObject* detail_value = exception_value ? PyObject_Str(exception_value) : nullptr;
			FString detail = TEXT("incompatible Python signature");
			if (detail_value && PyUnicodeOrString_Check(detail_value))
			{
				const char* detail_utf8 = UEPyUnicode_AsUTF8(detail_value);
				if (detail_utf8)
				{
					detail = UTF8_TO_TCHAR(detail_utf8);
				}
			}
			Py_XDECREF(detail_value);
			Py_XDECREF(exception_type);
			Py_XDECREF(exception_value);
			Py_XDECREF(exception_traceback);
			PyErr_Clear();
			PyErr_Format(
				PyExc_TypeError,
				"mixin method for %s must accept %zd positional arguments (self plus reflected inputs): %s",
				TCHAR_TO_UTF8(*function_name),
				positional_count,
				TCHAR_TO_UTF8(*detail));
		}
		return false;
	}

	UFunction* FindOriginalFunction(FMixinBinding* binding, UClass* target_class, FName function_name)
	{
		if (FMixinFunctionBinding* existing = FindFunctionBinding(binding, function_name))
		{
			return existing->OriginalFunction;
		}
		return target_class->FindFunctionByName(function_name);
	}

	bool CollectCandidates(UClass* target_class, FMixinBinding* binding, PyObject* python_class, TArray<FMixinCandidate>& candidates)
	{
		PyObject* class_dict = PyObject_GetAttrString(python_class, "__dict__");
		if (!class_dict)
		{
			return false;
		}
		PyObject* items = PyMapping_Items(class_dict);
		Py_DECREF(class_dict);
		if (!items)
		{
			return false;
		}

		TSet<FName> seen_names;
		const Py_ssize_t item_count = PyList_Size(items);
		for (Py_ssize_t index = 0; index < item_count; ++index)
		{
			PyObject* item = PyList_GetItem(items, index);
			PyObject* key = item ? PyTuple_GetItem(item, 0) : nullptr;
			PyObject* value = item ? PyTuple_GetItem(item, 1) : nullptr;
			if (!key || !value || !PyUnicodeOrString_Check(key) || !PyCallable_Check(value))
			{
				continue;
			}

			const char* python_name = UEPyUnicode_AsUTF8(key);
			if (!python_name || !python_name[0] || (python_name[0] == '_' && python_name[1] == '_'))
			{
				continue;
			}

			FString effective_name = UTF8_TO_TCHAR(python_name);
			bool explicit_override = false;
			PyObject* override_name = PyObject_GetAttrString(value, "override");
			if (override_name)
			{
				if (!PyUnicodeOrString_Check(override_name))
				{
					Py_DECREF(override_name);
					Py_DECREF(items);
					PyErr_Format(PyExc_TypeError, "%s.override must be a string", python_name);
					return false;
				}
				const char* override_utf8 = UEPyUnicode_AsUTF8(override_name);
				if (!override_utf8)
				{
					Py_DECREF(override_name);
					Py_DECREF(items);
					return false;
				}
				effective_name = UTF8_TO_TCHAR(override_utf8);
				explicit_override = true;
				Py_DECREF(override_name);
			}
			else
			{
				PyErr_Clear();
			}

			const FName function_name(*effective_name);
			if (function_name == MixinSetFunctionName || function_name == MixinProfileFunctionName)
			{
				if (explicit_override)
				{
					Py_DECREF(items);
					PyErr_Format(PyExc_TypeError, "%s is reserved for UEP mixin routing", TCHAR_TO_UTF8(*effective_name));
					return false;
				}
				continue;
			}

			UFunction* original = FindOriginalFunction(binding, target_class, function_name);
			if (!original)
			{
				if (explicit_override)
				{
					Py_DECREF(items);
					PyErr_Format(PyExc_AttributeError, "target class has no UFunction named %s", TCHAR_TO_UTF8(*effective_name));
					return false;
				}
				continue;
			}
			if (seen_names.Contains(function_name))
			{
				Py_DECREF(items);
				PyErr_Format(PyExc_ValueError, "mixin defines UFunction %s more than once", TCHAR_TO_UTF8(*effective_name));
				return false;
			}

			FString validation_error;
			if (!ValidateOriginalFunction(original, validation_error))
			{
				Py_DECREF(items);
				PyErr_Format(
					PyExc_TypeError,
					"cannot mix in %s.%s: %s",
					TCHAR_TO_UTF8(*target_class->GetName()),
					TCHAR_TO_UTF8(*effective_name),
					TCHAR_TO_UTF8(*validation_error));
				return false;
			}
			if (!ValidatePythonCallableSignature(value, original, effective_name))
			{
				Py_DECREF(items);
				return false;
			}

			FMixinCandidate& candidate = candidates.AddDefaulted_GetRef();
			candidate.PublicName = function_name;
			candidate.OriginalFunction = original;
			candidate.PythonCallable = value;
			Py_INCREF(value);
			seen_names.Add(function_name);
		}
		Py_DECREF(items);

		if (candidates.IsEmpty())
		{
			PyErr_SetString(PyExc_ValueError, "mixin class does not define a callable matching any target UFunction");
			return false;
		}
		return true;
	}

	void ReleaseCandidates(TArray<FMixinCandidate>& candidates)
	{
		for (FMixinCandidate& candidate : candidates)
		{
			Py_XDECREF(candidate.PythonCallable);
			candidate.PythonCallable = nullptr;
		}
	}

	UPythonFunction* CreateInjectedFunction(
		FMixinBinding& binding,
		FName public_name,
		UFunction* original,
		FMixinFunctionBinding& out_binding,
		FString& error)
	{
		UClass* target_class = binding.TargetClass;
		out_binding.PublicName = public_name;
		out_binding.OriginalFunction = original;
		out_binding.bOriginalOwnedByTarget = original->GetOuter() == target_class;

		if (out_binding.bOriginalOwnedByTarget)
		{
			target_class->RemoveFunctionFromFunctionMap(original);
			out_binding.StoredOriginalName = MakeUniqueObjectName(
				target_class,
				original->GetClass(),
				FName(*FString::Printf(TEXT("__UEP_Mixin_Original_%llu_%s"), binding.RegistrationId, *public_name.ToString())));
			if (!original->Rename(
				*out_binding.StoredOriginalName.ToString(),
				target_class,
				REN_DontCreateRedirectors | REN_NonTransactional))
			{
				target_class->AddFunctionToFunctionMap(original, public_name);
				error = FString::Printf(TEXT("unable to preserve original function %s"), *public_name.ToString());
				return nullptr;
			}
		}

		UPythonFunction* injected = NewObject<UPythonFunction>(
			target_class,
			public_name,
			RF_Public | RF_Transient | RF_MarkAsNative);
		if (!injected)
		{
			error = FString::Printf(TEXT("unable to allocate injected function %s"), *public_name.ToString());
			if (out_binding.bOriginalOwnedByTarget)
			{
				original->Rename(*public_name.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
				target_class->AddFunctionToFunctionMap(original, public_name);
			}
			return nullptr;
		}

		injected->FunctionFlags = static_cast<EFunctionFlags>(original->FunctionFlags | FUNC_Native);
		injected->ReturnValueOffset = MAX_uint16;
		injected->FirstPropertyToInit = nullptr;
		injected->Script.Add(EX_EndFunctionParms);

		FField::FLinkedListBuilder property_builder(&injected->ChildProperties);
		for (TFieldIterator<FProperty> it(original); it && it->HasAnyPropertyFlags(CPF_Parm); ++it)
		{
			FProperty* cloned = CastField<FProperty>(FField::Duplicate(*it, injected, it->GetFName()));
			if (!cloned)
			{
				error = FString::Printf(TEXT("unable to clone parameter %s.%s"), *public_name.ToString(), *it->GetName());
				const FName discarded_name = MakeUniqueObjectName(
					target_class,
					UPythonFunction::StaticClass(),
					FName(*FString::Printf(TEXT("__UEP_Mixin_Failed_%llu_%s"), binding.RegistrationId, *public_name.ToString())));
				injected->Rename(*discarded_name.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
				injected->MarkAsGarbage();
				if (out_binding.bOriginalOwnedByTarget)
				{
					original->Rename(*public_name.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
					target_class->AddFunctionToFunctionMap(original, public_name);
				}
				return nullptr;
			}
			cloned->Next = nullptr;
			property_builder.AppendNoTerminate(*cloned);
		}

		injected->StaticLink(true);
		injected->SetNativeFunc((FNativeFuncPtr)&UPythonFunction::CallPythonCallable);
		injected->SetPyCallable(nullptr);
		injected->SetMixinRegistration(binding.RegistrationId, public_name);
		injected->AddToRoot();
		injected->Next = target_class->Children;
		target_class->Children = injected;
		target_class->AddFunctionToFunctionMap(injected, public_name);
		out_binding.InjectedFunction = injected;
		return injected;
	}

	void PruneUnusedFunctions(FMixinBinding& binding)
	{
		for (int32 index = binding.Functions.Num() - 1; index >= 0; --index)
		{
			const FName function_name = binding.Functions[index].PublicName;
			bool used = false;
			for (const TPair<FName, TUniquePtr<FMixinProfileBinding>>& pair : binding.Profiles)
			{
				if (pair.Value->Callables.Contains(function_name))
				{
					used = true;
					break;
				}
			}
			if (!used)
			{
				RestoreFunction(binding.Functions[index], binding.TargetClass, binding.RegistrationId);
				binding.Functions.RemoveAt(index);
			}
		}
	}

	bool AddOrReplaceProfile(FMixinBinding& binding, FName profile_name, PyObject* python_class, bool make_default)
	{
		if (binding.bMutating || binding.bUnregistering)
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin profile registration is already being modified");
			return false;
		}
		TGuardValue<bool> mutation_guard(binding.bMutating, true);

		TArray<FMixinCandidate> candidates;
		if (!CollectCandidates(binding.TargetClass, &binding, python_class, candidates))
		{
			ReleaseCandidates(candidates);
			return false;
		}

		const int32 original_function_count = binding.Functions.Num();
		FString error;
		for (const FMixinCandidate& candidate : candidates)
		{
			if (FindFunctionBinding(&binding, candidate.PublicName))
			{
				continue;
			}
			FMixinFunctionBinding function_binding;
			if (!CreateInjectedFunction(
				binding,
				candidate.PublicName,
				candidate.OriginalFunction,
				function_binding,
				error))
			{
				for (int32 index = binding.Functions.Num() - 1; index >= original_function_count; --index)
				{
					RestoreFunction(binding.Functions[index], binding.TargetClass, binding.RegistrationId);
					binding.Functions.RemoveAt(index);
				}
				ClearFunctionCaches(binding.TargetClass);
				ReleaseCandidates(candidates);
				PyErr_Format(PyExc_RuntimeError, "%s", TCHAR_TO_UTF8(*error));
				return false;
			}
			binding.Functions.Add(function_binding);
		}

		if (TUniquePtr<FMixinProfileBinding>* existing = binding.Profiles.Find(profile_name))
		{
			TeardownProfile(binding.RegistrationId, profile_name, (*existing)->GenerationId);
			ReleaseProfile(*existing->Get());
			binding.Profiles.Remove(profile_name);
		}

		TUniquePtr<FMixinProfileBinding> profile = MakeUnique<FMixinProfileBinding>();
		profile->GenerationId = GNextMixinProfileGenerationId++;
		profile->ProfileName = profile_name;
		profile->PythonClass = python_class;
		Py_INCREF(python_class);
		for (FMixinCandidate& candidate : candidates)
		{
			profile->Callables.Add(candidate.PublicName, candidate.PythonCallable);
			candidate.PythonCallable = nullptr;
		}
		ReleaseCandidates(candidates);
		const int32 profile_function_count = profile->Callables.Num();
		binding.Profiles.Add(profile_name, MoveTemp(profile));
		PruneUnusedFunctions(binding);

		if (binding.DefaultProfile.IsNone() || make_default)
		{
			binding.DefaultProfile = profile_name;
		}
		ClearFunctionCaches(binding.TargetClass);

		UE_LOG(
			LogPython,
			Log,
			TEXT("Registered Python mixin profile %s on %s (%d profile functions, %d routed functions)"),
			*profile_name.ToString(),
			*binding.TargetClass->GetPathName(),
			profile_function_count,
			binding.Functions.Num());
		return true;
	}

	FMixinBinding* RegisterProfileInternal(UClass* target_class, FName profile_name, PyObject* python_class, bool make_default)
	{
		if (!ValidateTargetClass(target_class))
		{
			return nullptr;
		}
		if (profile_name.IsNone())
		{
			PyErr_SetString(PyExc_ValueError, "mixin profile name cannot be empty");
			return nullptr;
		}
		if (!PyType_Check(python_class))
		{
			PyErr_SetString(PyExc_TypeError, "mixin profile expects a Python class");
			return nullptr;
		}

		FMixinBinding* binding = FindBindingForClass(target_class);
		bool created_binding = false;
		if (!binding || binding->TargetClass != target_class)
		{
			TUniquePtr<FMixinBinding> new_binding = MakeUnique<FMixinBinding>();
			new_binding->RegistrationId = GNextMixinRegistrationId++;
			new_binding->TargetClass = target_class;
			binding = new_binding.Get();
			GMixinsById.Add(binding->RegistrationId, binding);
			GMixinsByClass.Add(target_class, MoveTemp(new_binding));
			created_binding = true;
		}

		if (!AddOrReplaceProfile(*binding, profile_name, python_class, make_default))
		{
			if (created_binding)
			{
				UnregisterBinding(target_class);
			}
			return nullptr;
		}
		return binding;
	}

	UClass* PythonObjectAsClass(PyObject* value)
	{
		ue_PyUObject* py_class = ue_is_pyuobject(value);
		return py_class ? Cast<UClass>(py_class->ue_object) : nullptr;
	}

	UObject* PythonObjectAsUObject(PyObject* value)
	{
		ue_PyUObject* py_object = ue_is_pyuobject(value);
		return py_object ? py_object->ue_object : nullptr;
	}

	bool PythonObjectAsProfileName(PyObject* value, FName& out_name, bool allow_none = false)
	{
		if (allow_none && value == Py_None)
		{
			out_name = NAME_None;
			return true;
		}
		if (!PyUnicodeOrString_Check(value))
		{
			PyErr_SetString(PyExc_TypeError, "mixin profile name must be a string");
			return false;
		}
		const char* utf8 = UEPyUnicode_AsUTF8(value);
		if (!utf8)
		{
			return false;
		}
		out_name = FName(UTF8_TO_TCHAR(utf8));
		if (!allow_none && out_name.IsNone())
		{
			PyErr_SetString(PyExc_ValueError, "mixin profile name cannot be empty");
			return false;
		}
		return true;
	}

	PyObject* LoadPythonClass(const FUEPPythonMixinProfile& entry)
	{
		if (entry.PythonModule.IsEmpty() || entry.PythonClass.IsEmpty())
		{
			PyErr_Format(
				PyExc_ValueError,
				"mixin profile '%s' requires PythonModule and PythonClass",
				TCHAR_TO_UTF8(*entry.ProfileName.ToString()));
			return nullptr;
		}

		PyObject* current = PyImport_ImportModule(TCHAR_TO_UTF8(*entry.PythonModule));
		if (!current)
		{
			return nullptr;
		}
		TArray<FString> attribute_path;
		entry.PythonClass.ParseIntoArray(attribute_path, TEXT("."), true);
		for (const FString& attribute : attribute_path)
		{
			PyObject* next = PyObject_GetAttrString(current, TCHAR_TO_UTF8(*attribute));
			Py_DECREF(current);
			if (!next)
			{
				return nullptr;
			}
			current = next;
		}
		if (!PyType_Check(current))
		{
			Py_DECREF(current);
			PyErr_Format(
				PyExc_TypeError,
				"%s.%s is not a Python class",
				TCHAR_TO_UTF8(*entry.PythonModule),
				TCHAR_TO_UTF8(*entry.PythonClass));
			return nullptr;
		}
		return current;
	}

	void ReleaseLoadedProfiles(TArray<FLoadedProfile>& profiles)
	{
		for (FLoadedProfile& profile : profiles)
		{
			Py_XDECREF(profile.PythonClass);
			profile.PythonClass = nullptr;
		}
	}

	bool DeclaresMixinInterface(const UClass* target_class)
	{
		if (!target_class)
		{
			return false;
		}
		const UClass* interface_class = UUEPPythonMixinInterface::StaticClass();
		for (const FImplementedInterface& implemented : target_class->Interfaces)
		{
			if (implemented.Class == interface_class)
			{
				return true;
			}
		}
		return false;
	}

	bool IsDiscoverableMixinClass(UClass* target_class)
	{
		if (!target_class ||
			!target_class->HasAnyClassFlags(CLASS_CompiledFromBlueprint) ||
			target_class->HasAnyClassFlags(CLASS_NewerVersionExists) ||
			target_class->HasAnyFlags(RF_Transient) ||
			target_class->GetOutermost() == GetTransientPackage() ||
			!DeclaresMixinInterface(target_class))
		{
			return false;
		}

		const FString class_name = target_class->GetName();
		return !class_name.StartsWith(TEXT("SKEL_")) &&
			!class_name.StartsWith(TEXT("REINST_")) &&
			!class_name.StartsWith(TEXT("TRASHCLASS_"));
	}

	bool RegisterDeclaredMixinInternal(UClass* target_class, bool error_if_missing)
	{
		if (!IsDiscoverableMixinClass(target_class))
		{
			if (error_if_missing)
			{
				PyErr_SetString(
					PyExc_TypeError,
					"target must be a persistent Blueprint class that directly declares UEPPythonMixinInterface");
			}
			return false;
		}

		const TWeakObjectPtr<UClass> declaring_key(target_class);
		if (GDeclaringClasses.Contains(declaring_key))
		{
			return false;
		}
		GDeclaringClasses.Add(declaring_key);

		UObject* cdo = target_class->GetDefaultObject();
		UUEPPythonMixinSet* mixin_set = nullptr;
		if (cdo)
		{
			FUEPyScopedMixinCallback callback_scope;
			mixin_set = IUEPPythonMixinInterface::Execute_GetPythonMixinSet(cdo);
		}
		if (!mixin_set)
		{
			GDeclaringClasses.Remove(declaring_key);
			if (error_if_missing)
			{
				PyErr_SetString(PyExc_ValueError, "UEPPythonMixinInterface returned no Mixin Set on the class default object");
			}
			return false;
		}

		if (TUniquePtr<FMixinBinding>* existing = GMixinsByClass.Find(target_class))
		{
			if ((*existing)->DeclaredSet.Get() == mixin_set)
			{
				GDeclaringClasses.Remove(declaring_key);
				return true;
			}
			if (!(*existing)->DeclaredSet.IsValid())
			{
				GDeclaringClasses.Remove(declaring_key);
				if (error_if_missing)
				{
					PyErr_SetString(PyExc_RuntimeError, "target class already has a manually registered mixin router");
				}
				return false;
			}
		}

		TArray<FLoadedProfile> loaded_profiles;
		TSet<FName> seen_names;
		for (const FUEPPythonMixinProfile& entry : mixin_set->Profiles)
		{
			if (entry.ProfileName.IsNone())
			{
				PyErr_SetString(PyExc_ValueError, "Mixin Set contains an empty profile name");
				ReleaseLoadedProfiles(loaded_profiles);
				GDeclaringClasses.Remove(declaring_key);
				return false;
			}
			if (seen_names.Contains(entry.ProfileName))
			{
				PyErr_Format(
					PyExc_ValueError,
					"Mixin Set contains duplicate profile '%s'",
					TCHAR_TO_UTF8(*entry.ProfileName.ToString()));
				ReleaseLoadedProfiles(loaded_profiles);
				GDeclaringClasses.Remove(declaring_key);
				return false;
			}
			PyObject* python_class = LoadPythonClass(entry);
			if (!python_class)
			{
				ReleaseLoadedProfiles(loaded_profiles);
				GDeclaringClasses.Remove(declaring_key);
				return false;
			}
			FLoadedProfile& loaded = loaded_profiles.AddDefaulted_GetRef();
			loaded.ProfileName = entry.ProfileName;
			loaded.PythonClass = python_class;
			seen_names.Add(entry.ProfileName);
		}

		if (loaded_profiles.IsEmpty())
		{
			PyErr_SetString(PyExc_ValueError, "Mixin Set contains no profiles");
			GDeclaringClasses.Remove(declaring_key);
			return false;
		}
		FName default_profile = mixin_set->DefaultProfile;
		if (default_profile.IsNone() && loaded_profiles.Num() == 1)
		{
			default_profile = loaded_profiles[0].ProfileName;
		}
		if (default_profile.IsNone() || !seen_names.Contains(default_profile))
		{
			PyErr_Format(
				PyExc_ValueError,
				"Mixin Set default profile '%s' is not declared",
				TCHAR_TO_UTF8(*default_profile.ToString()));
			ReleaseLoadedProfiles(loaded_profiles);
			GDeclaringClasses.Remove(declaring_key);
			return false;
		}

		if (GMixinsByClass.Contains(target_class))
		{
			UnregisterBinding(target_class);
		}
		FMixinBinding* binding = nullptr;
		for (FLoadedProfile& loaded : loaded_profiles)
		{
			binding = RegisterProfileInternal(
				target_class,
				loaded.ProfileName,
				loaded.PythonClass,
				loaded.ProfileName == default_profile);
			if (!binding)
			{
				UnregisterBinding(target_class);
				ReleaseLoadedProfiles(loaded_profiles);
				GDeclaringClasses.Remove(declaring_key);
				return false;
			}
		}
		ReleaseLoadedProfiles(loaded_profiles);
		binding->DeclaredSet = mixin_set;
		binding->DefaultProfile = default_profile;
		GDeclaringClasses.Remove(declaring_key);
		UE_LOG(
			LogPython,
			Log,
			TEXT("Registered declared Python Mixin Set %s on %s"),
			*mixin_set->GetPathName(),
			*target_class->GetPathName());
		return true;
	}

	int32 RegisterLoadedMixinInterfacesInternal()
	{
		int32 registered_count = 0;
		for (TObjectIterator<UClass> it; it; ++it)
		{
			UClass* target_class = *it;
			if (!IsDiscoverableMixinClass(target_class))
			{
				continue;
			}
			const bool had_binding = GMixinsByClass.Contains(target_class);
			if (RegisterDeclaredMixinInternal(target_class, false) && !had_binding)
			{
				++registered_count;
			}
			else if (PyErr_Occurred())
			{
				unreal_engine_py_log_error();
			}
		}
		return registered_count;
	}

	void RegisterMixinInterfacesInPackage(UPackage* loaded_package)
	{
		if (!loaded_package)
		{
			return;
		}

		TArray<UObject*> loaded_objects;
		GetObjectsWithPackage(
			loaded_package,
			loaded_objects,
			EGetObjectsFlags::IncludeNestedObjects,
			RF_Transient);
		for (UObject* loaded_object : loaded_objects)
		{
			UClass* target_class = Cast<UClass>(loaded_object);
			if (!IsDiscoverableMixinClass(target_class))
			{
				continue;
			}
			RegisterDeclaredMixinInternal(target_class, false);
			if (PyErr_Occurred())
			{
				unreal_engine_py_log_error();
			}
		}
	}

	void OnMixinPackageLoadCompleted(UPackage* loaded_package)
	{
		if (IsRunningCookCommandlet() || !loaded_package || !Py_IsInitialized())
		{
			return;
		}
		if (!IsInGameThread() || FUEPyScopedMixinCallback::IsActive() || GMixinMutationDepth > 0)
		{
			const TWeakObjectPtr<UPackage> weak_package(loaded_package);
			AsyncTask(ENamedThreads::GameThread, [weak_package]()
			{
				if (GMixinPackageLoadedHandle.IsValid())
				{
					OnMixinPackageLoadCompleted(weak_package.Get());
				}
			});
			return;
		}

		FScopePythonGIL gil;
		FScopedMixinMutation mutation_scope;
		RegisterMixinInterfacesInPackage(loaded_package);
	}
}

void UPythonFunction::SetMixinRegistration(uint64 registration_id, FName function_name)
{
	mixin_registration_id = registration_id;
	mixin_function_name = function_name;
}

void UPythonFunction::ClearMixinRegistration()
{
	mixin_registration_id = 0;
	mixin_function_name = NAME_None;
}

EUEPyMixinDispatch ue_py_resolve_mixin_call(
	UPythonFunction* function,
	UObject* context,
	PyObject*& out_callable,
	UFunction*& out_original)
{
	out_callable = nullptr;
	out_original = nullptr;
	if (!function || !function->IsMixinFunction())
	{
		return EUEPyMixinDispatch::NotMixin;
	}

	FMixinBinding** found = GMixinsById.Find(function->GetMixinRegistrationId());
	if (!found || !*found)
	{
		PyErr_SetString(PyExc_RuntimeError, "Python mixin registration is no longer active");
		return EUEPyMixinDispatch::Error;
	}
	FMixinBinding* binding = *found;
	FMixinProfileBinding* profile = ResolveAndInitializeProfile(binding, context);
	if (!profile)
	{
		return EUEPyMixinDispatch::Error;
	}

	if (PyObject** callable = profile->Callables.Find(function->GetMixinFunctionName()))
	{
		out_callable = *callable;
		Py_INCREF(out_callable);
		return EUEPyMixinDispatch::Python;
	}

	FMixinFunctionBinding* function_binding = FindFunctionBinding(binding, function->GetMixinFunctionName());
	if (!function_binding || !function_binding->OriginalFunction)
	{
		PyErr_Format(
			PyExc_RuntimeError,
			"mixin router lost original function %s",
			TCHAR_TO_UTF8(*function->GetMixinFunctionName().ToString()));
		return EUEPyMixinDispatch::Error;
	}
	out_original = function_binding->OriginalFunction;
	return EUEPyMixinDispatch::Original;
}

PyObject* ue_py_get_mixin_attribute(ue_PyUObject* self, PyObject* attr_name)
{
	if (!self || !self->ue_object || !PyUnicodeOrString_Check(attr_name))
	{
		return nullptr;
	}

	FMixinBinding* binding = FindBindingForClass(self->ue_object->GetClass());
	if (!binding)
	{
		return nullptr;
	}

	// Attribute lookup is also used by ordinary Python getattr(..., default)
	// inside __init__. Re-entering the full resolver there would recursively
	// initialize the same object. While initialization is active, expose the
	// already selected class for helper lookup without running lifecycle again;
	// unresolved/teardown states simply behave like a missing Python attribute.
	const TWeakObjectPtr<UObject> object_key(self->ue_object);
	FMixinInstanceState* state = binding->InstanceStates.Find(object_key);
	FMixinProfileBinding* profile = nullptr;
	if (state && state->bInitializing)
	{
		profile = FindProfile(binding, state->ActiveProfile);
		if (!profile || profile->GenerationId != state->ActiveGenerationId)
		{
			return nullptr;
		}
	}
	else if (state && (state->bResolving || state->bTearingDown))
	{
		return nullptr;
	}
	else
	{
		profile = ResolveAndInitializeProfile(binding, self->ue_object);
	}
	if (!profile)
	{
		return nullptr;
	}

	PyObject* python_class = profile->PythonClass;
	Py_INCREF(python_class);
	PyObject* value = nullptr;
	{
		FUEPyScopedMixinCallback callback_scope;
		value = PyObject_GetAttr(python_class, attr_name);
	}
	Py_DECREF(python_class);
	if (!value)
	{
		if (PyErr_ExceptionMatches(PyExc_AttributeError))
		{
			PyErr_Clear();
		}
		return nullptr;
	}
	if (PyFunction_Check(value))
	{
		PyObject* bound = PyMethod_New(value, reinterpret_cast<PyObject*>(self));
		Py_DECREF(value);
		return bound;
	}
	return value;
}

PyObject* py_unreal_engine_register_mixin(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	PyObject* python_class = nullptr;
	if (!PyArg_ParseTuple(args, "OO:register_mixin", &py_target, &python_class))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "register_mixin must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("register_mixin"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;

	UClass* target_class = PythonObjectAsClass(py_target);
	if (!ValidateTargetClass(target_class))
	{
		return nullptr;
	}
	if (!PyType_Check(python_class))
	{
		return PyErr_Format(PyExc_TypeError, "register_mixin expects a Python class");
	}

	TArray<FMixinCandidate> candidates;
	FMixinBinding* current = FindBindingForClass(target_class);
	if (!CollectCandidates(target_class, current, python_class, candidates))
	{
		ReleaseCandidates(candidates);
		return nullptr;
	}
	ReleaseCandidates(candidates);
	if (GMixinsByClass.Contains(target_class))
	{
		UnregisterBinding(target_class);
	}

	FMixinBinding* binding = RegisterProfileInternal(
		target_class,
		DefaultMixinProfileName,
		python_class,
		true);
	if (!binding)
	{
		return nullptr;
	}
	Py_INCREF(python_class);
	return python_class;
}

PyObject* py_unreal_engine_register_mixin_profile(PyObject* self, PyObject* args, PyObject* kwargs)
{
	PyObject* py_target = nullptr;
	PyObject* py_profile_name = nullptr;
	PyObject* python_class = nullptr;
	int make_default = 0;
	static char* keywords[] = {
		(char*)"target_class",
		(char*)"profile",
		(char*)"python_class",
		(char*)"make_default",
		nullptr,
	};
	if (!PyArg_ParseTupleAndKeywords(
		args,
		kwargs,
		"OOO|p:register_mixin_profile",
		keywords,
		&py_target,
		&py_profile_name,
		&python_class,
		&make_default))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "register_mixin_profile must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("register_mixin_profile"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;

	FName profile_name;
	if (!PythonObjectAsProfileName(py_profile_name, profile_name))
	{
		return nullptr;
	}
	FMixinBinding* binding = RegisterProfileInternal(
		PythonObjectAsClass(py_target),
		profile_name,
		python_class,
		make_default != 0);
	if (!binding)
	{
		return nullptr;
	}
	Py_INCREF(python_class);
	return python_class;
}

PyObject* py_unreal_engine_mixin(PyObject* self, PyObject* args, PyObject* kwargs)
{
	PyObject* py_target = nullptr;
	PyObject* py_profile_name = Py_None;
	int make_default = 0;
	static char* keywords[] = {
		(char*)"target_class",
		(char*)"profile",
		(char*)"make_default",
		nullptr,
	};
	if (!PyArg_ParseTupleAndKeywords(
		args,
		kwargs,
		"O|Op:mixin",
		keywords,
		&py_target,
		&py_profile_name,
		&make_default))
	{
		return nullptr;
	}

	FName profile_name;
	if (!PythonObjectAsProfileName(py_profile_name, profile_name, true))
	{
		return nullptr;
	}

	PyObject* functools = PyImport_ImportModule("functools");
	if (!functools)
	{
		return nullptr;
	}
	PyObject* partial = PyObject_GetAttrString(functools, "partial");
	Py_DECREF(functools);
	if (!partial)
	{
		return nullptr;
	}

	PyObject* unreal_engine_module = PyImport_AddModule("unreal_engine");
	const char* function_name = profile_name.IsNone() ? "register_mixin" : "register_mixin_profile";
	PyObject* register_function = unreal_engine_module
		? PyObject_GetAttrString(unreal_engine_module, function_name)
		: nullptr;
	if (!register_function)
	{
		Py_DECREF(partial);
		return nullptr;
	}

	PyObject* partial_args = profile_name.IsNone()
		? PyTuple_Pack(2, register_function, py_target)
		: PyTuple_Pack(3, register_function, py_target, py_profile_name);
	PyObject* partial_kwargs = nullptr;
	if (!profile_name.IsNone() && make_default)
	{
		partial_kwargs = Py_BuildValue("{s:O}", "make_default", Py_True);
	}
	PyObject* decorator = partial_args
		? PyObject_Call(partial, partial_args, partial_kwargs)
		: nullptr;
	Py_XDECREF(partial_kwargs);
	Py_XDECREF(partial_args);
	Py_DECREF(register_function);
	Py_DECREF(partial);
	return decorator;
}

PyObject* py_unreal_engine_set_mixin_profile(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	PyObject* py_profile_name = nullptr;
	if (!PyArg_ParseTuple(args, "OO:set_mixin_profile", &py_target, &py_profile_name))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "set_mixin_profile must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("set_mixin_profile"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;

	UObject* target = PythonObjectAsUObject(py_target);
	FName profile_name;
	if (!target || !PythonObjectAsProfileName(py_profile_name, profile_name))
	{
		return target ? nullptr : PyErr_Format(PyExc_TypeError, "set_mixin_profile target must be a UObject instance");
	}
	FMixinBinding* binding = FindBindingForClass(target->GetClass());
	if (!binding)
	{
		return PyErr_Format(PyExc_RuntimeError, "target object has no active mixin router");
	}
	FMixinProfileBinding* requested_profile = FindProfile(binding, profile_name);
	if (!requested_profile)
	{
		return PyErr_Format(
			PyExc_KeyError,
			"Python mixin profile '%s' is not registered",
			TCHAR_TO_UTF8(*profile_name.ToString()));
	}

	if (binding->bMutating || binding->bUnregistering)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin router is already being modified");
	}
	TGuardValue<bool> binding_mutation(binding->bMutating, true);
	const uint64 registration_id = binding->RegistrationId;
	const uint64 requested_generation = requested_profile->GenerationId;
	const TWeakObjectPtr<UObject> object_key(target);
	FMixinInstanceState* state = &binding->InstanceStates.FindOrAdd(object_key);
	if (IsInstanceStateBusy(*state))
	{
		SetBusyStateError(TEXT("change"), target);
		return nullptr;
	}
	if (state->ActiveGenerationId != 0 && state->ActiveGenerationId != requested_generation)
	{
		if (!TeardownActiveProfile(registration_id, target))
		{
			return nullptr;
		}
	}
	binding = FindBindingById(registration_id);
	state = binding ? &binding->InstanceStates.FindOrAdd(object_key) : nullptr;
	if (!state)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin state disappeared while changing profiles");
	}
	state->ExplicitProfile = profile_name;
	Py_RETURN_NONE;
}

PyObject* py_unreal_engine_clear_mixin_profile(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	if (!PyArg_ParseTuple(args, "O:clear_mixin_profile", &py_target))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "clear_mixin_profile must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("clear_mixin_profile"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	UObject* target = PythonObjectAsUObject(py_target);
	if (!target)
	{
		return PyErr_Format(PyExc_TypeError, "clear_mixin_profile target must be a UObject instance");
	}
	FMixinBinding* binding = FindBindingForClass(target->GetClass());
	if (!binding)
	{
		return PyErr_Format(PyExc_RuntimeError, "target object has no active mixin router");
	}

	if (binding->bMutating || binding->bUnregistering)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin router is already being modified");
	}
	TGuardValue<bool> binding_mutation(binding->bMutating, true);
	const uint64 registration_id = binding->RegistrationId;
	const TWeakObjectPtr<UObject> object_key(target);
	FMixinInstanceState* state = &binding->InstanceStates.FindOrAdd(object_key);
	if (IsInstanceStateBusy(*state))
	{
		SetBusyStateError(TEXT("clear"), target);
		return nullptr;
	}
	if (state->ActiveGenerationId != 0)
	{
		if (!TeardownActiveProfile(registration_id, target))
		{
			return nullptr;
		}
	}
	binding = FindBindingById(registration_id);
	state = binding ? &binding->InstanceStates.FindOrAdd(object_key) : nullptr;
	if (!state)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin state disappeared while clearing its profile");
	}
	state->ExplicitProfile = NAME_None;
	Py_RETURN_NONE;
}

PyObject* py_unreal_engine_get_mixin_profile(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	if (!PyArg_ParseTuple(args, "O:get_mixin_profile", &py_target))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "get_mixin_profile must run on Unreal's game thread");
	}
	UObject* target = PythonObjectAsUObject(py_target);
	if (!target)
	{
		return PyErr_Format(PyExc_TypeError, "get_mixin_profile target must be a UObject instance");
	}
	FMixinBinding* binding = FindBindingForClass(target->GetClass());
	if (!binding)
	{
		return PyErr_Format(PyExc_RuntimeError, "target object has no active mixin router");
	}
	const TWeakObjectPtr<UObject> object_key(target);
	FName profile_name = NAME_None;
	if (!ResolveRequestedProfile(binding->RegistrationId, target, profile_name))
	{
		return nullptr;
	}
	if (profile_name.IsNone())
	{
		Py_RETURN_NONE;
	}
	return PyUnicode_FromString(TCHAR_TO_UTF8(*profile_name.ToString()));
}

PyObject* py_unreal_engine_set_default_mixin_profile(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	PyObject* py_profile_name = nullptr;
	if (!PyArg_ParseTuple(args, "OO:set_default_mixin_profile", &py_target, &py_profile_name))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "set_default_mixin_profile must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("set_default_mixin_profile"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	FName profile_name;
	if (!PythonObjectAsProfileName(py_profile_name, profile_name))
	{
		return nullptr;
	}
	UClass* target_class = PythonObjectAsClass(py_target);
	FMixinBinding* binding = target_class ? FindBindingForClass(target_class) : nullptr;
	if (!binding || binding->TargetClass != target_class)
	{
		return PyErr_Format(PyExc_RuntimeError, "target class has no active mixin router");
	}
	if (!FindProfile(binding, profile_name))
	{
		return PyErr_Format(PyExc_KeyError, "Python mixin profile '%s' is not registered", TCHAR_TO_UTF8(*profile_name.ToString()));
	}
	if (binding->bMutating || binding->bUnregistering)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin router is already being modified");
	}
	TGuardValue<bool> binding_mutation(binding->bMutating, true);
	const uint64 registration_id = binding->RegistrationId;
	TArray<TWeakObjectPtr<UObject>> instances_to_reset;
	for (const TPair<TWeakObjectPtr<UObject>, FMixinInstanceState>& pair : binding->InstanceStates)
	{
		const FMixinInstanceState& state = pair.Value;
		if (!state.ExplicitProfile.IsNone() || state.ActiveGenerationId == 0)
		{
			continue;
		}
		instances_to_reset.Add(pair.Key);
	}
	for (const TWeakObjectPtr<UObject>& object_key : instances_to_reset)
	{
		if (UObject* object = object_key.Get())
		{
			if (!TeardownActiveProfile(registration_id, object) && PyErr_Occurred())
			{
				unreal_engine_py_log_error();
			}
		}
	}
	binding = FindBindingById(registration_id);
	if (!binding)
	{
		return PyErr_Format(PyExc_RuntimeError, "target mixin router disappeared while changing its default profile");
	}
	binding->DefaultProfile = profile_name;
	Py_RETURN_NONE;
}

PyObject* py_unreal_engine_register_declared_mixin(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	if (!PyArg_ParseTuple(args, "O:register_declared_mixin", &py_target))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "register_declared_mixin must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("register_declared_mixin"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	UClass* target_class = PythonObjectAsClass(py_target);
	if (!RegisterDeclaredMixinInternal(target_class, true))
	{
		return nullptr;
	}
	Py_RETURN_NONE;
}

PyObject* py_unreal_engine_register_loaded_mixin_interfaces(PyObject* self, PyObject* args)
{
	if (!PyArg_ParseTuple(args, ":register_loaded_mixin_interfaces"))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "register_loaded_mixin_interfaces must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("register_loaded_mixin_interfaces"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	return PyLong_FromLong(RegisterLoadedMixinInterfacesInternal());
}

PyObject* py_unreal_engine_unregister_mixin(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	if (!PyArg_ParseTuple(args, "O:unregister_mixin", &py_target))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "unregister_mixin must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("unregister_mixin"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	UClass* target_class = PythonObjectAsClass(py_target);
	if (!target_class)
	{
		return PyErr_Format(PyExc_TypeError, "unregister_mixin target must be a UClass");
	}
	return PyBool_FromLong(UnregisterBinding(target_class) ? 1 : 0);
}

PyObject* py_unreal_engine_unregister_all_mixins(PyObject* self, PyObject* args)
{
	if (!PyArg_ParseTuple(args, ":unregister_all_mixins"))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "unregister_all_mixins must run on Unreal's game thread");
	}
	if (!EnsureMixinRegistryMutationAllowed("unregister_all_mixins"))
	{
		return nullptr;
	}
	FScopedMixinMutation mutation_scope;
	const int32 count = GMixinsByClass.Num();
	unreal_engine_python_unregister_all_mixins();
	return PyLong_FromLong(count);
}

PyObject* py_unreal_engine_get_registered_mixins(PyObject* self, PyObject* args)
{
	if (!PyArg_ParseTuple(args, ":get_registered_mixins"))
	{
		return nullptr;
	}
	if (!IsInGameThread())
	{
		return PyErr_Format(PyExc_RuntimeError, "get_registered_mixins must run on Unreal's game thread");
	}

	PyObject* result = PyList_New(0);
	if (!result)
	{
		return nullptr;
	}
	for (const TPair<UClass*, TUniquePtr<FMixinBinding>>& pair : GMixinsByClass)
	{
		const FMixinBinding& binding = *pair.Value;
		PyObject* item = PyDict_New();
		PyObject* target = reinterpret_cast<PyObject*>(ue_get_python_uobject_inc(binding.TargetClass));
		PyObject* functions = PyList_New(binding.Functions.Num());
		PyObject* profiles = PyDict_New();
		if (!item || !target || !functions || !profiles)
		{
			Py_XDECREF(item);
			Py_XDECREF(target);
			Py_XDECREF(functions);
			Py_XDECREF(profiles);
			Py_DECREF(result);
			return nullptr;
		}

		for (int32 index = 0; index < binding.Functions.Num(); ++index)
		{
			PyList_SET_ITEM(functions, index, PyUnicode_FromString(TCHAR_TO_UTF8(*binding.Functions[index].PublicName.ToString())));
		}
		for (const TPair<FName, TUniquePtr<FMixinProfileBinding>>& profile_pair : binding.Profiles)
		{
			const FMixinProfileBinding& profile = *profile_pair.Value;
			PyObject* profile_item = PyDict_New();
			PyObject* profile_functions = PyList_New(profile.Callables.Num());
			if (!profile_item || !profile_functions)
			{
				Py_XDECREF(profile_item);
				Py_XDECREF(profile_functions);
				Py_DECREF(item);
				Py_DECREF(target);
				Py_DECREF(functions);
				Py_DECREF(profiles);
				Py_DECREF(result);
				return nullptr;
			}
			int32 function_index = 0;
			for (const TPair<FName, PyObject*>& callable : profile.Callables)
			{
				PyList_SET_ITEM(profile_functions, function_index++, PyUnicode_FromString(TCHAR_TO_UTF8(*callable.Key.ToString())));
			}
			PyObject* generation_id = PyLong_FromUnsignedLongLong(profile.GenerationId);
			PyDict_SetItemString(profile_item, "python_class", profile.PythonClass);
			PyDict_SetItemString(profile_item, "functions", profile_functions);
			if (generation_id)
			{
				PyDict_SetItemString(profile_item, "generation_id", generation_id);
				Py_DECREF(generation_id);
			}
			PyDict_SetItemString(profiles, TCHAR_TO_UTF8(*profile_pair.Key.ToString()), profile_item);
			Py_DECREF(profile_functions);
			Py_DECREF(profile_item);
		}

		PyDict_SetItemString(item, "target_class", target);
		PyDict_SetItemString(item, "functions", functions);
		PyDict_SetItemString(item, "profiles", profiles);
		PyObject* default_profile = PyUnicode_FromString(TCHAR_TO_UTF8(*binding.DefaultProfile.ToString()));
		PyObject* registration_id = PyLong_FromUnsignedLongLong(binding.RegistrationId);
		if (default_profile)
		{
			PyDict_SetItemString(item, "default_profile", default_profile);
			Py_DECREF(default_profile);
		}
		if (registration_id)
		{
			PyDict_SetItemString(item, "registration_id", registration_id);
			Py_DECREF(registration_id);
		}
		if (const TUniquePtr<FMixinProfileBinding>* default_binding = binding.Profiles.Find(binding.DefaultProfile))
		{
			PyDict_SetItemString(item, "python_class", (*default_binding)->PythonClass);
		}
		if (binding.DeclaredSet.IsValid())
		{
			PyObject* declared_set = reinterpret_cast<PyObject*>(ue_get_python_uobject_inc(binding.DeclaredSet.Get()));
			if (declared_set)
			{
				PyDict_SetItemString(item, "mixin_set", declared_set);
				Py_DECREF(declared_set);
			}
		}
		Py_DECREF(target);
		Py_DECREF(functions);
		Py_DECREF(profiles);
		PyList_Append(result, item);
		Py_DECREF(item);
	}
	return result;
}

PyObject* py_ue_call_mixin_original(ue_PyUObject* self, PyObject* args, PyObject* kwargs)
{
	ue_py_check(self);
	if (PyTuple_Size(args) < 1)
	{
		return PyErr_Format(PyExc_TypeError, "call_mixin_original requires a UFunction name");
	}

	PyObject* py_name = PyTuple_GetItem(args, 0);
	if (!PyUnicodeOrString_Check(py_name))
	{
		return PyErr_Format(PyExc_TypeError, "call_mixin_original function name must be a string");
	}
	const FName function_name(UTF8_TO_TCHAR(UEPyUnicode_AsUTF8(py_name)));
	FMixinBinding* binding = FindBindingForClass(self->ue_object->GetClass());
	FMixinFunctionBinding* function_binding = FindFunctionBinding(binding, function_name);
	if (!function_binding || !function_binding->OriginalFunction)
	{
		return PyErr_Format(
			PyExc_AttributeError,
			"no active mixin original named %s",
			TCHAR_TO_UTF8(*function_name.ToString()));
	}

	PyObject* forwarded_args = PyTuple_GetSlice(args, 1, PyTuple_Size(args));
	if (!forwarded_args)
	{
		return nullptr;
	}
	PyObject* result = nullptr;
	{
		FUEPyScopedMixinCallback callback_scope;
		result = py_ue_ufunction_call(
			function_binding->OriginalFunction,
			self->ue_object,
			forwarded_args,
			0,
			kwargs);
	}
	Py_DECREF(forwarded_args);
	return result;
}

void unreal_engine_python_enable_mixin_discovery()
{
	if (IsRunningCookCommandlet())
	{
		return;
	}
	if (!GMixinPackageLoadedHandle.IsValid())
	{
		GMixinPackageLoadedHandle = FCoreUObjectDelegates::OnPackageLoadCompleted.AddStatic(&OnMixinPackageLoadCompleted);
	}
	if (Py_IsInitialized() && !FUEPyScopedMixinCallback::IsActive() && GMixinMutationDepth == 0)
	{
		FScopePythonGIL gil;
		FScopedMixinMutation mutation_scope;
		RegisterLoadedMixinInterfacesInternal();
	}
}

void unreal_engine_python_disable_mixin_discovery()
{
	if (GMixinPackageLoadedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnPackageLoadCompleted.Remove(GMixinPackageLoadedHandle);
		GMixinPackageLoadedHandle.Reset();
	}
	GDeclaringClasses.Empty();
}

void unreal_engine_python_unregister_all_mixins()
{
	TArray<UClass*> target_classes;
	GMixinsByClass.GenerateKeyArray(target_classes);
	for (UClass* target_class : target_classes)
	{
		UnregisterBinding(target_class);
	}
}
