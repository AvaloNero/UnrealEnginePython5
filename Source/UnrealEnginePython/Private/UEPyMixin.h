#pragma once

#include "UnrealEnginePython.h"

class UPythonFunction;
class UFunction;

enum class EUEPyMixinDispatch : uint8
{
	NotMixin,
	Python,
	Original,
	Error,
};

/**
 * Marks execution that may call user-authored Python or Blueprint logic.
 *
 * Mixin registry mutations are rejected while one of these scopes is active;
 * this keeps callback-driven re-entry from invalidating bindings used by the
 * current dispatch. Nested dispatch itself remains supported.
 */
class FUEPyScopedMixinCallback
{
public:
	FUEPyScopedMixinCallback();
	~FUEPyScopedMixinCallback();

	FUEPyScopedMixinCallback(const FUEPyScopedMixinCallback&) = delete;
	FUEPyScopedMixinCallback& operator=(const FUEPyScopedMixinCallback&) = delete;

	static bool IsActive();
};

// Python module API.
PyObject* py_unreal_engine_register_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_register_mixin_profile(PyObject* self, PyObject* args, PyObject* kwargs);
PyObject* py_unreal_engine_mixin(PyObject* self, PyObject* args, PyObject* kwargs);
PyObject* py_unreal_engine_set_mixin_profile(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_clear_mixin_profile(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_get_mixin_profile(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_set_default_mixin_profile(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_register_declared_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_register_loaded_mixin_interfaces(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_unregister_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_unregister_all_mixins(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_get_registered_mixins(PyObject* self, PyObject* args);

// UObject API.
PyObject* py_ue_call_mixin_original(ue_PyUObject* self, PyObject* args, PyObject* kwargs);

// Dispatch and attribute integration.
EUEPyMixinDispatch ue_py_resolve_mixin_call(
	UPythonFunction* function,
	UObject* context,
	PyObject*& out_callable,
	UFunction*& out_original);
PyObject* ue_py_get_mixin_attribute(ue_PyUObject* self, PyObject* attr_name);

// Runtime discovery for Blueprint classes implementing UUEPPythonMixinInterface.
void unreal_engine_python_enable_mixin_discovery();
void unreal_engine_python_disable_mixin_discovery();

// Called before the Python VM/housekeeper shuts down. Idempotent.
void unreal_engine_python_unregister_all_mixins();
