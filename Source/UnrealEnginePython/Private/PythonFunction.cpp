
#include "PythonFunction.h"
#include "UEPyModule.h"
#include "UEPyMixin.h"


void UPythonFunction::SetPyCallable(PyObject *callable)
{
	Py_XINCREF(callable);
	Py_XDECREF(py_callable);
	py_callable = callable;
}


#if UEP_LEGACY_ENGINE_MINOR_VERSION > 18
void UPythonFunction::CallPythonCallable(UObject *Context, FFrame& Stack, RESULT_DECL)
#else
void UPythonFunction::CallPythonCallable(FFrame& Stack, RESULT_DECL)
#endif
{

	FScopePythonGIL gil;

#if UEP_LEGACY_ENGINE_MINOR_VERSION <= 18
	UObject *Context = Stack.Object;
#endif

	UPythonFunction *function = static_cast<UPythonFunction *>(Stack.CurrentNativeFunction);
	if (!ue_py_prepare_mixin_call(function, Context))
	{
		unreal_engine_py_log_error();
		return;
	}

	bool on_error = false;
	bool is_static = function->HasAnyFunctionFlags(FUNC_Static);

	// count the number of arguments
	Py_ssize_t argn = (Context && !is_static) ? 1 : 0;
	TFieldIterator<FProperty> IArgs(function);
	for (; IArgs && ((IArgs->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm); ++IArgs) {
		argn++;
	}
#if defined(UEPY_MEMORY_DEBUG)
	UE_LOG(LogPython, Warning, TEXT("Initializing %d parameters"), argn);
#endif
	PyObject *py_args = PyTuple_New(argn);
	if (!py_args)
	{
		unreal_engine_py_log_error();
		return;
	}
	argn = 0;

	if (Context && !is_static) {
		PyObject *py_obj = (PyObject *)ue_get_python_uobject(Context);
		if (!py_obj) {
			unreal_engine_py_log_error();
			on_error = true;
		}
		else {
			Py_INCREF(py_obj);
			PyTuple_SetItem(py_args, argn++, py_obj);
		}
	}

	uint8 *frame = Stack.Locals;
	bool owns_frame = false;

	// is it a blueprint call ?
	if (!Stack.Code || *Stack.Code == EX_EndFunctionParms) {
		for (TFieldIterator<FProperty> PropIt(function); PropIt; ++PropIt) {
			FProperty* prop = *PropIt;
			if (!prop->HasAnyPropertyFlags(CPF_Parm))
				continue;
			if (prop->PropertyFlags & CPF_ReturnParm)
				continue;
			if (!on_error) {
				PyObject *arg = ue_py_convert_property(prop, (uint8 *)Stack.Locals, 0);
				if (!arg) {
					unreal_engine_py_log_error();
					on_error = true;
				}
				else {
					PyTuple_SetItem(py_args, argn++, arg);
				}
			}
		}
	}
	else {
		//UE_LOG(LogPython, Warning, TEXT("BLUEPRINT CALL"));
		frame = (uint8 *)FMemory_Alloca_Aligned(function->PropertiesSize, function->GetMinAlignment());
		function->InitializeStruct(frame);
		owns_frame = true;
		for (TFieldIterator<FProperty> PropIt(function); PropIt && *Stack.Code != EX_EndFunctionParms; ++PropIt) {
			FProperty* prop = *PropIt;
			if (!prop->HasAnyPropertyFlags(CPF_Parm))
				continue;
			Stack.Step(Stack.Object, prop->ContainerPtrToValuePtr<uint8>(frame));
			if (prop->PropertyFlags & CPF_ReturnParm)
				continue;
			if (!on_error) {
				PyObject *arg = ue_py_convert_property(prop, frame, 0);
				if (!arg) {
					unreal_engine_py_log_error();
					on_error = true;
				}
				else {
					PyTuple_SetItem(py_args, argn++, arg);
				}
			}
		}
	}

	if (Stack.Code)
	{
		Stack.Code++;
	}

	if (on_error || !function->py_callable) {
		Py_DECREF(py_args);
		if (owns_frame)
		{
			function->DestroyStruct(frame);
		}
		return;
	}

	PyObject *ret = PyObject_CallObject(function->py_callable, py_args);
	Py_DECREF(py_args);
	if (!ret) {
		unreal_engine_py_log_error();
		if (owns_frame)
		{
			function->DestroyStruct(frame);
		}
		return;
	}

	// get return value (if required)
	FProperty *return_property = function->GetReturnProperty();
	if (return_property && function->ReturnValueOffset != MAX_uint16) {
#if defined(UEPY_MEMORY_DEBUG)
		UE_LOG(LogPython, Warning, TEXT("FOUND RETURN VALUE"));
#endif
		if (ue_py_convert_pyobject(ret, return_property, frame, 0)) {
			// copy value to stack result value
			void* return_value = frame + function->ReturnValueOffset;
			if (RESULT_PARAM != return_value)
			{
				return_property->CopyCompleteValue(RESULT_PARAM, return_value);
			}
		}
		else {
			UE_LOG(LogPython, Error, TEXT("Invalid return value type for function %s"), *function->GetFName().ToString());
			if (PyErr_Occurred())
			{
				unreal_engine_py_log_error();
			}
		}
	}
	Py_DECREF(ret);
	if (owns_frame)
	{
		function->DestroyStruct(frame);
	}
}

UPythonFunction::~UPythonFunction()
{
	if (Py_IsInitialized())
	{
		FScopePythonGIL gil;
		Py_XDECREF(py_callable);
	}
	py_callable = nullptr;
	FUnrealEnginePythonHouseKeeper::Get()->UnregisterPyUObject(this);
#if defined(UEPY_MEMORY_DEBUG)
	UE_LOG(LogPython, Warning, TEXT("PythonFunction callable %p XDECREF'ed"), this);
#endif
}
