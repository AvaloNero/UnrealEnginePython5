#include "UEPyObject.h"

#include "PythonDelegate.h"
#include "PythonFunction.h"
#include "Components/ActorComponent.h"
#include "Engine/UserDefinedEnum.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorReimportHandler.h"
#include "ObjectTools.h"
#include "Runtime/Core/Public/HAL/FeedbackContextAnsi.h"

#include "Wrappers/UEPyFObjectThumbnail.h"
#endif

#include "Runtime/Core/Public/Misc/OutputDeviceNull.h"
#include "Runtime/CoreUObject/Public/Serialization/ObjectWriter.h"
#include "Runtime/CoreUObject/Public/Serialization/ObjectReader.h"
#include "UObject/SavePackage.h"

PyObject *py_ue_get_class(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	Py_RETURN_UOBJECT(self->ue_object->GetClass());
}

PyObject *py_ue_class_generated_by(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
		return PyErr_Format(PyExc_Exception, "uobject is a not a UClass");

#if WITH_EDITORONLY_DATA
	UObject *u_object = u_class->ClassGeneratedBy;
	if (!u_object)
		Py_RETURN_NONE;

	Py_RETURN_UOBJECT(u_object);
#else
	Py_RETURN_NONE;
#endif
}

PyObject *py_ue_class_get_flags(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
		return PyErr_Format(PyExc_Exception, "uobject is a not a UClass");

	return PyLong_FromUnsignedLongLong((uint64)u_class->GetClassFlags());
}

PyObject *py_ue_class_set_flags(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	uint64 flags;
	if (!PyArg_ParseTuple(args, "K:class_set_flags", &flags))
	{
		return nullptr;
	}

	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
		return PyErr_Format(PyExc_Exception, "uobject is a not a UClass");

	u_class->ClassFlags = (EClassFlags)flags;

	Py_RETURN_NONE;
}

PyObject *py_ue_get_obj_flags(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	return PyLong_FromUnsignedLongLong((uint64)self->ue_object->GetFlags());
}

PyObject *py_ue_set_obj_flags(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	uint64 flags;
	PyObject *py_reset = nullptr;
	if (!PyArg_ParseTuple(args, "K|O:set_obj_flags", &flags, &py_reset))
	{
		return nullptr;
	}

	if (py_reset && PyObject_IsTrue(py_reset))
	{
		self->ue_object->ClearFlags(self->ue_object->GetFlags());
	}

	self->ue_object->SetFlags((EObjectFlags)flags);

	Py_RETURN_NONE;
}

PyObject *py_ue_clear_obj_flags(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	uint64 flags;
	if (!PyArg_ParseTuple(args, "K:clear_obj_flags", &flags))
	{
		return nullptr;
	}

	self->ue_object->ClearFlags((EObjectFlags)flags);

	Py_RETURN_NONE;
}

PyObject *py_ue_reset_obj_flags(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	self->ue_object->ClearFlags(self->ue_object->GetFlags());

	Py_RETURN_NONE;
}

#if WITH_EDITOR
PyObject *py_ue_class_set_config_name(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *config_name;
	if (!PyArg_ParseTuple(args, "s:class_set_config_name", &config_name))
	{
		return nullptr;
	}

	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
		return PyErr_Format(PyExc_Exception, "uobject is a not a UClass");

	u_class->ClassConfigName = UTF8_TO_TCHAR(config_name);

	Py_RETURN_NONE;
}

PyObject *py_ue_class_get_config_name(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);


	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
		return PyErr_Format(PyExc_Exception, "uobject is a not a UClass");

	return PyUnicode_FromString(TCHAR_TO_UTF8(*u_class->ClassConfigName.ToString()));
}
#endif

PyObject *py_ue_get_property_struct(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:get_property_struct", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	FStructProperty *prop = CastField<FStructProperty>(u_property);
	if (!prop)
		return PyErr_Format(PyExc_Exception, "object is not a StructProperty");
	return py_ue_new_uscriptstruct(prop->Struct, prop->ContainerPtrToValuePtr<uint8>(self->ue_object));
}

PyObject *py_ue_get_super_class(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UClass *u_class = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_class = (UClass *)self->ue_object;
	}
	else
	{
		u_class = self->ue_object->GetClass();
	}

	Py_RETURN_UOBJECT(u_class->GetSuperClass());
}

PyObject *py_ue_get_outer(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	UObject *outer = self->ue_object->GetOuter();
	if (!outer)
		Py_RETURN_NONE;

	Py_RETURN_UOBJECT(outer);
}

PyObject *py_ue_get_outermost(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	UObject *outermost = self->ue_object->GetOutermost();
	if (!outermost)
		Py_RETURN_NONE;

	Py_RETURN_UOBJECT(outermost);
}

PyObject *py_ue_conditional_begin_destroy(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	self->ue_object->ConditionalBeginDestroy();
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject *py_ue_is_valid(ue_PyUObject * self, PyObject * args)
{
	if (!::IsValid(self->ue_object))
	{
		Py_RETURN_FALSE;
	}

	Py_RETURN_TRUE;
}

PyObject *py_ue_is_a(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	PyObject *obj;
	if (!PyArg_ParseTuple(args, "O:is_a", &obj))
	{
		return NULL;
	}

	if (!ue_is_pyuobject(obj))
	{
		return PyErr_Format(PyExc_Exception, "argument is not a UObject");
	}

	ue_PyUObject *py_obj = (ue_PyUObject *)obj;

	if (self->ue_object->IsA((UClass *)py_obj->ue_object))
	{
		Py_RETURN_TRUE;
	}


	Py_RETURN_FALSE;
}

PyObject *py_ue_is_child_of(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	PyObject *obj;
	if (!PyArg_ParseTuple(args, "O:is_child_of", &obj))
	{
		return NULL;
	}

	if (!self->ue_object->IsA<UClass>())
		return PyErr_Format(PyExc_Exception, "object is not a UClass");

	if (!ue_is_pyuobject(obj))
	{
		return PyErr_Format(PyExc_Exception, "argument is not a UObject");
	}

	ue_PyUObject *py_obj = (ue_PyUObject *)obj;

	if (!py_obj->ue_object->IsA<UClass>())
		return PyErr_Format(PyExc_Exception, "argument is not a UClass");

	UClass *parent = (UClass *)py_obj->ue_object;
	UClass *child = (UClass *)self->ue_object;

	if (child->IsChildOf(parent))
	{
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyObject *py_ue_post_edit_change(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);

#if WITH_EDITOR
	Py_BEGIN_ALLOW_THREADS;
	self->ue_object->PostEditChange();
	Py_END_ALLOW_THREADS;
#endif
	Py_RETURN_NONE;
}

PyObject *py_ue_post_edit_change_property(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);

	char *prop_name = nullptr;
	int change_type = (int)EPropertyChangeType::Unspecified;

	if (!PyArg_ParseTuple(args, "s|i:post_edit_change_property", &prop_name, &change_type))
	{
		return nullptr;
	}


	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *prop = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(prop_name)));
	if (!prop)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", prop_name);

