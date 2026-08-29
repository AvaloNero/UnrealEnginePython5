#pragma once

#include "UnrealEnginePython.h"

class UPythonFunction;

// Python module API.
PyObject* py_unreal_engine_register_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_unregister_mixin(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_unregister_all_mixins(PyObject* self, PyObject* args);
PyObject* py_unreal_engine_get_registered_mixins(PyObject* self, PyObject* args);

// UObject API.
PyObject* py_ue_call_mixin_original(ue_PyUObject* self, PyObject* args, PyObject* kwargs);

// Dispatch and attribute integration.
bool ue_py_prepare_mixin_call(UPythonFunction* function, UObject* context);
PyObject* ue_py_get_mixin_attribute(ue_PyUObject* self, PyObject* attr_name);

// Called before the Python VM/housekeeper shuts down. Idempotent.
void unreal_engine_python_unregister_all_mixins();
