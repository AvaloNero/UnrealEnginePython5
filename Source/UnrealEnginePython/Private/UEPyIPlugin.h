#pragma once

#include "UEPyModule.h"

#include "Runtime/Projects/Public/Interfaces/IPluginManager.h"

typedef struct
{
	PyObject_HEAD
		/* Type-specific fields go here. */
		IPlugin *plugin;
} ue_PyIPlugin;

PyObject *py_ue_new_iplugin(IPlugin *);

void ue_python_init_iplugin(PyObject *);

PyObject *py_unreal_engine_get_discovered_plugins(PyObject *, PyObject *);
PyObject *py_unreal_engine_get_enabled_plugins(PyObject *, PyObject *);
PyObject *py_unreal_engine_find_plugin(PyObject *, PyObject *);
