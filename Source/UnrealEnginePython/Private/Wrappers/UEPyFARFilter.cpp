#include "UEPyFARFilter.h"

#if WITH_EDITOR

static PyObject *py_ue_farfilter_append(ue_PyFARFilter *self, PyObject * args)
{
	PyObject *pyfilter = nullptr;
	if (!PyArg_ParseTuple(args, "O:append", &pyfilter))
		return NULL;
	ue_PyFARFilter *filter = py_ue_is_farfilter(pyfilter);
	if (!filter)
		return PyErr_Format(PyExc_TypeError, "argument is not a FARFilter");
	if (!py_ue_sync_farfilter((PyObject *)self) || !py_ue_sync_farfilter((PyObject *)filter))
		return nullptr;
	self->filter.Append(filter->filter);
	Py_RETURN_NONE;
}

static PyObject *py_ue_farfilter_clear(ue_PyFARFilter *self)
{
	py_ue_clear_farfilter(self);
	self->filter.Clear();
	Py_RETURN_NONE;
}

static PyObject *py_ue_farfilter_is_empty(ue_PyFARFilter *self, PyObject * args)
{
	if (!py_ue_sync_farfilter((PyObject *)self))
		return nullptr;
	if (self->filter.IsEmpty())
		Py_RETURN_TRUE;
	Py_RETURN_FALSE;
}

static PyMethodDef ue_PyFARFilter_methods[] = {
	{ "append", (PyCFunction)py_ue_farfilter_append, METH_VARARGS, "" },
	{ "clear", (PyCFunction)py_ue_farfilter_clear, METH_VARARGS, "" },
	{ "is_empty", (PyCFunction)py_ue_farfilter_is_empty, METH_VARARGS, "" },
	{ NULL }  /* Sentinel */
};

static PyObject *py_ue_farfilter_get_include_only_on_disk_assets(ue_PyFARFilter *self, void *closure)
{
	if (self->filter.bIncludeOnlyOnDiskAssets)
		Py_RETURN_TRUE;
	Py_RETURN_FALSE;
}

static int py_ue_farfilter_set_include_only_on_disk_assets(ue_PyFARFilter *self, PyObject *value, void *closure)
{
	if (value && PyBool_Check(value))
	{
		if (PyObject_IsTrue(value))
			self->filter.bIncludeOnlyOnDiskAssets = true;
		else
			self->filter.bIncludeOnlyOnDiskAssets = false;
		return 0;
	}
	PyErr_SetString(PyExc_TypeError, "value is not a bool");
	return -1;
}

static PyObject *py_ue_farfilter_get_recursive_classes(ue_PyFARFilter *self, void *closure)
{
	if (self->filter.bRecursiveClasses)
		Py_RETURN_TRUE;
	Py_RETURN_FALSE;
}

static int py_ue_farfilter_set_recursive_classes(ue_PyFARFilter *self, PyObject *value, void *closure)
{
	if (value && PyBool_Check(value))
	{
		if (PyObject_IsTrue(value))
			self->filter.bRecursiveClasses = true;
		else
			self->filter.bRecursiveClasses = false;
		return 0;
	}
	PyErr_SetString(PyExc_TypeError, "value is not a bool");
	return -1;
}

static PyObject *py_ue_farfilter_get_recursive_paths(ue_PyFARFilter *self, void *closure)
{
	if (self->filter.bRecursivePaths)
		Py_RETURN_TRUE;
	Py_RETURN_FALSE;
}

static int py_ue_farfilter_set_recursive_paths(ue_PyFARFilter *self, PyObject *value, void *closure)
{
	if (value && PyBool_Check(value))
	{
		if (PyObject_IsTrue(value))
			self->filter.bRecursivePaths = true;
		else
			self->filter.bRecursivePaths = false;
		return 0;
	}
	PyErr_SetString(PyExc_TypeError, "value is not a bool");
	return -1;
}

static PyGetSetDef ue_PyFARFilter_getseters[] = {
	{ (char *)"include_only_on_disk_assets", (getter)py_ue_farfilter_get_include_only_on_disk_assets,
									 (setter)py_ue_farfilter_set_include_only_on_disk_assets, (char *)"", NULL },
	{ (char *)"recursive_classes", (getter)py_ue_farfilter_get_recursive_classes,
						   (setter)py_ue_farfilter_set_recursive_classes, (char *)"", NULL },
	{ (char *)"recursive_paths", (getter)py_ue_farfilter_get_recursive_paths,
						 (setter)py_ue_farfilter_set_recursive_paths, (char *)"", NULL },
	{ NULL }  /* Sentinel */
};

