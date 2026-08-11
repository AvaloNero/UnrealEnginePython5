#include "UEPyModule.h"
#include "PythonClass.h"
#include "UObject/UEPyObject.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/BlueprintGraph/Public/BlueprintActionDatabase.h"
#endif

#if UEP_WITH_DYNAMIC_CLASS_GENERATION

// hack for avoiding loops in class constructors (thanks to the Unreal.js project for the idea)
UClass *ue_py_class_constructor_placeholder = nullptr;
static void UEPyClassConstructor(UClass *u_class, const FObjectInitializer &ObjectInitializer)
{
	if (UPythonClass *u_py_class_casted = Cast<UPythonClass>(u_class))
	{
		ue_py_class_constructor_placeholder = u_class;
	}
	u_class->ClassConstructor(ObjectInitializer);
	ue_py_class_constructor_placeholder = nullptr;
}

static bool UEPyResolvePropertyDeclaration(
	PyObject* value,
	FFieldClass*& field_class,
	PyObject*& type_metadata)
{
	field_class = ue_py_get_ffield_class_from_capsule(value);
	type_metadata = nullptr;
	if (field_class)
	{
		return true;
	}

	ue_PyUObject* py_uobject = ue_is_pyuobject(value);
	if (!py_uobject)
	{
		return false;
	}

	if (py_uobject->ue_object->IsA<UClass>())
	{
		field_class = FObjectProperty::StaticClass();
		type_metadata = value;
		return true;
	}
	if (py_uobject->ue_object->IsA<UScriptStruct>())
	{
		field_class = FStructProperty::StaticClass();
		type_metadata = value;
		return true;
	}
	if (py_uobject->ue_object->IsA<UEnum>())
	{
		field_class = FEnumProperty::StaticClass();
		type_metadata = value;
		return true;
	}

	return false;
}

static int UEPyApplyCallableFlag(PyObject* callable, const char* attribute, uint32 flag, uint32& flags)
{
	PyObject* value = PyObject_GetAttrString(callable, attribute);
	if (!value)
	{
		PyErr_Clear();
		return 0;
	}

	const int enabled = PyObject_IsTrue(value);
	Py_DECREF(value);
	if (enabled < 0)
	{
		return -1;
	}
	if (enabled)
	{
		flags |= flag;
	}
	return 0;
}

static int UEPyTryAddClassProperty(ue_PyUObject* self, const char* name, PyObject* value)
{
	FFieldClass* first_field_class = nullptr;
	FFieldClass* second_field_class = nullptr;
	PyObject* first_metadata = nullptr;
	PyObject* second_metadata = nullptr;
	PyObject* declaration = nullptr;
	PyObject* call_args = nullptr;

	if (UEPyResolvePropertyDeclaration(value, first_field_class, first_metadata))
	{
		declaration = ue_py_new_ffield_class_capsule(first_field_class);
		call_args = PyTuple_New(first_metadata ? 3 : 2);
	}
	else if (PyList_Check(value) && PyList_Size(value) == 1 &&
		UEPyResolvePropertyDeclaration(PyList_GetItem(value, 0), first_field_class, first_metadata))
	{
		declaration = PyList_New(1);
		PyList_SET_ITEM(declaration, 0, ue_py_new_ffield_class_capsule(first_field_class));
		call_args = PyTuple_New(first_metadata ? 3 : 2);
	}
	else if (PyAnySet_Check(value) && PySet_Size(value) == 1)
	{
		PyObject* iterator = PyObject_GetIter(value);
		PyObject* item = iterator ? PyIter_Next(iterator) : nullptr;
		Py_XDECREF(iterator);
		if (!item)
		{
			return PyErr_Occurred() ? -1 : 0;
		}

		const bool resolved = UEPyResolvePropertyDeclaration(item, first_field_class, first_metadata);
		Py_DECREF(item);
		if (!resolved)
		{
			return 0;
		}

		declaration = PySet_New(nullptr);
		PyObject* capsule = ue_py_new_ffield_class_capsule(first_field_class);
		if (!declaration || !capsule || PySet_Add(declaration, capsule) < 0)
		{
			Py_XDECREF(capsule);
			Py_XDECREF(declaration);
			return -1;
		}
		Py_DECREF(capsule);
		call_args = PyTuple_New(first_metadata ? 3 : 2);
	}
	else if (PyDict_Check(value) && PyDict_Size(value) == 1)
	{
		PyObject* map_key = nullptr;
		PyObject* map_value = nullptr;
		Py_ssize_t position = 0;
		PyDict_Next(value, &position, &map_key, &map_value);
		if (!UEPyResolvePropertyDeclaration(map_key, first_field_class, first_metadata) ||
			!UEPyResolvePropertyDeclaration(map_value, second_field_class, second_metadata))
		{
			return 0;
		}

		declaration = PyList_New(2);
		PyList_SET_ITEM(declaration, 0, ue_py_new_ffield_class_capsule(first_field_class));
		PyList_SET_ITEM(declaration, 1, ue_py_new_ffield_class_capsule(second_field_class));
		call_args = PyTuple_New(4);
	}
	else
	{
		return 0;
	}

	if (!declaration || !call_args)
	{
		Py_XDECREF(declaration);
		Py_XDECREF(call_args);
		return -1;
	}

	PyTuple_SET_ITEM(call_args, 0, declaration);
	PyTuple_SET_ITEM(call_args, 1, PyUnicode_FromString(name));
	if (PyTuple_Size(call_args) >= 3)
	{
		PyObject* metadata = first_metadata ? first_metadata : Py_None;
		Py_INCREF(metadata);
		PyTuple_SET_ITEM(call_args, 2, metadata);
	}
	if (PyTuple_Size(call_args) == 4)
	{
		PyObject* metadata = second_metadata ? second_metadata : Py_None;
		Py_INCREF(metadata);
		PyTuple_SET_ITEM(call_args, 3, metadata);
	}

	PyObject* result = py_ue_add_property(self, call_args);
	Py_DECREF(call_args);
	if (!result)
	{
		return -1;
	}
	Py_DECREF(result);
	return 1;
}

