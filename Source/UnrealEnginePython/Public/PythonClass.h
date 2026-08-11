#pragma once

#include "UnrealEnginePython.h"
#include "PythonClass.generated.h"

void unreal_engine_py_log_error();

UCLASS()
class UPythonClass : public UClass
{
	GENERATED_BODY()

public:
	~UPythonClass()
	{
		if (py_constructor && Py_IsInitialized())
		{
			FScopePythonGIL gil;
			Py_CLEAR(py_constructor);
		}
		else
		{
			py_constructor = nullptr;
		}
	}

	void SetPyConstructor(PyObject *callable)
	{
		Py_XINCREF(callable);
		Py_XDECREF(py_constructor);
		py_constructor = callable;
	}

	void CallPyConstructor(ue_PyUObject *self)
	{
		if (!py_constructor)
			return;
		PyObject *args = PyTuple_Pack(1, self);
		if (!args)
		{
			unreal_engine_py_log_error();
			return;
		}
		PyObject *ret = PyObject_CallObject(py_constructor, args);
		Py_DECREF(args);
		if (!ret)
		{
			unreal_engine_py_log_error();
			return;
		}
		Py_DECREF(ret);
	}

	// __dict__ is stored here
	ue_PyUObject *py_uobject = nullptr;

private:

	PyObject *py_constructor = nullptr;
};