static PyMemberDef ue_PyFARFilter_members[] = {
	{ (char *)"class_names", T_OBJECT_EX, offsetof(ue_PyFARFilter, class_names), 0, (char *)"class_names" },
	{ (char *)"object_paths", T_OBJECT_EX, offsetof(ue_PyFARFilter, object_paths), 0, (char *)"object_paths" },
	{ (char *)"package_names", T_OBJECT_EX, offsetof(ue_PyFARFilter, package_names), 0, (char *)"package_names" },
	{ (char *)"package_paths", T_OBJECT_EX, offsetof(ue_PyFARFilter, package_paths), 0, (char *)"package_paths" },
	{ (char *)"recursive_classes_exclusion_set", T_OBJECT_EX, offsetof(ue_PyFARFilter, recursive_classes_exclusion_set), 0, (char *)"recursive_classes_exclusion_set" },
	{ (char *)"tags_and_values", T_OBJECT_EX, offsetof(ue_PyFARFilter, tags_and_values), 0, (char *)"tags_and_values" },
	{ NULL }  /* Sentinel */
};

static int ue_py_farfilter_init(ue_PyFARFilter *self, PyObject *args, PyObject *kwargs)
{
	return 0;
}

static void ue_py_farfilter_dealloc(ue_PyFARFilter *self)
{
	self->filter.~FARFilter();

	Py_XDECREF(self->class_names);
	Py_XDECREF(self->object_paths);
	Py_XDECREF(self->package_names);
	Py_XDECREF(self->package_paths);
	Py_XDECREF(self->recursive_classes_exclusion_set);
	Py_XDECREF(self->tags_and_values);


	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject * ue_py_farfilter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	ue_PyFARFilter *self;

	self = (ue_PyFARFilter *)type->tp_alloc(type, 0);
	if (self != NULL)
	{

		new(&self->filter) FARFilter();

		self->class_names = PyList_New(0);
		if (self->class_names == NULL)
		{
			Py_DECREF(self);
			return NULL;
		}

		self->object_paths = PyList_New(0);
		if (self->object_paths == NULL)
		{
			Py_DECREF(self);
			return NULL;
		}

		self->package_names = PyList_New(0);
		if (self->package_names == NULL)
		{
			Py_DECREF(self);
			return NULL;
		}

		self->package_paths = PyList_New(0);
		if (self->package_paths == NULL)
		{
			Py_DECREF(self);
			return NULL;
		}

		self->recursive_classes_exclusion_set = PySet_New(NULL);
		if (self->recursive_classes_exclusion_set == NULL)
		{
			Py_DECREF(self);
			return NULL;
		}

		PyObject *collections = PyImport_ImportModule("collections");
		if (!collections)
		{
			unreal_engine_py_log_error();
			Py_DECREF(self);
			return NULL;
		}
		PyObject *collections_module_dict = PyModule_GetDict(collections);
		PyObject *defaultdict_cls = PyDict_GetItemString(collections_module_dict, "defaultdict");
		PyObject *default_dict = PyObject_CallFunctionObjArgs(defaultdict_cls, (PyObject *)&PySet_Type, nullptr);
		Py_DECREF(collections);
		if (!default_dict)
		{
			unreal_engine_py_log_error();
			Py_DECREF(self);
			return NULL;
		}

		self->tags_and_values = default_dict;
	}

	return (PyObject *)self;
}

static PyTypeObject ue_PyFARFilterType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"unreal_engine.FARFilter", /* tp_name */
	sizeof(ue_PyFARFilter),    /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)ue_py_farfilter_dealloc,   /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_reserved */
	0,                         /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash  */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,        /* tp_flags */
	"Unreal Engine FARFilter", /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	ue_PyFARFilter_methods,    /* tp_methods */
	ue_PyFARFilter_members,    /* tp_members */
	ue_PyFARFilter_getseters,   /* tp_getset */
};