#if WITH_EDITOR
	Py_BEGIN_ALLOW_THREADS;
	FPropertyChangedEvent changed(prop, change_type);
	self->ue_object->PostEditChangeProperty(changed);
	Py_END_ALLOW_THREADS;
#endif
	Py_RETURN_NONE;
}

PyObject *py_ue_modify(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);

#if WITH_EDITOR
	Py_BEGIN_ALLOW_THREADS;
	self->ue_object->Modify();
	Py_END_ALLOW_THREADS;
#endif
	Py_RETURN_NONE;
}

PyObject *py_ue_pre_edit_change(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);

	FProperty *prop = nullptr;
	char *prop_name = nullptr;

	if (!PyArg_ParseTuple(args, "|s:pre_edit_change", &prop_name))
	{
		return nullptr;
	}

	if (prop_name)
	{
		UStruct *u_struct = nullptr;

		if (self->ue_object->IsA<UStruct>())
		{
			u_struct = (UStruct *)self->ue_object;
		}
		else
		{
			u_struct = (UStruct *)self->ue_object->GetClass();
		}

		prop = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(prop_name)));
		if (!prop)
			return PyErr_Format(PyExc_Exception, "unable to find property %s", prop_name);
	}

#if WITH_EDITOR
	Py_BEGIN_ALLOW_THREADS;
	self->ue_object->PreEditChange(prop);
	Py_END_ALLOW_THREADS;
#endif
	Py_RETURN_NONE;
}


#if WITH_EDITOR
PyObject *py_ue_set_metadata(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_key;
	char *metadata_value;
	if (!PyArg_ParseTuple(args, "ss:set_metadata", &metadata_key, &metadata_value))
	{
		return NULL;
	}

	if (self->ue_object->IsA<UClass>())
	{
		UClass *u_class = (UClass *)self->ue_object;
		u_class->SetMetaData(FName(UTF8_TO_TCHAR(metadata_key)), UTF8_TO_TCHAR(metadata_value));
	}
	else if (self->ue_object->IsA<UField>())
	{
		UField *u_field = (UField *)self->ue_object;
		u_field->SetMetaData(FName(UTF8_TO_TCHAR(metadata_key)), UTF8_TO_TCHAR(metadata_value));
	}
	else
	{
		return PyErr_Format(PyExc_TypeError, "the object does not support MetaData");
	}

	Py_RETURN_NONE;
}

PyObject *py_ue_get_metadata(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_key;
	if (!PyArg_ParseTuple(args, "s:get_metadata", &metadata_key))
	{
		return NULL;
	}

	char *metadata_value = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		UClass *u_class = (UClass *)self->ue_object;
		FString value = u_class->GetMetaData(FName(UTF8_TO_TCHAR(metadata_key)));
		return PyUnicode_FromString(TCHAR_TO_UTF8(*value));
	}

	if (self->ue_object->IsA<UField>())
	{
		UField *u_field = (UField *)self->ue_object;
		FString value = u_field->GetMetaData(FName(UTF8_TO_TCHAR(metadata_key)));
		return PyUnicode_FromString(TCHAR_TO_UTF8(*value));
	}

	return PyErr_Format(PyExc_TypeError, "the object does not support MetaData");
}

PyObject *py_ue_get_metadata_tag(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_tag_key;
	if (!PyArg_ParseTuple(args, "s:get_metadata_tag", &metadata_tag_key))
	{
		return nullptr;
	}

	const FString& Value = self->ue_object->GetOutermost()->GetMetaData().GetValue(self->ue_object, UTF8_TO_TCHAR(metadata_tag_key));
	return PyUnicode_FromString(TCHAR_TO_UTF8(*Value));
}

