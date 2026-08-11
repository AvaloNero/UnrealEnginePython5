
#include "PythonDelegate.h"
#include "InputActionValue.h"
#include "UEPyModule.h"
#include "UEPyCallable.h"
#include "Wrappers/UEPyFVector2D.h"

UPythonDelegate::UPythonDelegate()
{
	py_callable = nullptr;
	signature_set = false;
}

void UPythonDelegate::SetPyCallable(PyObject *callable)
{
	// do not acquire the gil here as we set the callable in python call themselves
	py_callable = callable;
	Py_INCREF(py_callable);
}

void UPythonDelegate::SetSignature(UFunction *original_signature)
{
	signature = original_signature;
	signature_set = true;
}

void UPythonDelegate::ProcessEvent(UFunction *function, void *Parms)
{

	if (!py_callable)
		return;

	FScopePythonGIL gil;

	PyObject *py_args = nullptr;

	if (signature_set)
	{
		py_args = PyTuple_New(signature->NumParms);
		Py_ssize_t argn = 0;

		TFieldIterator<FProperty> PArgs(signature);
		for (; PArgs && argn < signature->NumParms && ((PArgs->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm); ++PArgs)
		{
			FProperty *prop = *PArgs;
			PyObject *arg = ue_py_convert_property(prop, (uint8 *)Parms, 0);
			if (!arg)
			{
				unreal_engine_py_log_error();
				Py_DECREF(py_args);
				return;
			}
			PyTuple_SetItem(py_args, argn, arg);
			argn++;
		}
	}

	PyObject *ret = PyObject_CallObject(py_callable, py_args);
	Py_XDECREF(py_args);
	if (!ret)
	{
		unreal_engine_py_log_error();
		return;
	}
	// currently useless as events do not return a value
	/*
	if (signature_set) {
		FProperty *return_property = signature->GetReturnProperty();
		if (return_property && signature->ReturnValueOffset != MAX_uint16) {
			if (!ue_py_convert_pyobject(ret, return_property, (uint8 *)Parms)) {
				UE_LOG(LogPython, Error, TEXT("Invalid return value type for delegate"));
			}
		}
	}
	*/
	Py_DECREF(ret);
}

void UPythonDelegate::PyFakeCallable()
{
}

void UPythonDelegate::PyInputHandler()
{
	FScopePythonGIL gil;
	PyObject *ret = PyObject_CallObject(py_callable, NULL);
	if (!ret)
	{
		unreal_engine_py_log_error();
		return;
	}
	Py_DECREF(ret);
}

void UPythonDelegate::PyInputAxisHandler(float value)
{
	FScopePythonGIL gil;
	PyObject *ret = PyObject_CallFunction(py_callable, (char *)"f", value);
	if (!ret)
	{
		unreal_engine_py_log_error();
		return;
	}
	Py_DECREF(ret);
}

void UPythonDelegate::PyEnhancedInputActionHandler(const FInputActionValue& value)
{
	if (!py_callable)
	{
		return;
	}

	FScopePythonGIL gil;
	PyObject* py_value = nullptr;
	switch (value.GetValueType())
	{
	case EInputActionValueType::Boolean:
		py_value = PyBool_FromLong(value.Get<bool>() ? 1 : 0);
		break;
	case EInputActionValueType::Axis1D:
		py_value = PyFloat_FromDouble(value.Get<float>());
		break;
	case EInputActionValueType::Axis2D:
		py_value = py_ue_new_fvector2d(value.Get<FVector2D>());
		break;
	case EInputActionValueType::Axis3D:
		py_value = py_ue_new_fvector(value.Get<FVector>());
		break;
	default:
		PyErr_SetString(PyExc_RuntimeError, "unsupported Enhanced Input action value type");
		unreal_engine_py_log_error();
		return;
	}

	PyObject* ret = PyObject_CallFunctionObjArgs(py_callable, py_value, nullptr);
	Py_DECREF(py_value);
	if (!ret)
	{
		unreal_engine_py_log_error();
		return;
	}
	Py_DECREF(ret);
}

bool UPythonDelegate::UsesPyCallable(PyObject *other)
{
    return py_callable == other;
}

UPythonDelegate::~UPythonDelegate()
{
	if (Py_IsInitialized())
	{
		FScopePythonGIL gil;
		Py_XDECREF(py_callable);
	}
	py_callable = nullptr;
#if defined(UEPY_MEMORY_DEBUG)
	UE_LOG(LogPython, Warning, TEXT("PythonDelegate %p callable XDECREF'ed"), this);
#endif
}