int unreal_engine_py_init(ue_PyUObject *self, PyObject *args, PyObject *kwds)
{

	// is it subclassing ?
	if (PyTuple_Size(args) == 3)
	{
		// TODO make it smarter on error checking
		for (Py_ssize_t index = 0; index < 3; ++index)
		{
			PyObject* description = PyObject_Str(PyTuple_GetItem(args, index));
			if (!description)
			{
				return -1;
			}
			UE_LOG(LogPython, Warning, TEXT("%s"), UTF8_TO_TCHAR(UEPyUnicode_AsUTF8(description)));
			Py_DECREF(description);
		}

		PyObject *parents = PyTuple_GetItem(args, 1);
		ue_PyUObject *parent = (ue_PyUObject *)PyTuple_GetItem(parents, 0);

		PyObject *class_attributes = PyTuple_GetItem(args, 2);

		PyObject *class_name = PyDict_GetItemString(class_attributes, (char *)"__qualname__");
		const char *name = UEPyUnicode_AsUTF8(class_name);
		// check if parent is a uclass
		UClass *new_class = unreal_engine_new_uclass((char *)name, (UClass *)parent->ue_object);
		if (!new_class)
			return -1;

		// map the class to the python object
		self->ue_object = new_class;
		self->py_proxy = nullptr;
		self->auto_rooted = 0;
		self->py_dict = PyDict_New();

		FUnrealEnginePythonHouseKeeper::Get()->RegisterPyUObject(new_class, self);

		PyObject *py_additional_properties = PyDict_New();
		if (!py_additional_properties)
		{
			return -1;
		}

		PyObject *class_attributes_keys = PyObject_GetIter(class_attributes);
		if (!class_attributes_keys)
		{
			Py_DECREF(py_additional_properties);
			return -1;
		}
		for (;;)
		{
			PyObject *key = PyIter_Next(class_attributes_keys);
			if (!key)
			{
				if (PyErr_Occurred())
				{
					Py_DECREF(class_attributes_keys);
					Py_DECREF(py_additional_properties);
					return -1;
				}
				break;
			}
			if (!PyUnicodeOrString_Check(key))
			{
				Py_DECREF(key);
				continue;
			}
			const char *class_key = UEPyUnicode_AsUTF8(key);

			PyObject *value = PyDict_GetItem(class_attributes, key);

			if (strlen(class_key) > 2 && class_key[0] == '_' && class_key[1] == '_')
			{
				Py_DECREF(key);
				continue;
			}

			bool prop_added = false;

			if (FProperty *u_property = new_class->FindPropertyByName(FName(UTF8_TO_TCHAR(class_key))))
			{
				UE_LOG(LogPython, Warning, TEXT("Found FProperty %s"), UTF8_TO_TCHAR(class_key));
				PyDict_SetItem(py_additional_properties, key, value);
				prop_added = true;
			}
			else
			{
				const int add_property_result = UEPyTryAddClassProperty(self, class_key, value);
				if (add_property_result < 0)
				{
					unreal_engine_py_log_error();
					Py_DECREF(key);
					Py_DECREF(class_attributes_keys);
					Py_DECREF(py_additional_properties);
					return -1;
				}
				prop_added = add_property_result > 0;
			}

			// function ?
			if (!prop_added && PyCallable_Check(value) && class_key[0] >= 'A' && class_key[0] <= 'Z')
			{
				uint32 func_flags = FUNC_Native | FUNC_BlueprintCallable | FUNC_Public;
				if (UEPyApplyCallableFlag(value, "event", FUNC_Event | FUNC_BlueprintEvent, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "multicast", FUNC_NetMulticast, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "server", FUNC_NetServer, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "client", FUNC_NetClient, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "reliable", FUNC_NetReliable, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "pure", FUNC_BlueprintPure, func_flags) < 0 ||
					UEPyApplyCallableFlag(value, "static", FUNC_Static, func_flags) < 0)
				{
					unreal_engine_py_log_error();
					Py_DECREF(key);
					Py_DECREF(class_attributes_keys);
					Py_DECREF(py_additional_properties);
					return -1;
				}

				PyObject *override_name = PyObject_GetAttrString(value, (char *)"override");
				if (override_name && PyUnicodeOrString_Check(override_name))
				{
					class_key = UEPyUnicode_AsUTF8(override_name);
				}
				else if (!override_name)
				{
					PyErr_Clear();
				}
				const FString function_name = UTF8_TO_TCHAR(class_key);
				const bool function_added = unreal_engine_add_function(new_class, (char *)class_key, value, func_flags) != nullptr;
				Py_XDECREF(override_name);
				if (!function_added)
				{
					UE_LOG(LogPython, Error, TEXT("unable to add function %s"), *function_name);
					Py_DECREF(key);
					Py_DECREF(class_attributes_keys);
					Py_DECREF(py_additional_properties);
					return -1;
				}
				prop_added = true;
			}


			if (!prop_added)
			{
				UE_LOG(LogPython, Warning, TEXT("Adding %s as attr"), UTF8_TO_TCHAR(class_key));
				if (PyObject_SetAttr((PyObject *)self, key, value) < 0)
				{
					Py_DECREF(key);
					Py_DECREF(class_attributes_keys);
					Py_DECREF(py_additional_properties);
					return -1;
				}
			}
			Py_DECREF(key);
		}
		Py_DECREF(class_attributes_keys);

		if (PyDict_Size(py_additional_properties) > 0)
		{
			if (PyObject_SetAttrString((PyObject *)self, (char*)"__additional_uproperties__", py_additional_properties) < 0)
			{
				Py_DECREF(py_additional_properties);
				return -1;
			}
		}
		Py_DECREF(py_additional_properties);

		UPythonClass *new_u_py_class = (UPythonClass *)new_class;
		// TODO: check if we can use this to decref the ue_PyUbject mapped to the class
		new_u_py_class->py_uobject = self;
		new_u_py_class->ClassConstructor = [](const FObjectInitializer &ObjectInitializer)
		{
			FScopePythonGIL gil;
			UClass *u_class = ue_py_class_constructor_placeholder ? ue_py_class_constructor_placeholder : ObjectInitializer.GetClass();
			ue_py_class_constructor_placeholder = nullptr;

			UEPyClassConstructor(u_class->GetSuperClass(), ObjectInitializer);

			if (UPythonClass *u_py_class_casted = Cast<UPythonClass>(u_class))
			{
				ue_PyUObject *new_self = ue_get_python_uobject(ObjectInitializer.GetObj());
				if (!new_self)
				{
					unreal_engine_py_log_error();
					return;
				}

				// fill __dict__ from class
				if (u_py_class_casted->py_uobject && u_py_class_casted->py_uobject->py_dict)
				{
					PyObject *found_additional_props = PyDict_GetItemString(u_py_class_casted->py_uobject->py_dict, (char *)"__additional_uproperties__");
					// manage UProperties (and automatically maps multicast properties)
					if (found_additional_props)
					{
						PyObject *keys = PyDict_Keys(found_additional_props);
						Py_ssize_t items_len = PyList_Size(keys);
						for (Py_ssize_t i = 0; i < items_len; i++)
						{
							PyObject *mc_key = PyList_GetItem(keys, i);
							PyObject *mc_value = PyDict_GetItem(found_additional_props, mc_key);

							const char *mc_name = UEPyUnicode_AsUTF8(mc_key);
							FProperty *u_property = ObjectInitializer.GetObj()->GetClass()->FindPropertyByName(FName(UTF8_TO_TCHAR(mc_name)));
							if (u_property)
							{
								if (auto casted_prop = CastField<FMulticastDelegateProperty>(u_property))
								{
#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 23
									FMulticastScriptDelegate multiscript_delegate = *casted_prop->GetMulticastDelegate(ObjectInitializer.GetObj());
#else
									
									FMulticastScriptDelegate multiscript_delegate = casted_prop->GetPropertyValue_InContainer(ObjectInitializer.GetObj());
#endif

									FScriptDelegate script_delegate;
									UPythonDelegate *py_delegate = FUnrealEnginePythonHouseKeeper::Get()->NewDelegate(ObjectInitializer.GetObj(), mc_value, casted_prop->SignatureFunction);
									// fake UFUNCTION for bypassing checks
									script_delegate.BindUFunction(py_delegate, FName("PyFakeCallable"));

									// add the new delegate
									multiscript_delegate.Add(script_delegate);

									// re-assign multicast delegate
#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 23
									casted_prop->SetMulticastDelegate(ObjectInitializer.GetObj(), multiscript_delegate);
#else
									casted_prop->SetPropertyValue_InContainer(ObjectInitializer.GetObj(), multiscript_delegate);
#endif
								}
								else
								{
									PyObject_SetAttr((PyObject *)new_self, mc_key, mc_value);
								}
							}

						}
						Py_DECREF(keys);
					}
					else
					{
						PyErr_Clear();
					}
					PyObject *keys = PyDict_Keys(u_py_class_casted->py_uobject->py_dict);
					Py_ssize_t keys_len = PyList_Size(keys);
					for (Py_ssize_t i = 0; i < keys_len; i++)
					{
						PyObject *key = PyList_GetItem(keys, i);
						PyObject *value = PyDict_GetItem(u_py_class_casted->py_uobject->py_dict, key);
						if (PyUnicodeOrString_Check(key))
						{
							const char *key_name = UEPyUnicode_AsUTF8(key);
							if (!strcmp(key_name, (char *)"__additional_uproperties__"))
								continue;
						}
						// special case to bound function to method
						if (PyFunction_Check(value))
						{
							PyObject *bound_function = PyObject_CallMethod(value, (char*)"__get__", (char*)"O", (PyObject *)new_self);
							if (bound_function)
							{
								PyObject_SetAttr((PyObject *)new_self, key, bound_function);
								Py_DECREF(bound_function);
							}
							else
							{
								unreal_engine_py_log_error();
							}
						}
						else
						{
							PyObject_SetAttr((PyObject *)new_self, key, value);
						}
					}
					Py_DECREF(keys);
				}
				// call __init__
				u_py_class_casted->CallPyConstructor(new_self);
			}
		};

		// Install the Python constructor before creating the CDO. The class
		// constructor above copies Python attributes and calls __init__ for both
		// the CDO and every later instance.
		PyObject* py_init = PyDict_GetItemString(class_attributes, (char*)"__init__");
		if (py_init && PyCallable_Check(py_init))
		{
			new_u_py_class->SetPyConstructor(py_init);
		}

		new_u_py_class->Bind();
		new_u_py_class->StaticLink(true);
		new_u_py_class->AssembleReferenceTokenStream();
		if (!new_u_py_class->GetDefaultObject())
		{
			PyErr_Format(PyExc_RuntimeError, "unable to create the CDO for dynamic class %s", name);
			return -1;
		}

#if WITH_EDITOR
		new_u_py_class->PostEditChange();
		new_u_py_class->PostLinkerChange();
		if (GEditor)
		{
			FBlueprintActionDatabase::Get().RefreshClassActions(new_u_py_class);
		}
#endif
	}

	return 0;
}

#else

int unreal_engine_py_init(ue_PyUObject* self, PyObject* args, PyObject* kwds)
{
	PyErr_SetString(
		PyExc_TypeError,
		"UE5.8 UObject wrappers cannot be instantiated or subclassed directly yet");
	return -1;
}

#endif
