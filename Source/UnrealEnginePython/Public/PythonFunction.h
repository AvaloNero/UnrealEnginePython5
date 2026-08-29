#pragma once

#include "UnrealEnginePython.h"
#include "PythonFunction.generated.h"

UCLASS()
class UPythonFunction : public UFunction
{
	GENERATED_BODY()

public:
	~UPythonFunction();
	void SetPyCallable(PyObject *callable);
	void SetMixinRegistration(uint64 registration_id, FName function_name);
	void ClearMixinRegistration();
	bool IsMixinFunction() const { return mixin_registration_id != 0; }
	uint64 GetMixinRegistrationId() const { return mixin_registration_id; }
	FName GetMixinFunctionName() const { return mixin_function_name; }

	DECLARE_FUNCTION(CallPythonCallable);

	PyObject *py_callable = nullptr;

private:
	uint64 mixin_registration_id = 0;
	FName mixin_function_name = NAME_None;
};

