#include "UEPyFFoliageInstance.h"

#include "Wrappers/UEPyFVector.h"
#include "Wrappers/UEPyFRotator.h"
#include "Wrappers/UEPyFTransform.h"
#include "Components/ActorComponent.h"
#include "Runtime/Foliage/Public/InstancedFoliageActor.h"

#if WITH_EDITOR

#define get_instance(x) FFoliageInstance *instance = get_foliage_instance(x);\
	if (!instance)\
		return nullptr;

#define set_instance(x) FFoliageInstance *instance = get_foliage_instance(x);\
	if (!instance)\
		return -1;

using FFoliageActorWeakPtr = TWeakObjectPtr<AInstancedFoliageActor>;
using FFoliageTypeWeakPtr = TWeakObjectPtr<UFoliageType>;

static FFoliageInfo* get_foliage_info(ue_PyFFoliageInstance* self)
{
	if (!self->foliage_actor.IsValid())
	{
		PyErr_SetString(PyExc_Exception, "AInstancedFoliageActor is in invalid state");
		return nullptr;
	}

	if (!self->foliage_type.IsValid())
	{
		PyErr_SetString(PyExc_Exception, "UFoliageType is in invalid state");
		return nullptr;
	}

	FFoliageInfo* info = self->foliage_actor->FindInfo(self->foliage_type.Get());
	if (!info)
	{
		PyErr_SetString(PyExc_Exception, "foliage type is not registered with the instanced foliage actor");
	}
	return info;
}

static FFoliageInstance* get_foliage_instance(ue_PyFFoliageInstance* self)
{
	FFoliageInfo* info = get_foliage_info(self);
	if (!info)
		return nullptr;

	if (info->Instances.IsValidIndex(self->instance_id))
	{
		return &info->Instances[self->instance_id];
	}

	PyErr_SetString(PyExc_Exception, "invalid foliage instance id");
	return nullptr;
}

static PyObject* ue_PyFFoliageInstance_str(ue_PyFFoliageInstance* self)
{
	return PyUnicode_FromFormat("<unreal_engine.FFoliageInstance %d>",
		self->instance_id);
}


static PyObject* py_ue_ffoliage_instance_get_location(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return py_ue_new_fvector(instance->Location);
}

static int py_ue_ffoliage_instance_set_location(ue_PyFFoliageInstance* self, PyObject* value, void* closure)
{
	set_instance(self);
	if (value)
	{
		ue_PyFVector* vec = py_ue_is_fvector(value);
		if (vec)
		{
			TArray<int32> instances;
			instances.Add(self->instance_id);
			FFoliageInfo* info = get_foliage_info(self);
			if (!info)
				return -1;
			info->PreMoveInstances(instances);
			instance->Location = vec->vec;
			info->PostMoveInstances(instances, true);
			return 0;
		}
	}
	PyErr_SetString(PyExc_TypeError, "value is not an FVector");
	return -1;
}

static int py_ue_ffoliage_instance_set_rotation(ue_PyFFoliageInstance* self, PyObject* value, void* closure)
{
	set_instance(self);
	if (value)
	{
		ue_PyFRotator* rot = py_ue_is_frotator(value);
		if (rot)
		{
			TArray<int32> instances;
			instances.Add(self->instance_id);
			FFoliageInfo* info = get_foliage_info(self);
			if (!info)
				return -1;
			info->PreMoveInstances(instances);
			instance->Rotation = rot->rot;
			info->PostMoveInstances(instances, true);
			return 0;
		}
	}
	PyErr_SetString(PyExc_TypeError, "value is not an FRotator");
	return -1;
}

static PyObject* py_ue_ffoliage_instance_get_draw_scale3d(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return py_ue_new_fvector(FVector(instance->DrawScale3D));
}

static PyObject* py_ue_ffoliage_instance_get_flags(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return PyLong_FromUnsignedLong(instance->Flags);
}

static PyObject* py_ue_ffoliage_instance_get_pre_align_rotation(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return py_ue_new_frotator(instance->PreAlignRotation);
}

static PyObject* py_ue_ffoliage_instance_get_rotation(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return py_ue_new_frotator(instance->Rotation);
}

static PyObject* py_ue_ffoliage_instance_get_zoffset(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return PyFloat_FromDouble(instance->ZOffset);
}

static PyObject* py_ue_ffoliage_instance_get_procedural_guid(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	FGuid guid = instance->ProceduralGuid;
	return py_ue_new_owned_uscriptstruct(ue_py_find_first_object<UScriptStruct>(TEXT("Guid")), (uint8*)& guid);
}

static PyObject* py_ue_ffoliage_instance_get_base_id(ue_PyFFoliageInstance* self, void* closure)
{
	get_instance(self);
	return PyLong_FromLong(instance->BaseId);
}

static PyObject* py_ue_ffoliage_instance_get_instance_id(ue_PyFFoliageInstance* self, void* closure)
{
	return PyLong_FromLong(self->instance_id);
}

#if UEP_LEGACY_ENGINE_MINOR_VERSION > 19
static PyObject * py_ue_ffoliage_instance_get_base_component(ue_PyFFoliageInstance * self, void* closure)
{
	get_instance(self);
	Py_RETURN_UOBJECT(instance->BaseComponent);
}
#endif



