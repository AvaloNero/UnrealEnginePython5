#pragma once

#include "UnrealEnginePython.h"

struct FPythonItem
{
	PyObject *py_object = nullptr;

	// Takes ownership of one Python reference (all callers pass the new
	// reference returned by PyIter_Next).
	explicit FPythonItem(PyObject *item)
		: py_object(item)
	{
	}

	~FPythonItem()
	{
		if (py_object && Py_IsInitialized())
		{
			FScopePythonGIL gil;
			Py_DECREF(py_object);
		}
		py_object = nullptr;
	}

	FPythonItem(const FPythonItem &) = delete;
	FPythonItem &operator=(const FPythonItem &) = delete;
};
