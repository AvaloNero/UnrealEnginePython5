#include "UEPyLandscape.h"


#if WITH_EDITOR

#include "Wrappers/UEPyFRawMesh.h"
#include "Runtime/Landscape/Classes/LandscapeProxy.h"
#include "Runtime/Landscape/Classes/LandscapeInfo.h"
#include "GameFramework/GameModeBase.h"

PyObject* py_ue_create_landscape_info(ue_PyUObject* self, PyObject* args)
{

	ue_py_check(self);

	ALandscapeProxy* landscape = ue_py_check_type<ALandscapeProxy>(self);
	if (!landscape)
		return PyErr_Format(PyExc_Exception, "uobject is not a ULandscapeProxy");

	Py_RETURN_UOBJECT(landscape->CreateLandscapeInfo());
}

PyObject* py_ue_get_landscape_info(ue_PyUObject* self, PyObject* args)
{

	ue_py_check(self);

	ALandscapeProxy* landscape = ue_py_check_type<ALandscapeProxy>(self);
	if (!landscape)
		return PyErr_Format(PyExc_Exception, "uobject is not a ULandscapeProxy");

	ULandscapeInfo* info = landscape->GetLandscapeInfo();
	if (!info)
		Py_RETURN_NONE;

	Py_RETURN_UOBJECT(info);
}

PyObject* py_ue_landscape_import(ue_PyUObject* self, PyObject* args)
{

	ue_py_check(self);

	int section_size;
	int sections_per_component;
	int component_x;
	int component_y;
	Py_buffer heightmap_buffer = {};
	int layer_type = (int)ELandscapeImportAlphamapType::Additive;

	if (!PyArg_ParseTuple(args, "iiiiy*|i:landscape_import", &section_size, &sections_per_component, &component_x, &component_y, &heightmap_buffer, &layer_type))
		return nullptr;

	ALandscapeProxy* landscape = ue_py_check_type<ALandscapeProxy>(self);
	if (!landscape)
	{
		PyBuffer_Release(&heightmap_buffer);
		return PyErr_Format(PyExc_Exception, "uobject is not a ULandscapeProxy");
	}

	if (section_size <= 0 || sections_per_component <= 0 || component_x <= 0 || component_y <= 0)
	{
		PyBuffer_Release(&heightmap_buffer);
		return PyErr_Format(PyExc_ValueError, "landscape dimensions must be positive");
	}

	const int32 quads_per_component = sections_per_component * section_size;
	const int32 size_x = component_x * quads_per_component + 1;
	const int32 size_y = component_y * quads_per_component + 1;
	const int64 required_samples = static_cast<int64>(size_x) * size_y;
	const int64 required_bytes = required_samples * sizeof(uint16);

	if (required_samples > MAX_int32 || heightmap_buffer.len < required_bytes)
	{
		PyBuffer_Release(&heightmap_buffer);
		return PyErr_Format(PyExc_Exception, "not enough heightmap data, expecting %lld bytes", required_bytes);
	}

	uint16* data = (uint16*)heightmap_buffer.buf;

	TArray<FLandscapeImportLayerInfo> infos;

#if UEP_LEGACY_ENGINE_MINOR_VERSION < 23
	landscape->Import(FGuid::NewGuid(), 0, 0, size_x - 1, size_y - 1, sections_per_component, section_size, data, nullptr, infos, (ELandscapeImportAlphamapType)layer_type);
#else
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	TArray<uint16> HeightData;
	HeightData.Append(data, static_cast<int32>(required_samples));
	HeightDataPerLayers.Add(FGuid(), HeightData);
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayersInfo;
	MaterialLayersInfo.Add(FGuid(), infos);
	landscape->Import(
		FGuid::NewGuid(),
		0,
		0,
		size_x - 1,
		size_y - 1,
		sections_per_component,
		section_size,
		HeightDataPerLayers,
		nullptr,
		MaterialLayersInfo,
		static_cast<ELandscapeImportAlphamapType>(layer_type),
		TArrayView<const FLandscapeLayer>());
#endif
	PyBuffer_Release(&heightmap_buffer);

	Py_RETURN_NONE;
}

PyObject* py_ue_landscape_export_to_raw_mesh(ue_PyUObject* self, PyObject* args)
{

	ue_py_check(self);

	int lod = 0;

	if (!PyArg_ParseTuple(args, "|i:landscape_import", &lod))
		return nullptr;

	ALandscapeProxy* landscape = ue_py_check_type<ALandscapeProxy>(self);
	if (!landscape)
		return PyErr_Format(PyExc_Exception, "uobject is not a ULandscapeProxy");

#if UEP_LEGACY_ENGINE_MINOR_VERSION > 21
	return PyErr_Format(PyExc_Exception, "MeshDescription struct is still unsupported");;
#else
	FRawMesh raw_mesh;
	if (!landscape->ExportToRawMesh(lod, raw_mesh))
		return PyErr_Format(PyExc_Exception, "unable to export landscape to FRawMesh");

	return py_ue_new_fraw_mesh(raw_mesh);
#endif
}
#endif