PyObject *py_ue_has_metadata_tag(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_tag_key;
	if (!PyArg_ParseTuple(args, "s:has_metadata_tag", &metadata_tag_key))
	{
		return nullptr;
	}

	if (self->ue_object->GetOutermost()->GetMetaData().HasValue(self->ue_object, UTF8_TO_TCHAR(metadata_tag_key)))
	{
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

PyObject *py_ue_remove_metadata_tag(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_tag_key;
	if (!PyArg_ParseTuple(args, "s:remove_metadata_tag", &metadata_tag_key))
	{
		return nullptr;
	}

	self->ue_object->GetOutermost()->GetMetaData().RemoveValue(self->ue_object, UTF8_TO_TCHAR(metadata_tag_key));
	Py_RETURN_NONE;
}

PyObject *py_ue_set_metadata_tag(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_tag_key;
	char *metadata_tag_value;
	if (!PyArg_ParseTuple(args, "ss:set_metadata_tag", &metadata_tag_key, &metadata_tag_value))
	{
		return nullptr;
	}

	self->ue_object->GetOutermost()->GetMetaData().SetValue(self->ue_object, UTF8_TO_TCHAR(metadata_tag_key), UTF8_TO_TCHAR(metadata_tag_value));
	Py_RETURN_NONE;
}


PyObject *py_ue_metadata_tags(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	TMap<FName, FString> *TagsMap = self->ue_object->GetOutermost()->GetMetaData().GetMapForObject(self->ue_object);
	if (!TagsMap)
		Py_RETURN_NONE;

	PyObject* py_list = PyList_New(0);
	for (TPair< FName, FString>& Pair : *TagsMap)
	{
		PyList_Append(py_list, PyUnicode_FromString(TCHAR_TO_UTF8(*Pair.Key.ToString())));
	}
	return py_list;
}

PyObject *py_ue_has_metadata(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *metadata_key;
	if (!PyArg_ParseTuple(args, "s:has_metadata", &metadata_key))
	{
		return NULL;
	}

	if (self->ue_object->IsA<UClass>())
	{
		UClass *u_class = (UClass *)self->ue_object;
		if (u_class->HasMetaData(FName(UTF8_TO_TCHAR(metadata_key))))
		{
			Py_RETURN_TRUE;
		}
		Py_RETURN_FALSE;
	}

	if (self->ue_object->IsA<UField>())
	{
		UField *u_field = (UField *)self->ue_object;
		if (u_field->HasMetaData(FName(UTF8_TO_TCHAR(metadata_key))))
		{
			Py_INCREF(Py_True);
			return Py_True;
		}
		Py_INCREF(Py_False);
		return Py_False;
	}

	return PyErr_Format(PyExc_TypeError, "the object does not support MetaData");
}
#endif

PyObject *py_ue_call_function(ue_PyUObject * self, PyObject * args, PyObject *kwargs)
{

	ue_py_check(self);

	UFunction *function = nullptr;

	if (PyTuple_Size(args) < 1)
	{
		return PyErr_Format(PyExc_TypeError, "this function requires at least an argument");
	}

	PyObject *func_id = PyTuple_GetItem(args, 0);

	if (PyUnicodeOrString_Check(func_id))
	{
		function = self->ue_object->FindFunction(FName(UTF8_TO_TCHAR(UEPyUnicode_AsUTF8(func_id))));
	}

	if (!function)
		return PyErr_Format(PyExc_Exception, "unable to find function");

	return py_ue_ufunction_call(function, self->ue_object, args, 1, kwargs);

}

PyObject *py_ue_find_function(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *name;
	if (!PyArg_ParseTuple(args, "s:find_function", &name))
	{
		return NULL;
	}

	UFunction *function = self->ue_object->FindFunction(FName(UTF8_TO_TCHAR(name)));
	if (!function)
	{
		Py_RETURN_NONE;
	}

	Py_RETURN_UOBJECT((UObject *)function);
}

#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 15
PyObject *py_ue_can_modify(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

#if WITH_EDITOR
	if (self->ue_object->CanModify())
	{
		Py_RETURN_TRUE;
	}
#endif

	Py_RETURN_FALSE;
}
#endif

PyObject *py_ue_set_name(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *name;
	if (!PyArg_ParseTuple(args, "s:set_name", &name))
	{
		return NULL;
	}

	if (!self->ue_object->Rename(UTF8_TO_TCHAR(name), self->ue_object->GetOutermost(), REN_Test))
	{
		return PyErr_Format(PyExc_Exception, "cannot set name %s", name);
	}

	if (self->ue_object->Rename(UTF8_TO_TCHAR(name)))
	{
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyObject *py_ue_set_outer(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	PyObject *py_outer;
	if (!PyArg_ParseTuple(args, "O:set_outer", &py_outer))
	{
		return nullptr;
	}

	UPackage *package = ue_py_check_type<UPackage>(py_outer);
	if (!package)
		return PyErr_Format(PyExc_Exception, "argument is not a UPackage");

	if (!self->ue_object->Rename(nullptr, package, REN_Test))
	{
		return PyErr_Format(PyExc_Exception, "cannot move to package %s", TCHAR_TO_UTF8(*package->GetPathName()));
	}

	if (self->ue_object->Rename(nullptr, package))
	{
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyObject *py_ue_get_name(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);


	return PyUnicode_FromString(TCHAR_TO_UTF8(*(self->ue_object->GetName())));
}

PyObject *py_ue_get_display_name(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

#if WITH_EDITOR
	if (UClass *uclass = ue_py_check_type<UClass>(self))
	{
		return PyUnicode_FromString(TCHAR_TO_UTF8(*uclass->GetDisplayNameText().ToString()));
	}

	if (AActor *actor = ue_py_check_type<AActor>(self))
	{
		return PyUnicode_FromString(TCHAR_TO_UTF8(*actor->GetActorLabel()));
	}
#endif

	if (UActorComponent *component = ue_py_check_type<UActorComponent>(self))
	{
		return PyUnicode_FromString(TCHAR_TO_UTF8(*component->GetReadableName()));
	}

	return PyUnicode_FromString(TCHAR_TO_UTF8(*(self->ue_object->GetName())));
}


PyObject *py_ue_get_full_name(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);


	return PyUnicode_FromString(TCHAR_TO_UTF8(*(self->ue_object->GetFullName())));
}

PyObject *py_ue_get_path_name(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	return PyUnicode_FromString(TCHAR_TO_UTF8(*(self->ue_object->GetPathName())));
}

PyObject *py_ue_save_config(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	Py_BEGIN_ALLOW_THREADS;
	self->ue_object->SaveConfig();
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

PyObject *py_ue_set_property(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	PyObject *property_value;
	int index = 0;
	if (!PyArg_ParseTuple(args, "sO|i:set_property", &property_name, &property_value, &index))
	{
		return nullptr;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);


	if (!ue_py_convert_pyobject(property_value, u_property, (uint8 *)self->ue_object, index))
	{
		return PyErr_Format(PyExc_Exception, "unable to set property %s", property_name);
	}

	Py_RETURN_NONE;

}

PyObject *py_ue_set_property_flags(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	uint64 flags;
	if (!PyArg_ParseTuple(args, "sK:set_property_flags", &property_name, &flags))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

#if UEP_LEGACY_ENGINE_MINOR_VERSION < 20
	u_property->SetPropertyFlags(flags);
#else
	u_property->SetPropertyFlags((EPropertyFlags)flags);
#endif
	Py_RETURN_NONE;
}

PyObject *py_ue_add_property_flags(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	uint64 flags;
	if (!PyArg_ParseTuple(args, "sK:add_property_flags", &property_name, &flags))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);


#if UEP_LEGACY_ENGINE_MINOR_VERSION < 20
	u_property->SetPropertyFlags(u_property->GetPropertyFlags() | flags);
#else
	u_property->SetPropertyFlags(u_property->GetPropertyFlags() | (EPropertyFlags)flags);
#endif
	Py_RETURN_NONE;
}

PyObject *py_ue_get_property_flags(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:get_property_flags", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	return PyLong_FromUnsignedLong(u_property->GetPropertyFlags());
}

PyObject *py_ue_enum_values(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);
	if (!self->ue_object->IsA<UEnum>())
		return PyErr_Format(PyExc_TypeError, "uobject is not a UEnum");

	UEnum *u_enum = (UEnum *)self->ue_object;
	uint8 max_enum_value = u_enum->GetMaxEnumValue();
	PyObject *ret = PyList_New(0);
	for (uint8 i = 0; i < max_enum_value; i++)
	{
		PyObject *py_long = PyLong_FromLong(i);
		PyList_Append(ret, py_long);
		Py_DECREF(py_long);
	}
	return ret;
}

PyObject *py_ue_enum_names(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);
	if (!self->ue_object->IsA<UEnum>())
		return PyErr_Format(PyExc_TypeError, "uobject is not a UEnum");

	UEnum *u_enum = (UEnum *)self->ue_object;
	uint8 max_enum_value = u_enum->GetMaxEnumValue();
	PyObject *ret = PyList_New(0);
	for (uint8 i = 0; i < max_enum_value; i++)
	{
#if UEP_LEGACY_ENGINE_MINOR_VERSION > 15
		PyObject *py_long = PyUnicode_FromString(TCHAR_TO_UTF8(*u_enum->GetNameStringByIndex(i)));
#else
		PyObject *py_long = PyUnicode_FromString(TCHAR_TO_UTF8(*u_enum->GetEnumName(i)));
#endif
		PyList_Append(ret, py_long);
		Py_DECREF(py_long);
	}
	return ret;
}

#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 15
PyObject *py_ue_enum_user_defined_names(ue_PyUObject *self, PyObject * args)
{
	ue_py_check(self);
	if (!self->ue_object->IsA<UUserDefinedEnum>())
		return PyErr_Format(PyExc_TypeError, "uobject is not a UEnum");

	UUserDefinedEnum *u_enum = (UUserDefinedEnum *)self->ue_object;
	TArray<FText> user_defined_names;
	u_enum->DisplayNameMap.GenerateValueArray(user_defined_names);
	PyObject *ret = PyList_New(0);
	for (FText text : user_defined_names)
	{
		PyObject *py_long = PyUnicode_FromString(TCHAR_TO_UTF8(*text.ToString()));
		PyList_Append(ret, py_long);
		Py_DECREF(py_long);
	}
	return ret;
}
#endif

PyObject *py_ue_properties(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	PyObject *ret = PyList_New(0);

	for (TFieldIterator<FProperty> PropIt(u_struct); PropIt; ++PropIt)
	{
		FProperty* property = *PropIt;
		PyObject *property_name = PyUnicode_FromString(TCHAR_TO_UTF8(*property->GetName()));
		PyList_Append(ret, property_name);
		Py_DECREF(property_name);
	}

	return ret;
}

PyObject *py_ue_functions(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	PyObject *ret = PyList_New(0);

	for (TFieldIterator<UFunction> FuncIt(u_struct); FuncIt; ++FuncIt)
	{
		UFunction* func = *FuncIt;
		PyObject *func_name = PyUnicode_FromString(TCHAR_TO_UTF8(*func->GetFName().ToString()));
		PyList_Append(ret, func_name);
		Py_DECREF(func_name);
	}

	return ret;

}

PyObject *py_ue_call(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *call_args;
	if (!PyArg_ParseTuple(args, "s:call", &call_args))
	{
		return nullptr;
	}

	FOutputDeviceNull od_null;
	bool success = false;
	Py_BEGIN_ALLOW_THREADS;
	success = self->ue_object->CallFunctionByNameWithArguments(UTF8_TO_TCHAR(call_args), od_null, NULL, true);
	Py_END_ALLOW_THREADS;
	if (!success)
	{
		return PyErr_Format(PyExc_Exception, "error while calling \"%s\"", call_args);
	}

	Py_RETURN_NONE;
}

PyObject *py_ue_broadcast(ue_PyUObject *self, PyObject *args)
{

	ue_py_check(self);

	Py_ssize_t args_len = PyTuple_Size(args);
	if (args_len < 1)
	{
		return PyErr_Format(PyExc_Exception, "you need to specify the event to trigger");
	}

	PyObject *py_property_name = PyTuple_GetItem(args, 0);
	if (!PyUnicodeOrString_Check(py_property_name))
	{
		return PyErr_Format(PyExc_Exception, "event name must be a unicode string");
	}

	const char *property_name = UEPyUnicode_AsUTF8(py_property_name);

	FProperty *u_property = self->ue_object->GetClass()->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find event property %s", property_name);

	if (auto casted_prop = CastField<FMulticastDelegateProperty>(u_property))
	{
#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 23
		FMulticastScriptDelegate multiscript_delegate = *casted_prop->GetMulticastDelegate(self->ue_object);
#else
		FMulticastScriptDelegate multiscript_delegate = casted_prop->GetPropertyValue_InContainer(self->ue_object);
#endif
		uint8 *parms = (uint8 *)FMemory_Alloca(casted_prop->SignatureFunction->PropertiesSize);
		FMemory::Memzero(parms, casted_prop->SignatureFunction->PropertiesSize);

		uint32 argn = 1;

		// initialize args
		for (TFieldIterator<FProperty> IArgs(casted_prop->SignatureFunction); IArgs && IArgs->HasAnyPropertyFlags(CPF_Parm); ++IArgs)
		{
			FProperty *prop = *IArgs;
			if (!prop->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				prop->InitializeValue_InContainer(parms);
			}

			if ((IArgs->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm)
			{
				if (!prop->IsInContainer(casted_prop->SignatureFunction->ParmsSize))
				{
					return PyErr_Format(PyExc_Exception, "Attempting to import func param property that's out of bounds. %s", TCHAR_TO_UTF8(*casted_prop->SignatureFunction->GetName()));
				}

				PyObject *py_arg = PyTuple_GetItem(args, argn);
				if (!py_arg)
				{
					PyErr_Clear();
#if WITH_EDITOR
					FString default_key = FString("CPP_Default_") + prop->GetName();
					FString default_key_value = casted_prop->SignatureFunction->GetMetaData(FName(*default_key));
					if (!default_key_value.IsEmpty())
					{
						prop->ImportText_Direct(
							*default_key_value,
							prop->ContainerPtrToValuePtr<uint8>(parms),
							nullptr,
							PPF_None);
					}
#endif
				}
				else if (!ue_py_convert_pyobject(py_arg, prop, parms, 0))
				{
					return PyErr_Format(PyExc_TypeError, "unable to convert pyobject to property %s (%s)", TCHAR_TO_UTF8(*prop->GetName()), TCHAR_TO_UTF8(*prop->GetClass()->GetName()));
				}


			}

			argn++;

		}

		Py_BEGIN_ALLOW_THREADS;
		multiscript_delegate.ProcessDelegate<UObject>(parms);
		Py_END_ALLOW_THREADS;
	}
	else
	{
		return PyErr_Format(PyExc_Exception, "property is not a FMulticastDelegateProperty");
	}

	Py_RETURN_NONE;
}

PyObject *py_ue_get_property(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	int index = 0;
	if (!PyArg_ParseTuple(args, "s|i:get_property", &property_name, &index))
	{
		return nullptr;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	return ue_py_convert_property(u_property, (uint8 *)self->ue_object, index);
}

PyObject *py_ue_get_property_array_dim(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:get_property_array_dim", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	return PyLong_FromLongLong(u_property->ArrayDim);
}

#if WITH_EDITOR
PyObject *py_ue_get_thumbnail(ue_PyUObject *self, PyObject * args)
{

	PyObject *py_generate = nullptr;
	if (!PyArg_ParseTuple(args, "|O:get_thumbnail", &py_generate))
	{
		return nullptr;
	}

	ue_py_check(self);

	TArray<FName> names;
	names.Add(FName(*self->ue_object->GetFullName()));

	FThumbnailMap thumb_map;

	FObjectThumbnail *object_thumbnail = nullptr;

	if (!ThumbnailTools::ConditionallyLoadThumbnailsForObjects(names, thumb_map))
	{
		if (py_generate && PyObject_IsTrue(py_generate))
		{
			object_thumbnail = ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(self->ue_object);
		}
	}
	else
	{
		object_thumbnail = &thumb_map[names[0]];
	}

	if (!object_thumbnail)
	{
		return PyErr_Format(PyExc_Exception, "unable to retrieve thumbnail");
	}

	return py_ue_new_fobject_thumbnail(*object_thumbnail);
}

PyObject *py_ue_render_thumbnail(ue_PyUObject *self, PyObject * args)
{

	int width = ThumbnailTools::DefaultThumbnailSize;
	int height = ThumbnailTools::DefaultThumbnailSize;
	PyObject *no_flush = nullptr;
	if (!PyArg_ParseTuple(args, "|iiO:render_thumbnail", &width, height, &no_flush))
	{
		return nullptr;
	}

	ue_py_check(self);
	FObjectThumbnail object_thumbnail;
	ThumbnailTools::RenderThumbnail(self->ue_object, width, height,
		(no_flush && PyObject_IsTrue(no_flush)) ? ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush : ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
		nullptr, &object_thumbnail);

	return py_ue_new_fobject_thumbnail(object_thumbnail);
}
#endif

PyObject *py_ue_get_uproperty(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:get_uproperty", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	return ue_py_new_fproperty_capsule(u_property);

}

PyObject *py_ue_get_inner(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	FArrayProperty *u_property = nullptr;
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "object is not a FArrayProperty");

	FProperty* inner = u_property->Inner;
	if (!inner)
		Py_RETURN_NONE;

	return ue_py_new_fproperty_capsule(inner);
}

PyObject *py_ue_get_key_prop(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	FMapProperty *u_property = nullptr;
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "object is not a FMapProperty");

	FProperty* key = u_property->KeyProp;
	if (!key)
		Py_RETURN_NONE;

	return ue_py_new_fproperty_capsule(key);
}

PyObject *py_ue_get_value_prop(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	FMapProperty *u_property = nullptr;
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "object is not a FMapProperty");

	FProperty* value = u_property->ValueProp;
	if (!value)
		Py_RETURN_NONE;

	return ue_py_new_fproperty_capsule(value);
}

PyObject *py_ue_has_property(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:has_property", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		Py_RETURN_FALSE;
	Py_RETURN_TRUE;
}

PyObject *py_ue_get_property_class(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	char *property_name;
	if (!PyArg_ParseTuple(args, "s:get_property_class", &property_name))
	{
		return NULL;
	}

	UStruct *u_struct = nullptr;

	if (self->ue_object->IsA<UClass>())
	{
		u_struct = (UStruct *)self->ue_object;
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	FProperty *u_property = u_struct->FindPropertyByName(FName(UTF8_TO_TCHAR(property_name)));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", property_name);

	return PyUnicode_FromString(TCHAR_TO_UTF8(*u_property->GetClass()->GetName()));

}

PyObject *py_ue_is_rooted(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	if (self->ue_object->IsRooted())
	{
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}


PyObject *py_ue_add_to_root(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	self->ue_object->AddToRoot();

	Py_RETURN_NONE;
}

PyObject *py_ue_own(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	if (self->owned)
	{
		return PyErr_Format(PyExc_Exception, "uobject already owned");
	}

	Py_DECREF(self);

	self->owned = 1;
	FUnrealEnginePythonHouseKeeper::Get()->TrackUObject(self->ue_object);

	Py_RETURN_NONE;
}

PyObject *py_ue_disown(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	if (!self->owned)
	{
		return PyErr_Format(PyExc_Exception, "uobject not owned");
	}

	Py_INCREF(self);

	self->owned = 0;
	FUnrealEnginePythonHouseKeeper::Get()->UntrackUObject(self->ue_object);

	Py_RETURN_NONE;
}

PyObject *py_ue_is_owned(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	if (!self->owned)
	{
		Py_RETURN_FALSE;
	}

	Py_RETURN_TRUE;
}

PyObject *py_ue_auto_root(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	self->ue_object->AddToRoot();
	self->auto_rooted = 1;

	Py_RETURN_NONE;
}

PyObject *py_ue_remove_from_root(ue_PyUObject *self, PyObject * args)
{

	ue_py_check(self);

	self->ue_object->RemoveFromRoot();

	Py_INCREF(Py_None);
	return Py_None;
}

PyObject *py_ue_bind_event(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *event_name;
	PyObject *py_callable;
	if (!PyArg_ParseTuple(args, "sO:bind_event", &event_name, &py_callable))
	{
		return NULL;
	}

	if (!PyCallable_Check(py_callable))
	{
		return PyErr_Format(PyExc_Exception, "object is not callable");
	}

	return ue_bind_pyevent(self, FString(event_name), py_callable, true);
}

PyObject *py_ue_unbind_event(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	char *event_name;
	PyObject *py_callable;
	if (!PyArg_ParseTuple(args, "sO:bind_event", &event_name, &py_callable))
	{
		return NULL;
	}

	if (!PyCallable_Check(py_callable))
	{
		return PyErr_Format(PyExc_Exception, "object is not callable");
	}

	return ue_unbind_pyevent(self, FString(event_name), py_callable, true);
}

PyObject *py_ue_delegate_bind_ufunction(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	char *delegate_name;
	PyObject *py_obj;
	char *fname;

	if (!PyArg_ParseTuple(args, "sOs:delegate_bind_ufunction", &delegate_name, &py_obj, &fname))
		return nullptr;

	FProperty *u_property = self->ue_object->GetClass()->FindPropertyByName(FName(delegate_name));
	if (!u_property)
		return PyErr_Format(PyExc_Exception, "unable to find property %s", delegate_name);

	FDelegateProperty *Prop = CastField<FDelegateProperty>(u_property);
	if (!Prop)
		return PyErr_Format(PyExc_Exception, "property is not a FDelegateProperty");

	UObject *Object = ue_py_check_type<UObject>(py_obj);
	if (!Object)
		return PyErr_Format(PyExc_Exception, "argument is not a UObject");

	FScriptDelegate script_delegate;
	script_delegate.BindUFunction(Object, FName(fname));

	// re-assign multicast delegate
	Prop->SetPropertyValue_InContainer(self->ue_object, script_delegate);

	Py_RETURN_NONE;
}

#if PY_MAJOR_VERSION >= 3
PyObject *py_ue_add_function(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *name;
	PyObject *py_callable;
	if (!PyArg_ParseTuple(args, "sO:add_function", &name, &py_callable))
	{
		return NULL;
	}

	if (!self->ue_object->IsA<UClass>())
	{
		return PyErr_Format(PyExc_Exception, "uobject is not a UClass");
	}

	UClass *u_class = (UClass *)self->ue_object;

	if (!PyCallable_Check(py_callable))
	{
		return PyErr_Format(PyExc_Exception, "object is not callable");
	}

	if (!unreal_engine_add_function(u_class, name, py_callable, FUNC_Native | FUNC_BlueprintCallable | FUNC_Public))
	{
		return PyErr_Format(PyExc_Exception, "unable to add function");
	}

	Py_INCREF(Py_None);
	return Py_None;
}
#endif

#if UEP_WITH_DYNAMIC_CLASS_GENERATION
static bool ue_py_resolve_dynamic_property_type(
	PyObject* py_type,
	UClass*& object_class,
	UScriptStruct*& script_struct,
	UEnum*& enum_type)
{
	object_class = nullptr;
	script_struct = nullptr;
	enum_type = nullptr;

	if (!py_type || py_type == Py_None)
	{
		return true;
	}

	ue_PyUObject* py_uobject = ue_is_pyuobject(py_type);
	if (!py_uobject)
	{
		PyErr_SetString(PyExc_TypeError, "property type metadata is not a UObject");
		return false;
	}

	object_class = Cast<UClass>(py_uobject->ue_object);
	if (!object_class)
	{
		script_struct = Cast<UScriptStruct>(py_uobject->ue_object);
	}
	if (!object_class && !script_struct)
	{
		enum_type = Cast<UEnum>(py_uobject->ue_object);
	}
	if (!object_class && !script_struct && !enum_type)
	{
		PyErr_SetString(PyExc_TypeError, "property type metadata must be a UClass, UScriptStruct, or UEnum");
		return false;
	}

	return true;
}

static FProperty* ue_py_construct_dynamic_property(
	FFieldClass* field_class,
	const FFieldVariant& owner,
	const FName& name)
{
	if (!field_class || !field_class->IsChildOf(FProperty::StaticClass()))
	{
		return nullptr;
	}
	return CastField<FProperty>(field_class->Construct(owner, name));
}

static void ue_py_configure_dynamic_property(
	FProperty* property,
	UClass* object_class,
	UScriptStruct* script_struct,
	UEnum* enum_type)
{
	if (FClassProperty* class_property = CastField<FClassProperty>(property))
	{
		class_property->PropertyClass = UClass::StaticClass();
		class_property->SetMetaClass(object_class ? object_class : UObject::StaticClass());
	}
	else if (FObjectPropertyBase* object_property = CastField<FObjectPropertyBase>(property))
	{
		object_property->SetPropertyClass(object_class ? object_class : UObject::StaticClass());
	}
	else if (FStructProperty* struct_property = CastField<FStructProperty>(property))
	{
		struct_property->Struct = script_struct;
	}
	else if (FEnumProperty* enum_property = CastField<FEnumProperty>(property))
	{
		enum_property->SetEnum(enum_type);
		enum_property->AddCppProperty(new FByteProperty(enum_property, TEXT("UnderlyingType")));
	}
	else if (FByteProperty* byte_property = CastField<FByteProperty>(property))
	{
		byte_property->Enum = enum_type;
	}
}

PyObject *py_ue_add_property(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	PyObject* obj = nullptr;
	char* name = nullptr;
	PyObject* property_class = nullptr;
	PyObject* property_class2 = nullptr;
	if (!PyArg_ParseTuple(args, "Os|OO:add_property", &obj, &name, &property_class, &property_class2))
	{
		return nullptr;
	}

	UStruct* owner_struct = Cast<UStruct>(self->ue_object);
	if (!owner_struct)
	{
		return PyErr_Format(PyExc_Exception, "uobject is not a UStruct");
	}

	if (UClass* owner_class = Cast<UClass>(owner_struct))
	{
		if (owner_class->GetDefaultObject(false))
		{
			return PyErr_Format(
				PyExc_RuntimeError,
				"properties can only be added while a dynamic class is being constructed");
		}
	}

	UClass* object_class = nullptr;
	UScriptStruct* script_struct = nullptr;
	UEnum* enum_type = nullptr;
	UClass* object_class2 = nullptr;
	UScriptStruct* script_struct2 = nullptr;
	UEnum* enum_type2 = nullptr;
	if (!ue_py_resolve_dynamic_property_type(property_class, object_class, script_struct, enum_type) ||
		!ue_py_resolve_dynamic_property_type(property_class2, object_class2, script_struct2, enum_type2))
	{
		return nullptr;
	}

	FProperty* property = nullptr;
	FProperty* inner_property = nullptr;
	FProperty* second_inner_property = nullptr;
	FFieldClass* field_class = nullptr;
	FFieldClass* second_field_class = nullptr;

	if (PyList_Check(obj))
	{
		const Py_ssize_t item_count = PyList_Size(obj);
		if (item_count != 1 && item_count != 2)
		{
			return PyErr_Format(PyExc_TypeError, "property lists must contain one array type or two map types");
		}

		field_class = ue_py_get_ffield_class_from_capsule(PyList_GetItem(obj, 0));
		if (!field_class)
		{
			return PyErr_Format(PyExc_TypeError, "container element is not an FProperty class");
		}

		if (item_count == 1)
		{
			FArrayProperty* array_property = new FArrayProperty(owner_struct, FName(UTF8_TO_TCHAR(name)));
			inner_property = ue_py_construct_dynamic_property(field_class, array_property, TEXT("Inner"));
			if (!inner_property)
			{
				delete array_property;
				return PyErr_Format(PyExc_RuntimeError, "unable to construct array inner property");
			}
			array_property->Inner = inner_property;
			property = array_property;
		}
		else
		{
			second_field_class = ue_py_get_ffield_class_from_capsule(PyList_GetItem(obj, 1));
			if (!second_field_class)
			{
				return PyErr_Format(PyExc_TypeError, "map value is not an FProperty class");
			}

			FMapProperty* map_property = new FMapProperty(owner_struct, FName(UTF8_TO_TCHAR(name)));
			inner_property = ue_py_construct_dynamic_property(field_class, map_property, TEXT("Key"));
			second_inner_property = ue_py_construct_dynamic_property(second_field_class, map_property, TEXT("Value"));
			if (!inner_property || !second_inner_property)
			{
				delete map_property;
				return PyErr_Format(PyExc_RuntimeError, "unable to construct map key/value properties");
			}
			map_property->KeyProp = inner_property;
			map_property->ValueProp = second_inner_property;
			property = map_property;
		}
	}
	else
	{
		field_class = ue_py_get_ffield_class_from_capsule(obj);
		if (!field_class)
		{
			return PyErr_Format(PyExc_TypeError, "argument is not an FProperty class");
		}
		if (field_class == FArrayProperty::StaticClass() || field_class == FMapProperty::StaticClass())
		{
			return PyErr_Format(PyExc_TypeError, "use a list to declare array or map properties");
		}
		property = ue_py_construct_dynamic_property(field_class, owner_struct, FName(UTF8_TO_TCHAR(name)));
	}

	if (!property)
	{
		return PyErr_Format(PyExc_RuntimeError, "unable to construct FProperty");
	}

	if (inner_property)
	{
		inner_property->SetPropertyFlags(CPF_ZeroConstructor | CPF_HasGetValueTypeHash);
		ue_py_configure_dynamic_property(inner_property, object_class, script_struct, enum_type);
	}
	if (second_inner_property)
	{
		second_inner_property->SetPropertyFlags(CPF_ZeroConstructor | CPF_HasGetValueTypeHash);
		ue_py_configure_dynamic_property(second_inner_property, object_class2, script_struct2, enum_type2);
	}
	if (!inner_property)
	{
		ue_py_configure_dynamic_property(property, object_class, script_struct, enum_type);
	}

	EPropertyFlags flags = CPF_Edit | CPF_BlueprintVisible | CPF_ZeroConstructor;
	if (object_class && object_class->IsChildOf<UActorComponent>())
	{
		flags &= ~CPF_Edit;
	}
	if (FMulticastDelegateProperty* multicast_property = CastField<FMulticastDelegateProperty>(property))
	{
		multicast_property->SignatureFunction = NewObject<UFunction>(
			owner_struct,
			FName(*FString::Printf(TEXT("%s__DelegateSignature"), UTF8_TO_TCHAR(name))),
			RF_Public | RF_Transient);
		multicast_property->SignatureFunction->FunctionFlags = FUNC_MulticastDelegate | FUNC_BlueprintCallable | FUNC_Native;
		flags |= CPF_BlueprintAssignable | CPF_BlueprintCallable;
		flags &= ~CPF_Edit;
	}
	else if (FDelegateProperty* delegate_property = CastField<FDelegateProperty>(property))
	{
		delegate_property->SignatureFunction = NewObject<UFunction>(
			owner_struct,
			FName(*FString::Printf(TEXT("%s__DelegateSignature"), UTF8_TO_TCHAR(name))),
			RF_Public | RF_Transient);
		delegate_property->SignatureFunction->FunctionFlags = FUNC_Delegate | FUNC_BlueprintCallable | FUNC_Native;
	}

	property->SetPropertyFlags(flags);
	property->ArrayDim = 1;
	owner_struct->AddCppProperty(property);

	return ue_py_new_fproperty_capsule(property);
}
#else
PyObject* py_ue_add_property(ue_PyUObject* self, PyObject* args)
{
	return PyErr_Format(
		PyExc_NotImplementedError,
		"dynamic FProperty generation is not available in the UE5.8 core port");
}
#endif

PyObject *py_ue_as_dict(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UStruct *u_struct = nullptr;
	UObject *u_object = self->ue_object;

	if (self->ue_object->IsA<UStruct>())
	{
		u_struct = (UStruct *)self->ue_object;
		if (self->ue_object->IsA<UClass>())
		{
			UClass *u_class = (UClass *)self->ue_object;
			u_object = u_class->GetDefaultObject();
		}
	}
	else
	{
		u_struct = (UStruct *)self->ue_object->GetClass();
	}

	PyObject *py_struct_dict = PyDict_New();
	TFieldIterator<FProperty> SArgs(u_struct);
	for (; SArgs; ++SArgs)
	{
		PyObject *struct_value = ue_py_convert_property(*SArgs, (uint8 *)u_object, 0);
		if (!struct_value)
		{
			Py_DECREF(py_struct_dict);
			return NULL;
		}
		PyDict_SetItemString(py_struct_dict, TCHAR_TO_UTF8(*SArgs->GetName()), struct_value);
	}
	return py_struct_dict;
}

PyObject *py_ue_get_cdo(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UClass *u_class = ue_py_check_type<UClass>(self);
	if (!u_class)
	{
		return PyErr_Format(PyExc_Exception, "uobject is not a UClass");
	}

	Py_RETURN_UOBJECT(u_class->GetDefaultObject());
}

PyObject *py_ue_get_archetype(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	UObject *Archetype = self->ue_object->GetArchetype();

	if (!Archetype)
		return PyErr_Format(PyExc_Exception, "uobject has no archetype");

	Py_RETURN_UOBJECT(Archetype);
}

PyObject *py_ue_get_archetype_instances(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	TArray<UObject *> Instances;

	self->ue_object->GetArchetypeInstances(Instances);

	PyObject *py_list = PyList_New(0);

	for (UObject *Instance : Instances)
	{
		PyList_Append(py_list, (PyObject *)ue_get_python_uobject(Instance));
	}

	return py_list;
}


#if WITH_EDITOR
PyObject *py_ue_save_package(ue_PyUObject * self, PyObject * args)
{

	/*

		Here we have the following cases to manage:

		calling on a UObject without an outer
		calling on a UObject with an outer
		calling on a UObject with an outer and a name arg

	*/

	ue_py_check(self);

	char *name = nullptr;
	if (!PyArg_ParseTuple(args, "|s:save_package", &name))
	{
		return NULL;
	}

	UObject *u_object = self->ue_object;
	UPackage *package = u_object->GetOutermost();
	if (package == GetTransientPackage())
		package = nullptr;
	const bool has_package = package != nullptr;

	if (!package && !name)
		return PyErr_Format(PyExc_Exception, "the object has no associated package, please specify a name");

	if (name)
	{
		const FString requested_package_name(UTF8_TO_TCHAR(name));
		FText invalid_package_reason;
		if (!FPackageName::IsValidLongPackageName(requested_package_name, true, &invalid_package_reason))
		{
			return PyErr_Format(
				PyExc_ValueError,
				"invalid long package name '%s': %s",
				name,
				TCHAR_TO_UTF8(*invalid_package_reason.ToString()));
		}

		if (!has_package)
		{
			// unmark transient object
			if (u_object->HasAnyFlags(RF_Transient))
			{
				u_object->ClearFlags(RF_Transient);
			}
		}

		UPackage *target_package = CreatePackage(*requested_package_name);
		if (!target_package)
			return PyErr_Format(PyExc_Exception, "unable to create package");

		if (has_package && target_package != package)
		{
			if (u_object == package)
				return PyErr_Format(PyExc_Exception, "saving a package itself under a new name is unsupported");

			const FString asset_name = FPackageName::GetLongPackageAssetName(requested_package_name);
			u_object = DuplicateObject(self->ue_object, target_package, FName(*asset_name));
			if (!u_object)
				return PyErr_Format(PyExc_Exception, "unable to duplicate object into package");
		}
		else if (!has_package)
		{
			// move to object into the new package
			if (!self->ue_object->Rename(*(self->ue_object->GetName()), target_package, REN_Test))
			{
				return PyErr_Format(PyExc_Exception, "unable to set object outer to package");
			}
			if (!self->ue_object->Rename(*(self->ue_object->GetName()), target_package))
			{
				return PyErr_Format(PyExc_Exception, "unable to set object outer to package");
			}
		}

		package = target_package;
	}

	// ensure the right flags are applied
	if (u_object != package)
		u_object->SetFlags(RF_Public | RF_Standalone);

	package->FullyLoad();
	package->MarkPackageDirty();

	const bool bIsMap = u_object->IsA<UWorld>() || package->ContainsMap();
	FString filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		package->GetName(),
		filename,
		bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
	{
		return PyErr_Format(PyExc_Exception, "unable to resolve filename for package %s", TCHAR_TO_UTF8(*package->GetName()));
	}

	FSavePackageArgs save_args;
	save_args.TopLevelFlags = RF_Standalone;
	save_args.Error = GWarn;
	UObject *asset_to_save = u_object == package ? nullptr : u_object;
	if (UPackage::SavePackage(package, asset_to_save, *filename, save_args))
	{
		if (asset_to_save)
			FAssetRegistryModule::AssetCreated(asset_to_save);
		Py_RETURN_UOBJECT(u_object);
	}

	return PyErr_Format(PyExc_Exception, "unable to save package");
}

PyObject *py_ue_import_custom_properties(ue_PyUObject * self, PyObject * args)
{
	ue_py_check(self);

	char *t3d;

	if (!PyArg_ParseTuple(args, "s:import_custom_properties", &t3d))
	{
		return nullptr;
	}

	FFeedbackContextAnsi context;

	Py_BEGIN_ALLOW_THREADS;
	self->ue_object->ImportCustomProperties(UTF8_TO_TCHAR(t3d), &context);
	Py_END_ALLOW_THREADS;

	TArray<FString> errors;
	context.GetErrors(errors);

	if (errors.Num() > 0)
	{
		return PyErr_Format(PyExc_Exception, "%s", TCHAR_TO_UTF8(*errors[0]));
	}

	Py_RETURN_NONE;
}

PyObject *py_ue_asset_can_reimport(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	if (FReimportManager::Instance()->CanReimport(self->ue_object))
	{
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

PyObject *py_ue_asset_reimport(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	PyObject *py_ask_for_new_file = nullptr;
	PyObject *py_show_notification = nullptr;
	char *filename = nullptr;
	if (!PyArg_ParseTuple(args, "|OOs:asset_reimport", &py_ask_for_new_file, &py_show_notification, &filename))
	{
		return NULL;
	}

	bool ask_for_new_file = false;
	bool show_notification = false;
	FString f_filename = FString("");

	if (py_ask_for_new_file && PyObject_IsTrue(py_ask_for_new_file))
		ask_for_new_file = true;

	if (py_show_notification && PyObject_IsTrue(py_show_notification))
		show_notification = true;

	if (filename)
		f_filename = FString(UTF8_TO_TCHAR(filename));

	if (FReimportManager::Instance()->Reimport(self->ue_object, ask_for_new_file, show_notification, f_filename))
	{
		Py_RETURN_TRUE;
	}

	Py_RETURN_FALSE;
}

PyObject *py_ue_duplicate(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	char *package_name;
	char *object_name;
	PyObject *py_overwrite = nullptr;

	if (!PyArg_ParseTuple(args, "ss|O:duplicate", &package_name, &object_name, &py_overwrite))
		return nullptr;

	ObjectTools::FPackageGroupName pgn;
	pgn.ObjectName = UTF8_TO_TCHAR(object_name);
	pgn.GroupName = FString("");
	pgn.PackageName = UTF8_TO_TCHAR(package_name);

	TSet<UPackage *> refused;

	UObject *new_asset = nullptr;

	Py_BEGIN_ALLOW_THREADS;
#if UEP_LEGACY_ENGINE_MINOR_VERSION < 14
	new_asset = ObjectTools::DuplicateSingleObject(self->ue_object, pgn, refused);
#else
	new_asset = ObjectTools::DuplicateSingleObject(self->ue_object, pgn, refused, (py_overwrite && PyObject_IsTrue(py_overwrite)));
#endif
	Py_END_ALLOW_THREADS;

	if (!new_asset)
		return PyErr_Format(PyExc_Exception, "unable to duplicate object");

	Py_RETURN_UOBJECT(new_asset);
}
#endif


PyObject *py_ue_to_bytes(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	TArray<uint8> Bytes;

	FObjectWriter(self->ue_object, Bytes);

	return PyBytes_FromStringAndSize((const char *)Bytes.GetData(), Bytes.Num());
}

PyObject *py_ue_to_bytearray(ue_PyUObject * self, PyObject * args)
{

	ue_py_check(self);

	TArray<uint8> Bytes;

	FObjectWriter(self->ue_object, Bytes);

	return PyByteArray_FromStringAndSize((const char *)Bytes.GetData(), Bytes.Num());
}

PyObject *py_ue_from_bytes(ue_PyUObject * self, PyObject * args)
{

	Py_buffer py_buf;

	if (!PyArg_ParseTuple(args, "z*:from_bytes", &py_buf))
		return nullptr;

	ue_py_check(self);

	TArray<uint8> Bytes;
	Bytes.AddUninitialized(py_buf.len);
	FMemory::Memcpy(Bytes.GetData(), py_buf.buf, py_buf.len);

	FObjectReader(self->ue_object, Bytes);

	Py_RETURN_NONE;
}