void ue_python_init_farfilter(PyObject *ue_module)
{
	ue_PyFARFilterType.tp_new = ue_py_farfilter_new;
	ue_PyFARFilterType.tp_init = (initproc)ue_py_farfilter_init;
	if (PyType_Ready(&ue_PyFARFilterType) < 0)
		return;

	Py_INCREF(&ue_PyFARFilterType);
	PyModule_AddObject(ue_module, "FARFilter", (PyObject *)&ue_PyFARFilterType);
}

void py_ue_clear_seq(PyObject *pyseq)
{
	if (PyList_Check(pyseq))
	{
		PyList_SetSlice(pyseq, 0, PyList_GET_SIZE(pyseq), nullptr);
	}
	else if (PySet_Check(pyseq))
	{
		PySet_Clear(pyseq);
	}
	else if (PyDict_Check(pyseq))
	{
		PyDict_Clear(pyseq);
	}
}

void py_ue_clear_farfilter(ue_PyFARFilter *pyfilter)
{
	py_ue_clear_seq(pyfilter->class_names);
	py_ue_clear_seq(pyfilter->object_paths);
	py_ue_clear_seq(pyfilter->package_names);
	py_ue_clear_seq(pyfilter->package_paths);
	py_ue_clear_seq(pyfilter->recursive_classes_exclusion_set);
	py_ue_clear_seq(pyfilter->tags_and_values);
}

static bool GetPythonString(PyObject *py_object, FString& OutString, const char *field_name)
{
	if (!PyUnicode_Check(py_object))
	{
		PyErr_Format(PyExc_TypeError, "%s entries must be strings", field_name);
		return false;
	}

	const char *utf8 = UEPyUnicode_AsUTF8(py_object);
	if (!utf8)
		return false;
	OutString = UTF8_TO_TCHAR(utf8);
	return true;
}

static bool SyncNameArray(PyObject *pylist, TArray<FName> &uelist, const char *field_name)
{
	if (!PyList_Check(pylist))
	{
		PyErr_Format(PyExc_TypeError, "%s must be a list", field_name);
		return false;
	}

	Py_ssize_t pylist_len = PyList_Size(pylist);
	uelist.Reset(pylist_len);
	for (Py_ssize_t i = 0; i < pylist_len; i++)
	{
		PyObject *py_item = PyList_GetItem(pylist, i);
		FString Item;
		if (!GetPythonString(py_item, Item, field_name))
			return false;
		uelist.Add(FName(*Item));
	}
	return true;
}

static bool SyncClassPathArray(PyObject *pylist, TArray<FTopLevelAssetPath>& uelist, const char *field_name)
{
	if (!PyList_Check(pylist))
	{
		PyErr_Format(PyExc_TypeError, "%s must be a list", field_name);
		return false;
	}

	uelist.Reset(PyList_GET_SIZE(pylist));
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(pylist); ++i)
	{
		FString ClassName;
		if (!GetPythonString(PyList_GetItem(pylist, i), ClassName, field_name))
			return false;

		FTopLevelAssetPath ClassPath = ClassName.StartsWith(TEXT("/"))
			? FTopLevelAssetPath(ClassName)
			: UClass::TryConvertShortTypeNameToPathName<UStruct>(*ClassName, ELogVerbosity::NoLogging);
		if (ClassPath.IsNull())
		{
			PyErr_Format(PyExc_ValueError, "unable to resolve class path '%s'", TCHAR_TO_UTF8(*ClassName));
			return false;
		}
		uelist.Add(ClassPath);
	}
	return true;
}

static bool SyncSoftObjectPathArray(PyObject *pylist, TArray<FSoftObjectPath>& uelist, const char *field_name)
{
	if (!PyList_Check(pylist))
	{
		PyErr_Format(PyExc_TypeError, "%s must be a list", field_name);
		return false;
	}

	uelist.Reset(PyList_GET_SIZE(pylist));
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(pylist); ++i)
	{
		FString ObjectPathString;
		if (!GetPythonString(PyList_GetItem(pylist, i), ObjectPathString, field_name))
			return false;
		FSoftObjectPath ObjectPath(ObjectPathString);
		if (ObjectPath.IsNull())
		{
			PyErr_Format(PyExc_ValueError, "invalid soft object path '%s'", TCHAR_TO_UTF8(*ObjectPathString));
			return false;
		}
		uelist.Add(ObjectPath);
	}
	return true;
}

