#pragma once

#include "UnrealEnginePython.h"
#include "PythonDelegate.generated.h"

struct FInputActionValue;

UCLASS()
class UPythonDelegate : public UObject
{
	GENERATED_BODY()

public:
	UPythonDelegate();
	~UPythonDelegate();
	virtual void ProcessEvent(UFunction *function, void *Parms) override;
	void SetPyCallable(PyObject *callable);
	void ClearPyCallable();
    bool UsesPyCallable(PyObject *callable);
	void SetSignature(UFunction *original_signature);

	void PyInputHandler();
	void PyInputAxisHandler(float value);
	void PyEnhancedInputActionHandler(const FInputActionValue& value);

protected:
	UFunction * signature;
	bool signature_set;

	UFUNCTION()
		void PyFakeCallable();

	PyObject *py_callable;


};

