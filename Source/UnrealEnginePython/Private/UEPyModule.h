#pragma once

#include "UnrealEnginePython.h"
#include "PythonDelegate.h"
#include "PythonSmartDelegate.h"
#include "UEPyUScriptStruct.h"
#include "PythonHouseKeeper.h"
#include "UObject/UObjectGlobals.h"

#ifndef UEP_WITH_DYNAMIC_CLASS_GENERATION
#define UEP_WITH_DYNAMIC_CLASS_GENERATION 0
#endif

// common wrappers
#include "Wrappers/UEPyFVector.h"
#include "Wrappers/UEPyFRotator.h"
#include "Wrappers/UEPyFQuat.h"
#include "Wrappers/UEPyFTransform.h"
#include "Wrappers/UEPyFColor.h"
#include "Wrappers/UEPyFLinearColor.h"

// backward compatibility for UE4.20 TCHAR_TO_WCHAR
#ifndef TCHAR_TO_WCHAR
	// SIZEOF_WCHAR_T is provided by pyconfig.h
	#if SIZEOF_WCHAR_T == (PLATFORM_TCHAR_IS_4_BYTES ? 4 : 2)
		#define TCHAR_TO_WCHAR(str) str
	#else
		#define TCHAR_TO_WCHAR(str) (wchar_t*)StringCast<wchar_t>(static_cast<const TCHAR*>(str)).Get()
	#endif
#endif


UWorld *ue_get_uworld(ue_PyUObject *);
AActor *ue_get_actor(ue_PyUObject *);
PyObject *ue_py_convert_property(FProperty *, uint8 *, int32, bool copy_structs = false);
bool ue_py_convert_pyobject(PyObject *, FProperty *, uint8 *, int32);
PyObject *ue_py_new_fproperty_capsule(FProperty *);
FProperty *ue_py_get_fproperty_from_capsule(PyObject *);
PyObject *ue_py_new_ffield_class_capsule(FFieldClass *);
FFieldClass *ue_py_get_ffield_class_from_capsule(PyObject *);
ue_PyUObject *ue_is_pyuobject(PyObject *);

template <typename T>
T *ue_py_find_first_object(const TCHAR *Name)
{
	return FindFirstObject<T>(Name, EFindFirstObjectOptions::NativeFirst);
}

void ue_bind_events_for_py_class_by_attribute(UObject *, PyObject *);

void ue_autobind_events_for_pyclass(ue_PyUObject *, PyObject *);
PyObject *ue_bind_pyevent(ue_PyUObject *, FString, PyObject *, bool);
PyObject *ue_unbind_pyevent(ue_PyUObject *, FString, PyObject *, bool);

PyObject *py_ue_ufunction_call(UFunction *, UObject *, PyObject *, int, PyObject *);

UClass *unreal_engine_new_uclass(char *, UClass *);
UFunction *unreal_engine_add_function(UClass *, char *, PyObject *, uint32);


template <typename T> T *ue_py_check_type(PyObject *py_obj)
{
	ue_PyUObject *ue_py_obj = ue_is_pyuobject(py_obj);
	if (!ue_py_obj)
	{
		return nullptr;
	}

	if (!::IsValid(ue_py_obj->ue_object))
	{
		UE_LOG(LogPython, Error, TEXT("invalid UObject in ue_PyUObject %p"), ue_py_obj);
		return nullptr;
	}

	return Cast<T>(ue_py_obj->ue_object);
}

template <typename T> T *ue_py_check_type(ue_PyUObject *py_obj)
{
	if (!::IsValid(py_obj->ue_object))
	{
		UE_LOG(LogPython, Error, TEXT("invalid UObject in ue_PyUObject %p"), py_obj);
		return nullptr;
	}
	return Cast<T>(py_obj->ue_object);
}

uint8 *do_ue_py_check_struct(PyObject *py_obj, UScriptStruct* chk_u_struct);

template <typename T> T *ue_py_check_struct(PyObject *py_obj)
{
	return (T*)do_ue_py_check_struct(py_obj, T::StaticStruct());
}

bool do_ue_py_check_childstruct(PyObject *py_obj, UScriptStruct* parent_u_struct);

template <typename T> bool ue_py_check_childstruct(PyObject *py_obj)
{
	return do_ue_py_check_childstruct(py_obj, T::StaticStruct());
}

FGuid *ue_py_check_fguid(PyObject *);