static PyGetSetDef ue_PyFFoliageInstance_getseters[] = {
	{ (char*)"location", (getter)py_ue_ffoliage_instance_get_location,  (setter)py_ue_ffoliage_instance_set_location, (char*)"", NULL },
	{ (char*)"draw_scale3d", (getter)py_ue_ffoliage_instance_get_draw_scale3d, nullptr, (char*)"", NULL },
	{ (char*)"flags", (getter)py_ue_ffoliage_instance_get_flags, nullptr, (char*)"", NULL },
	{ (char*)"pre_align_rotation", (getter)py_ue_ffoliage_instance_get_pre_align_rotation, nullptr, (char*)"", NULL },
	{ (char*)"rotation", (getter)py_ue_ffoliage_instance_get_rotation, (setter)py_ue_ffoliage_instance_set_rotation, (char*)"", NULL },
	{ (char*)"zoffset", (getter)py_ue_ffoliage_instance_get_zoffset, nullptr, (char*)"", NULL },
	{ (char*)"procedural_guid", (getter)py_ue_ffoliage_instance_get_procedural_guid, nullptr, (char*)"", NULL },
	{ (char*)"guid", (getter)py_ue_ffoliage_instance_get_procedural_guid, nullptr, (char*)"", NULL },
	{ (char*)"base_id", (getter)py_ue_ffoliage_instance_get_base_id, nullptr, (char*)"", NULL },
	{ (char*)"instance_id", (getter)py_ue_ffoliage_instance_get_instance_id, nullptr, (char*)"", NULL },
#if UEP_LEGACY_ENGINE_MINOR_VERSION > 19
	{ (char*)"base_component", (getter)py_ue_ffoliage_instance_get_base_component, nullptr, (char*)"", NULL },
#endif
	{ NULL }  /* Sentinel */
};

static PyObject* py_ue_ffoliage_instance_get_instance_world_transform(ue_PyFFoliageInstance* self, PyObject* args)
{
	get_instance(self);
	return py_ue_new_ftransform(instance->GetInstanceWorldTransform());
}

static PyObject* py_ue_ffoliage_instance_align_to_normal(ue_PyFFoliageInstance* self, PyObject* args)
{
	get_instance(self);

	PyObject* py_vec;
	float align_max_angle = 0;

	if (!PyArg_ParseTuple(args, "O|f:align_to_normal", &py_vec, &align_max_angle))
		return nullptr;

	ue_PyFVector* vec = py_ue_is_fvector(py_vec);
	if (!vec)
	{
		return PyErr_Format(PyExc_Exception, "argument is not an FVector");
	}

	instance->AlignToNormal(vec->vec, align_max_angle);

	Py_RETURN_NONE;
}


static PyMethodDef ue_PyFFoliageInstance_methods[] = {
	{ "get_instance_world_transform", (PyCFunction)py_ue_ffoliage_instance_get_instance_world_transform, METH_VARARGS, "" },
	{ "align_to_normal", (PyCFunction)py_ue_ffoliage_instance_align_to_normal, METH_VARARGS, "" },
	{ NULL }  /* Sentinel */
};

static void ue_py_ffoliage_instance_dealloc(ue_PyFFoliageInstance* self)
{
	self->foliage_actor.~FFoliageActorWeakPtr();
	self->foliage_type.~FFoliageTypeWeakPtr();
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject ue_PyFFoliageInstanceType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"unreal_engine.FFoliageInstance", /* tp_name */
	sizeof(ue_PyFFoliageInstance), /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)ue_py_ffoliage_instance_dealloc,       /* tp_dealloc */
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
	(reprfunc)ue_PyFFoliageInstance_str,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
#if PY_MAJOR_VERSION < 3
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_CHECKTYPES,        /* tp_flags */
#else
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /* tp_flags */
#endif
	"Unreal Engine FFoliageInstance",           /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	ue_PyFFoliageInstance_methods,             /* tp_methods */
	0,
	ue_PyFFoliageInstance_getseters,
};

void ue_python_init_ffoliage_instance(PyObject* ue_module)
{
	ue_PyFFoliageInstanceType.tp_new = PyType_GenericNew;

	if (PyType_Ready(&ue_PyFFoliageInstanceType) < 0)
		return;

	Py_INCREF(&ue_PyFFoliageInstanceType);
	PyModule_AddObject(ue_module, "FoliageInstance", (PyObject*)& ue_PyFFoliageInstanceType);
}

PyObject* py_ue_new_ffoliage_instance(AInstancedFoliageActor* foliage_actor, UFoliageType* foliage_type, int32 instance_id)
{
	ue_PyFFoliageInstance* ret = (ue_PyFFoliageInstance*)PyObject_New(ue_PyFFoliageInstance, &ue_PyFFoliageInstanceType);
	if (!ret)
		return nullptr;
	new(&ret->foliage_actor) FFoliageActorWeakPtr(foliage_actor);
	new(&ret->foliage_type) FFoliageTypeWeakPtr(foliage_type);
	ret->instance_id = instance_id;
	return (PyObject*)ret;
}

#endif
