// Copyright 20Tab S.r.l.

#pragma once

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdelete-non-virtual-dtor"
#endif


//#define UEPY_MEMORY_DEBUG	1

#include "CoreMinimal.h"
#include "Runtime/Core/Public/Modules/ModuleManager.h"
#include "Runtime/SlateCore/Public/Styling/SlateStyle.h"
#include "UObject/ScriptMacros.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_PYTHON
THIRD_PARTY_INCLUDES_START
PRAGMA_DISABLE_REGISTER_WARNINGS
#include <Python.h>
#include <structmember.h>
PRAGMA_ENABLE_REGISTER_WARNINGS
THIRD_PARTY_INCLUDES_END
#else
#error UnrealEnginePython requires Target.bCompilePython and the UE Python3 SDK
#endif

typedef struct
{
	PyObject_HEAD
	/* Type-specific fields go here. */
	UObject *ue_object;
	// reference to proxy class (can be null)
	PyObject *py_proxy;
	// the __dict__
	PyObject *py_dict;
	// if true RemoveFromRoot will be called at object destruction time
	int auto_rooted;
	// if owned the life of the UObject is related to the life of PyObject
	int owned;
} ue_PyUObject;

UNREALENGINEPYTHON_API void ue_py_register_magic_module(char *name, PyObject *(*)());
UNREALENGINEPYTHON_API PyObject *ue_py_register_module(const char *);

#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 18
#define FStringAssetReference FSoftObjectPath
#endif

#define ue_py_check(py_u) if (!FUnrealEnginePythonHouseKeeper::Get()->IsValidPyUObject(py_u))\
	return PyErr_Format(PyExc_Exception, "PyUObject is in invalid state")

#define ue_py_check_int(py_u) if (!FUnrealEnginePythonHouseKeeper::Get()->IsValidPyUObject(py_u))\
	{\
		PyErr_SetString(PyExc_Exception, "PyUObject is in invalid state");\
		return -1;\
	}


const char *UEPyUnicode_AsUTF8(PyObject *py_str);

#if PY_MAJOR_VERSION < 3
int PyGILState_Check();
#endif
bool PyUnicodeOrString_Check(PyObject *py_obj);

UNREALENGINEPYTHON_API void unreal_engine_py_log_error();
UNREALENGINEPYTHON_API ue_PyUObject *ue_get_python_uobject(UObject *);
UNREALENGINEPYTHON_API ue_PyUObject *ue_get_python_uobject_inc(UObject *);

#define Py_RETURN_UOBJECT(py_uobj) ue_PyUObject *ret = ue_get_python_uobject_inc(py_uobj);\
	if (!ret)\
		return PyErr_Format(PyExc_Exception, "uobject is in invalid state");\
	return (PyObject *)ret;

#define Py_RETURN_UOBJECT_NOINC(py_uobj) ue_PyUObject *ret = ue_get_python_uobject(py_uobj);\
	if (!ret)\
		return PyErr_Format(PyExc_Exception, "uobject is in invalid state");\
	return (PyObject *)ret;

#if UEP_LEGACY_ENGINE_MINOR_VERSION < 16
template<class CPPSTRUCT>
struct TStructOpsTypeTraitsBase2 : TStructOpsTypeTraitsBase
{

};
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogPython, Log, All);


class UNREALENGINEPYTHON_API FUnrealEnginePythonModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void InitializePython();
	bool IsPythonInitialized() const { return bPythonInitialized; }

	void RunString(char *);
	void RunFile(char *);

#if PLATFORM_MAC
	void RunStringInMainThread(char *);
	void RunFileInMainThread(char *);
#endif

	void UESetupPythonInterpreter(bool);

	TArray<FString> ScriptsPaths;
	FString ZipPath;
	FString AdditionalModulesPath;

	bool BrutalFinalize;
	bool OwnsPythonInterpreter;

	// pep8ize a string using various strategy (currently only autopep8 is supported)
	FString Pep8ize(FString Code);

private:
	PyThreadState *PythonMainThreadState;
	FDelegateHandle AllModuleLoadingPhasesCompleteHandle;
	bool bPythonInitialized;
	// used by console
	void *main_dict;
	void *local_dict;
	void *main_module;

	TSharedPtr<FSlateStyleSet> StyleSet;
};

struct FScopePythonGIL
{

	PyGILState_STATE state;

	FScopePythonGIL()
	{
		state = PyGILState_Ensure();
	}

	~FScopePythonGIL()
	{
		PyGILState_Release(state);
	}
};




