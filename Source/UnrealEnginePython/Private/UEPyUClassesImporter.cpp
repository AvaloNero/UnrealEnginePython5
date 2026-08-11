#include "UEPyUClassesImporter.h"

static PyObject *ue_PyUClassesImporter_getattro(ue_PyUClassesImporter *self, PyObject *attr_name)
{
	PyObject *py_attr = PyObject_GenericGetAttr((PyObject *)self, attr_name);
	if (!py_attr)
	{
		if (PyUnicodeOrString_Check(attr_name))
		{
			const char *attr = UEPyUnicode_AsUTF8(attr_name);
			if (attr[0] != '_')
			{
				FFieldClass* field_class = FFieldClass::GetNameToFieldClassMap().FindRef(FName(UTF8_TO_TCHAR(attr)));
				if (field_class && field_class->IsChildOf(FProperty::StaticClass()))
				{
					// UE5 properties are FFields rather than UObjects. Preserve the
					// historical unreal_engine.classes.FloatProperty-style API with
					// a non-owning capsule around the process-lifetime field class.
					// Check this map before UObject lookup because UE's Python plugin
					// may expose wrapper UClasses with the same short names.
					PyErr_Clear();
					return ue_py_new_ffield_class_capsule(field_class);
				}

				UClass *u_class = ue_py_find_first_object<UClass>(UTF8_TO_TCHAR(attr));
				if (u_class)
				{
					// swallow old exception
					PyErr_Clear();
					Py_RETURN_UOBJECT(u_class);
				}
			}
		}
	}
	return py_attr;
}

static PyTypeObject ue_PyUClassesImporterType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"unreal_engine.UClassesImporter", /* tp_name */
	sizeof(ue_PyUClassesImporter), /* tp_basicsize */
	0,                         /* tp_itemsize */
	0,       /* tp_dealloc */
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
	(getattrofunc)ue_PyUClassesImporter_getattro,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,        /* tp_flags */
	"Unreal Engine UClasses Importer",           /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,             /* tp_methods */
	0,
	0,
};

void ue_python_init_uclassesimporter(PyObject *ue_module)
{
	ue_PyUClassesImporterType.tp_new = PyType_GenericNew;

	if (PyType_Ready(&ue_PyUClassesImporterType) < 0)
		return;

	Py_INCREF(&ue_PyUClassesImporterType);
	PyModule_AddObject(ue_module, "UClassesImporter", (PyObject *)&ue_PyUClassesImporterType);
}

PyObject *py_ue_new_uclassesimporter()
{
	ue_PyUClassesImporter *ret = (ue_PyUClassesImporter *)PyObject_New(ue_PyUClassesImporter, &ue_PyUClassesImporterType);
	return (PyObject *)ret;
}
