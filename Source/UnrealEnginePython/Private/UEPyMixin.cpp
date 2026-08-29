#include "UEPyMixin.h"

#include "PythonFunction.h"
#include "UEPyModule.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr const char* MixinGenerationKey = "__uep_mixin_registration__";
	constexpr const char* MixinTeardownName = "__uep_mixin_teardown__";

	struct FMixinFunctionBinding
	{
		FName PublicName = NAME_None;
		FName StoredOriginalName = NAME_None;
		UFunction* OriginalFunction = nullptr;
		UPythonFunction* InjectedFunction = nullptr;
		bool bOriginalOwnedByTarget = false;
	};

	struct FMixinBinding
	{
		uint64 RegistrationId = 0;
		UClass* TargetClass = nullptr;
		PyObject* PythonClass = nullptr;
		TArray<FMixinFunctionBinding> Functions;
		TSet<TWeakObjectPtr<UObject>> InitializedObjects;
	};

	struct FMixinCandidate
	{
		FName PublicName = NAME_None;
		UFunction* OriginalFunction = nullptr;
		PyObject* PythonCallable = nullptr; // Borrowed from the Python class dictionary.
	};

	TMap<UClass*, TUniquePtr<FMixinBinding>> GMixinsByClass;
	TMap<uint64, FMixinBinding*> GMixinsById;
	uint64 GNextMixinRegistrationId = 1;

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

	bool EnsureInstanceInitialized(FMixinBinding* binding, UObject* context)
	{
		if (!binding || !context || !context->IsA(binding->TargetClass))
		{
			PyErr_SetString(PyExc_RuntimeError, "mixin dispatch target is no longer valid");
			return false;
		}

		ue_PyUObject* py_context = ue_get_python_uobject(context);
		if (!py_context || !py_context->py_dict)
		{
			PyErr_SetString(PyExc_RuntimeError, "unable to create the UObject Python wrapper for mixin dispatch");
			return false;
		}

		PyObject* generation = PyDict_GetItemString(py_context->py_dict, MixinGenerationKey);
		if (generation && PyLong_Check(generation) &&
			PyLong_AsUnsignedLongLong(generation) == binding->RegistrationId)
		{
			return true;
		}
		if (PyErr_Occurred())
		{
			return false;
		}

		PyObject* generation_value = PyLong_FromUnsignedLongLong(binding->RegistrationId);
		if (!generation_value ||
			PyDict_SetItemString(py_context->py_dict, MixinGenerationKey, generation_value) < 0)
		{
			Py_XDECREF(generation_value);
			return false;
		}
		Py_DECREF(generation_value);

		PyObject* initializer = GetLocalClassAttribute(binding->PythonClass, "__init__");
		if (initializer)
		{
			PyObject* result = PyObject_CallFunctionObjArgs(initializer, reinterpret_cast<PyObject*>(py_context), nullptr);
			Py_DECREF(initializer);
			if (!result)
			{
				PyDict_DelItemString(py_context->py_dict, MixinGenerationKey);
				return false;
			}
			Py_DECREF(result);
		}
		else if (PyErr_Occurred())
		{
			PyDict_DelItemString(py_context->py_dict, MixinGenerationKey);
			return false;
		}

		binding->InitializedObjects.Add(context);
		return true;
	}

	void TeardownInitializedObjects(FMixinBinding& binding)
	{
		PyObject* teardown = GetLocalClassAttribute(binding.PythonClass, MixinTeardownName);
		if (!teardown && PyErr_Occurred())
		{
			unreal_engine_py_log_error();
		}

		for (const TWeakObjectPtr<UObject>& weak_object : binding.InitializedObjects)
		{
			UObject* object = weak_object.Get();
			if (!object)
			{
				continue;
			}

			ue_PyUObject* py_object = FUnrealEnginePythonHouseKeeper::Get()->GetPyUObject(object);
			if (!py_object || !py_object->py_dict)
			{
				continue;
			}

			if (teardown)
			{
				PyObject* result = PyObject_CallFunctionObjArgs(teardown, reinterpret_cast<PyObject*>(py_object), nullptr);
				if (!result)
				{
					unreal_engine_py_log_error();
				}
				else
				{
					Py_DECREF(result);
				}
			}

			if (PyDict_DelItemString(py_object->py_dict, MixinGenerationKey) < 0)
			{
				PyErr_Clear();
			}
		}

		Py_XDECREF(teardown);
		binding.InitializedObjects.Empty();
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
		TeardownInitializedObjects(*binding);
		for (int32 index = binding->Functions.Num() - 1; index >= 0; --index)
		{
			RestoreFunction(binding->Functions[index], target_class, binding->RegistrationId);
		}
		ClearFunctionCaches(target_class);

		GMixinsById.Remove(binding->RegistrationId);
		Py_CLEAR(binding->PythonClass);
		GMixinsByClass.Remove(target_class);
		UE_LOG(LogPython, Log, TEXT("Unregistered Python mixin from %s"), *target_class->GetPathName());
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

	bool ValidatePythonCallableSignature(
		PyObject* python_callable,
		UFunction* original,
		const FString& function_name)
	{
		Py_ssize_t positional_count = 1; // The mixed UObject wrapper (self).
		for (TFieldIterator<FProperty> it(original);
			it && it->HasAnyPropertyFlags(CPF_Parm);
			++it)
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

	UPythonFunction* CreateInjectedFunction(
		FMixinBinding& binding,
		const FMixinCandidate& candidate,
		FMixinFunctionBinding& out_binding,
		FString& error)
	{
		UClass* target_class = binding.TargetClass;
		UFunction* original = candidate.OriginalFunction;
		out_binding.PublicName = candidate.PublicName;
		out_binding.OriginalFunction = original;
		out_binding.bOriginalOwnedByTarget = original->GetOuter() == target_class;

		if (out_binding.bOriginalOwnedByTarget)
		{
			target_class->RemoveFunctionFromFunctionMap(original);
			out_binding.StoredOriginalName = MakeUniqueObjectName(
				target_class,
				original->GetClass(),
				FName(*FString::Printf(TEXT("__UEP_Mixin_Original_%llu_%s"), binding.RegistrationId, *candidate.PublicName.ToString())));
			if (!original->Rename(
				*out_binding.StoredOriginalName.ToString(),
				target_class,
				REN_DontCreateRedirectors | REN_NonTransactional))
			{
				target_class->AddFunctionToFunctionMap(original, candidate.PublicName);
				error = FString::Printf(TEXT("unable to preserve original function %s"), *candidate.PublicName.ToString());
				return nullptr;
			}
		}

		UPythonFunction* injected = NewObject<UPythonFunction>(
			target_class,
			candidate.PublicName,
			RF_Public | RF_Transient | RF_MarkAsNative);
		if (!injected)
		{
			error = FString::Printf(TEXT("unable to allocate injected function %s"), *candidate.PublicName.ToString());
			if (out_binding.bOriginalOwnedByTarget)
			{
				original->Rename(*candidate.PublicName.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
				target_class->AddFunctionToFunctionMap(original, candidate.PublicName);
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
				error = FString::Printf(TEXT("unable to clone parameter %s.%s"), *candidate.PublicName.ToString(), *it->GetName());
				const FName discarded_name = MakeUniqueObjectName(
					target_class,
					UPythonFunction::StaticClass(),
					FName(*FString::Printf(TEXT("__UEP_Mixin_Failed_%llu_%s"), binding.RegistrationId, *candidate.PublicName.ToString())));
				injected->Rename(*discarded_name.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
				injected->MarkAsGarbage();
				if (out_binding.bOriginalOwnedByTarget)
				{
					original->Rename(*candidate.PublicName.ToString(), target_class, REN_DontCreateRedirectors | REN_NonTransactional);
					target_class->AddFunctionToFunctionMap(original, candidate.PublicName);
				}
				return nullptr;
			}
			cloned->Next = nullptr;
			property_builder.AppendNoTerminate(*cloned);
		}

		injected->StaticLink(true);
		injected->SetNativeFunc((FNativeFuncPtr)&UPythonFunction::CallPythonCallable);
		injected->SetPyCallable(candidate.PythonCallable);
		injected->SetMixinRegistration(binding.RegistrationId, candidate.PublicName);
		injected->AddToRoot();

		injected->Next = target_class->Children;
		target_class->Children = injected;
		target_class->AddFunctionToFunctionMap(injected, candidate.PublicName);
		out_binding.InjectedFunction = injected;
		return injected;
	}

	bool CollectCandidates(UClass* target_class, PyObject* python_class, TArray<FMixinCandidate>& candidates)
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
			UFunction* original = target_class->FindFunctionByName(function_name);
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

bool ue_py_prepare_mixin_call(UPythonFunction* function, UObject* context)
{
	if (!function || !function->IsMixinFunction())
	{
		return true;
	}

	FMixinBinding** found = GMixinsById.Find(function->GetMixinRegistrationId());
	if (!found || !*found)
	{
		PyErr_SetString(PyExc_RuntimeError, "Python mixin registration is no longer active");
		return false;
	}
	return EnsureInstanceInitialized(*found, context);
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
	if (!EnsureInstanceInitialized(binding, self->ue_object))
	{
		return nullptr;
	}

	PyObject* value = PyObject_GetAttr(binding->PythonClass, attr_name);
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

	ue_PyUObject* py_target_class = ue_is_pyuobject(py_target);
	UClass* target_class = py_target_class ? Cast<UClass>(py_target_class->ue_object) : nullptr;
	if (!target_class)
	{
		return PyErr_Format(PyExc_TypeError, "register_mixin target must be a UClass");
	}
	if (!target_class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		return PyErr_Format(PyExc_TypeError, "0.7 mixins are limited to Blueprint-generated classes");
	}
	if (!PyType_Check(python_class))
	{
		return PyErr_Format(PyExc_TypeError, "register_mixin expects a Python class");
	}
	for (const TPair<UClass*, TUniquePtr<FMixinBinding>>& pair : GMixinsByClass)
	{
		UClass* registered_class = pair.Key;
		if (registered_class != target_class &&
			(target_class->IsChildOf(registered_class) || registered_class->IsChildOf(target_class)))
		{
			return PyErr_Format(
				PyExc_TypeError,
				"0.7 does not allow simultaneous mixins on related Blueprint classes (%s and %s)",
				TCHAR_TO_UTF8(*target_class->GetName()),
				TCHAR_TO_UTF8(*registered_class->GetName()));
		}
	}

	TArray<FMixinCandidate> candidates;
	if (!CollectCandidates(target_class, python_class, candidates))
	{
		ReleaseCandidates(candidates);
		return nullptr;
	}

	// Validate against the currently visible signatures before replacing an
	// active generation. A bad reload therefore leaves the working mixin intact.
	if (GMixinsByClass.Contains(target_class))
	{
		UnregisterBinding(target_class);
		for (FMixinCandidate& candidate : candidates)
		{
			candidate.OriginalFunction = target_class->FindFunctionByName(candidate.PublicName);
			if (!candidate.OriginalFunction)
			{
				ReleaseCandidates(candidates);
				return PyErr_Format(
					PyExc_RuntimeError,
					"target function %s disappeared while replacing its mixin",
					TCHAR_TO_UTF8(*candidate.PublicName.ToString()));
			}
		}
	}

	TUniquePtr<FMixinBinding> binding = MakeUnique<FMixinBinding>();
	binding->RegistrationId = GNextMixinRegistrationId++;
	binding->TargetClass = target_class;
	binding->PythonClass = python_class;
	Py_INCREF(python_class);

	FString error;
	for (const FMixinCandidate& candidate : candidates)
	{
		FMixinFunctionBinding function_binding;
		if (!CreateInjectedFunction(*binding, candidate, function_binding, error))
		{
			for (int32 index = binding->Functions.Num() - 1; index >= 0; --index)
			{
				RestoreFunction(binding->Functions[index], target_class, binding->RegistrationId);
			}
			ClearFunctionCaches(target_class);
			Py_CLEAR(binding->PythonClass);
			ReleaseCandidates(candidates);
			return PyErr_Format(PyExc_RuntimeError, "%s", TCHAR_TO_UTF8(*error));
		}
		binding->Functions.Add(function_binding);
	}
	ReleaseCandidates(candidates);
	ClearFunctionCaches(target_class);

	FMixinBinding* binding_ptr = binding.Get();
	GMixinsById.Add(binding->RegistrationId, binding_ptr);
	GMixinsByClass.Add(target_class, MoveTemp(binding));
	FString python_class_name = UTF8_TO_TCHAR(Py_TYPE(python_class)->tp_name);
	PyObject* py_class_name = PyObject_GetAttrString(python_class, "__name__");
	if (py_class_name && PyUnicodeOrString_Check(py_class_name))
	{
		const char* class_name_utf8 = UEPyUnicode_AsUTF8(py_class_name);
		if (class_name_utf8)
		{
			python_class_name = UTF8_TO_TCHAR(class_name_utf8);
		}
	}
	Py_XDECREF(py_class_name);
	PyErr_Clear();
	UE_LOG(
		LogPython,
		Log,
		TEXT("Registered Python mixin %s on %s (%d functions)"),
		*python_class_name,
		*target_class->GetPathName(),
		binding_ptr->Functions.Num());

	Py_INCREF(python_class);
	return python_class;
}

PyObject* py_unreal_engine_mixin(PyObject* self, PyObject* args)
{
	PyObject* py_target = nullptr;
	if (!PyArg_ParseTuple(args, "O:mixin", &py_target))
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
	// Module-level functions in UEP are installed with a null ``self`` (see
	// unreal_engine_init_py_module), so resolve the sibling API from the module
	// instead of dereferencing ``self`` here.
	PyObject* unreal_engine_module = PyImport_AddModule("unreal_engine"); // Borrowed.
	if (!unreal_engine_module)
	{
		Py_DECREF(partial);
		return nullptr;
	}
	PyObject* register_function = PyObject_GetAttrString(unreal_engine_module, "register_mixin");
	if (!register_function)
	{
		Py_DECREF(partial);
		return nullptr;
	}
	PyObject* decorator = PyObject_CallFunctionObjArgs(partial, register_function, py_target, nullptr);
	Py_DECREF(register_function);
	Py_DECREF(partial);
	return decorator;
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

	ue_PyUObject* py_target_class = ue_is_pyuobject(py_target);
	UClass* target_class = py_target_class ? Cast<UClass>(py_target_class->ue_object) : nullptr;
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
		if (!item || !target || !functions)
		{
			Py_XDECREF(item);
			Py_XDECREF(target);
			Py_XDECREF(functions);
			Py_DECREF(result);
			return nullptr;
		}
		for (int32 index = 0; index < binding.Functions.Num(); ++index)
		{
			PyList_SET_ITEM(functions, index, PyUnicode_FromString(TCHAR_TO_UTF8(*binding.Functions[index].PublicName.ToString())));
		}
		PyDict_SetItemString(item, "target_class", target);
		PyDict_SetItemString(item, "python_class", binding.PythonClass);
		PyDict_SetItemString(item, "functions", functions);
		PyObject* registration_id = PyLong_FromUnsignedLongLong(binding.RegistrationId);
		if (registration_id)
		{
			PyDict_SetItemString(item, "registration_id", registration_id);
			Py_DECREF(registration_id);
		}
		Py_DECREF(target);
		Py_DECREF(functions);
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
	PyObject* result = py_ue_ufunction_call(
		function_binding->OriginalFunction,
		self->ue_object,
		forwarded_args,
		0,
		kwargs);
	Py_DECREF(forwarded_args);
	return result;
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