static bool SyncRecursiveClassExclusions(PyObject *pyset, TSet<FTopLevelAssetPath>& OutPaths)
{
	PyObject *iterator = PyObject_GetIter(pyset);
	if (!iterator)
	{
		PyErr_SetString(PyExc_TypeError, "recursive_classes_exclusion_set must be iterable");
		return false;
	}

	OutPaths.Reset();
	while (PyObject *py_item = PyIter_Next(iterator))
	{
		FString ClassName;
		const bool bStringValid = GetPythonString(py_item, ClassName, "recursive_classes_exclusion_set");
		Py_DECREF(py_item);
		if (!bStringValid)
		{
			Py_DECREF(iterator);
			return false;
		}

		FTopLevelAssetPath ClassPath = ClassName.StartsWith(TEXT("/"))
			? FTopLevelAssetPath(ClassName)
			: UClass::TryConvertShortTypeNameToPathName<UStruct>(*ClassName, ELogVerbosity::NoLogging);
		if (ClassPath.IsNull())
		{
			Py_DECREF(iterator);
			PyErr_Format(PyExc_ValueError, "unable to resolve class path '%s'", TCHAR_TO_UTF8(*ClassName));
			return false;
		}
		OutPaths.Add(ClassPath);
	}
	Py_DECREF(iterator);
	return !PyErr_Occurred();
}

bool py_ue_sync_farfilter(PyObject *pyobj)
{
	ue_PyFARFilter *pyfilter = py_ue_is_farfilter(pyobj);
	if (!pyfilter)
	{
		PyErr_SetString(PyExc_TypeError, "object is not a FARFilter");
		return false;
	}

	if (!SyncClassPathArray(pyfilter->class_names, pyfilter->filter.ClassPaths, "class_names") ||
		!SyncSoftObjectPathArray(pyfilter->object_paths, pyfilter->filter.SoftObjectPaths, "object_paths") ||
		!SyncNameArray(pyfilter->package_names, pyfilter->filter.PackageNames, "package_names") ||
		!SyncNameArray(pyfilter->package_paths, pyfilter->filter.PackagePaths, "package_paths") ||
		!SyncRecursiveClassExclusions(pyfilter->recursive_classes_exclusion_set, pyfilter->filter.RecursiveClassPathsExclusionSet))
	{
		return false;
	}

	PyObject *pykey, *pyvalue;
	Py_ssize_t pypos = 0;
	pyfilter->filter.TagsAndValues.Reset();
	if (!PyDict_Check(pyfilter->tags_and_values))
	{
		PyErr_SetString(PyExc_TypeError, "tags_and_values must be a dictionary");
		return false;
	}
	while (PyDict_Next(pyfilter->tags_and_values, &pypos, &pykey, &pyvalue))
	{
		FString KeyString;
		if (!GetPythonString(pykey, KeyString, "tags_and_values keys"))
			return false;
		const FName Key(*KeyString);

		PyObject *iterator = PyObject_GetIter(pyvalue);
		if (!iterator)
		{
			PyErr_SetString(PyExc_TypeError, "tags_and_values values must be iterable");
			return false;
		}

		while (PyObject *py_item = PyIter_Next(iterator))
		{
			FString Value;
			const bool bStringValid = GetPythonString(py_item, Value, "tags_and_values entries");
			Py_DECREF(py_item);
			if (!bStringValid)
			{
				Py_DECREF(iterator);
				return false;
			}
			pyfilter->filter.TagsAndValues.AddUnique(Key, TOptional<FString>(MoveTemp(Value)));
		}
		Py_DECREF(iterator);
		if (PyErr_Occurred())
			return false;
	}
	return true;
}

PyObject *py_ue_new_farfilter(FARFilter filter)
{
	ue_PyFARFilter *ret = (ue_PyFARFilter *)PyObject_CallObject((PyObject *)&ue_PyFARFilterType, NULL);
	if (!ret)
		return nullptr;
	ret->filter = filter;
	return (PyObject *)ret;
}

ue_PyFARFilter *py_ue_is_farfilter(PyObject *obj)
{
	if (!PyObject_IsInstance(obj, (PyObject *)&ue_PyFARFilterType))
		return nullptr;
	return (ue_PyFARFilter *)obj;
}

#endif
