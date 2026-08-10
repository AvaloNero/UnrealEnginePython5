#pragma once

#include "UEPyModule.h"

#include "Runtime/Core/Public/Misc/OutputDevice.h"

class FPythonOutputDevice : FOutputDevice
{

public:
	FPythonOutputDevice()
		: py_serialize(nullptr)
	{
		GLog->AddOutputDevice(this);
	}

	~FPythonOutputDevice()
	{
		if (GLog)
		{
			GLog->RemoveOutputDevice(this);
		}
		if (Py_IsInitialized())
		{
			FScopePythonGIL gil;
			Py_XDECREF(py_serialize);
		}
		py_serialize = nullptr;
	}

	void SetPySerialize(PyObject *py_callable)
	{
		py_serialize = py_callable;
		Py_INCREF(py_serialize);
	}

protected:
	virtual void Serialize(const TCHAR * V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		if (!py_serialize)
			return;
		PyObject *ret = PyObject_CallFunction(py_serialize, (char *)"sis", TCHAR_TO_UTF8(V), Verbosity, TCHAR_TO_UTF8(*Category.ToString()));
		if (!ret)
		{
			unreal_engine_py_log_error();
		}
		Py_XDECREF(ret);
	}

private:
	PyObject * py_serialize;
};

typedef struct
{
	PyObject_HEAD
		/* Type-specific fields go here. */
		FPythonOutputDevice *device;
} ue_PyFPythonOutputDevice;

void ue_python_init_fpython_output_device(PyObject *);
