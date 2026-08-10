#include "UEPyFAssetData.h"

#if WITH_EDITOR

#include "ObjectTools.h"
#include "Wrappers/UEPyFObjectThumbnail.h"

static PyObject *py_ue_fassetdata_get_asset(ue_PyFAssetData *self, PyObject * args)
{
	Py_RETURN_UOBJECT(self->asset_data.GetAsset());
}

static PyObject *py_ue_fassetdata_is_asset_loaded(ue_PyFAssetData *self, PyObject * args)
{
	if (self->asset_data.IsAssetLoaded())
		Py_RETURN_TRUE;
	Py_RETURN_FALSE;
}

static PyObject *py_ue_fassetdata_get_thumbnail(ue_PyFAssetData *self, PyObject * args)
{
	TArray<FName> names;
	FName name = FName(*self->asset_data.GetFullName());
	names.Add(name);
	FThumbnailMap map;

	if (!ThumbnailTools::ConditionallyLoadThumbnailsForObjects(names, map))
	{
		return PyErr_Format(PyExc_Exception, "Unable to retrieve thumbnail from FAssetData");
	}

	FObjectThumbnail *thumbnail = map.Find(name);
	if (!thumbnail)
	{
		return PyErr_Format(PyExc_Exception, "Unable to retrieve thumbnail from FAssetData");
	}

	return py_ue_new_fobject_thumbnail(*thumbnail);
}

#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 18

static PyObject *py_ue_fassetdata_has_custom_thumbnail(ue_PyFAssetData *self, PyObject * args)
{

#if UEP_LEGACY_ENGINE_MINOR_VERSION > 18
	if (!ThumbnailTools::AssetHasCustomThumbnail(self->asset_data.GetFullName()))
#else
	if (!ThumbnailTools::AssetHasCustomThumbnail(self->asset_data))
#endif
	{
		Py_RETURN_FALSE;
	}

	Py_RETURN_TRUE;
}
#endif

static PyObject *py_ue_fassetdata_has_cached_thumbnail(ue_PyFAssetData *self, PyObject * args)
{

	if (!ThumbnailTools::FindCachedThumbnail(self->asset_data.GetFullName()))
	{
		Py_RETURN_FALSE;
	}

	Py_RETURN_TRUE;
}

static PyMethodDef ue_PyFAssetData_methods[] = {
	{ "get_asset", (PyCFunction)py_ue_fassetdata_get_asset, METH_VARARGS, "" },
	{ "is_asset_loaded", (PyCFunction)py_ue_fassetdata_is_asset_loaded, METH_VARARGS, "" },
	{ "get_thumbnail", (PyCFunction)py_ue_fassetdata_get_thumbnail, METH_VARARGS, "" },

#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 18
	{ "has_custom_thumbnail", (PyCFunction)py_ue_fassetdata_has_custom_thumbnail, METH_VARARGS, "" },
#endif
	{ "has_cached_thumbnail", (PyCFunction)py_ue_fassetdata_has_cached_thumbnail, METH_VARARGS, "" },
	{ NULL }  /* Sentinel */
};

static PyObject *py_ue_fassetdata_get_asset_class(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.AssetClassPath.GetAssetName().ToString()));
}

static PyObject *py_ue_fassetdata_get_asset_name(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.AssetName.ToString()));
}

#if UEP_LEGACY_ENGINE_MINOR_VERSION < 17
static PyObject *py_ue_fassetdata_get_group_names(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.GroupNames.ToString()));
}
#endif

static PyObject *py_ue_fassetdata_get_object_path(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.GetObjectPathString()));
}

static PyObject *py_ue_fassetdata_get_package_flags(ue_PyFAssetData *self, void *closure)
{
	return PyLong_FromUnsignedLong(self->asset_data.PackageFlags);
}

static PyObject *py_ue_fassetdata_get_package_name(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.PackageName.ToString()));
}

static PyObject *py_ue_fassetdata_get_package_path(ue_PyFAssetData *self, void *closure)
{
	return PyUnicode_FromString(TCHAR_TO_UTF8(*self->asset_data.PackagePath.ToString()));
}

static PyObject *py_ue_fassetdata_get_tags_and_values(ue_PyFAssetData *self, void *closure)
{
	PyObject *ret = PyDict_New();
	if (!ret)
		return nullptr;

	for (auto It = self->asset_data.TagsAndValues.CreateConstIterator(); It; ++It)
	{
		const FString Key = It.Key().ToString();
		const FString Value = It.Value().AsString();
		PyObject *py_key = PyUnicode_FromString(TCHAR_TO_UTF8(*Key));
		PyObject *py_value = PyUnicode_FromString(TCHAR_TO_UTF8(*Value));
		if (!py_key || !py_value || PyDict_SetItem(ret, py_key, py_value) < 0)
		{
			Py_XDECREF(py_key);
			Py_XDECREF(py_value);
			Py_DECREF(ret);
			return nullptr;
		}
		Py_DECREF(py_key);
		Py_DECREF(py_value);
	}
	return ret;
}

static void ue_py_fassetdata_dealloc(ue_PyFAssetData *self)
{
	self->asset_data.~FAssetData();
	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyGetSetDef ue_PyFAssetData_getseters[] = {
	{ (char *)"asset_class", (getter)py_ue_fassetdata_get_asset_class, nullptr, (char *)"asset_class" },
	{ (char *)"asset_name", (getter)py_ue_fassetdata_get_asset_name, nullptr, (char *)"asset_name" },
#if UEP_LEGACY_ENGINE_MINOR_VERSION < 17
	{ (char *)"group_names", (getter)py_ue_fassetdata_get_group_names, nullptr, (char *)"group_names" },
#endif
	{ (char *)"object_path",(getter)py_ue_fassetdata_get_object_path, nullptr, (char *)"object_path" },
	{ (char *)"package_flags",(getter)py_ue_fassetdata_get_package_flags, nullptr, (char *)"package_flags" },
	{ (char *)"package_name", (getter)py_ue_fassetdata_get_package_name, nullptr, (char *)"package_name" },
	{ (char *)"package_path", (getter)py_ue_fassetdata_get_package_path, nullptr, (char *)"package_path" },
	{ (char *)"tags_and_values", (getter)py_ue_fassetdata_get_tags_and_values, nullptr, (char *)"tags_and_values" },
	{ NULL }  /* Sentinel */
};

static int ue_py_fassetdata_init(ue_PyFAssetData *self, PyObject *args, PyObject *kwargs)
{
	// avoid FAssetData manual creation
	return -1;
}

static PyObject *ue_PyFAssetData_str(ue_PyFAssetData *self)
{
	return PyUnicode_FromFormat("<unreal_engine.FAssetData '%s'>",
		TCHAR_TO_UTF8(*self->asset_data.GetExportTextName()));
}

static PyTypeObject ue_PyFAssetDataType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"unreal_engine.FAssetData", /* tp_name */
	sizeof(ue_PyFAssetData),    /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)ue_py_fassetdata_dealloc,   /* tp_dealloc */
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
	(reprfunc)ue_PyFAssetData_str,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,        /* tp_flags */
	"Unreal Engine FAssetData", /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	ue_PyFAssetData_methods,    /* tp_methods */
	0,   /* tp_members */
	ue_PyFAssetData_getseters,                         /* tp_getset */
};

void ue_python_init_fassetdata(PyObject *ue_module)
{
	ue_PyFAssetDataType.tp_new = PyType_GenericNew;;
	ue_PyFAssetDataType.tp_init = (initproc)ue_py_fassetdata_init;
	if (PyType_Ready(&ue_PyFAssetDataType) < 0)
		return;

	Py_INCREF(&ue_PyFAssetDataType);
	PyModule_AddObject(ue_module, "FAssetData", (PyObject *)&ue_PyFAssetDataType);
}

PyObject *py_ue_new_fassetdata(FAssetData asset_data)
{
	ue_PyFAssetData *ret = (ue_PyFAssetData *)PyObject_New(ue_PyFAssetData, &ue_PyFAssetDataType);
	if (!ret)
		return nullptr;

	new(&ret->asset_data) FAssetData(asset_data);
	return (PyObject *)ret;
}

ue_PyFAssetData *py_ue_is_fassetdata(PyObject *obj)
{
	if (!PyObject_IsInstance(obj, (PyObject *)&ue_PyFAssetDataType))
		return nullptr;
	return (ue_PyFAssetData *)obj;
}

#endif
