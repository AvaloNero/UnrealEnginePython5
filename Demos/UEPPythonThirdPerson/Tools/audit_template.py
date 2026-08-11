"""Inventory the unchanged UE 5.8 Third Person Blueprint reference assets."""

import json
from pathlib import Path
import sys
import traceback

import unreal_engine as ue
from unreal_engine.classes import Blueprint, World


RESULT_PREFIX = "-UEPTemplateAuditResult="
BLUEPRINTS = (
    (
        "character",
        "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter",
        "Python UEPThirdPersonCharacter",
    ),
    (
        "player_controller",
        "/Game/ThirdPerson/Blueprints/BP_ThirdPersonPlayerController",
        "Python UEPThirdPersonPlayerController",
    ),
    (
        "game_mode",
        "/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode",
        "Python UEPThirdPersonGameMode",
    ),
    (
        "animation",
        "/Game/Mannequins/Anims/Unarmed/ABP_Unarmed",
        "Python locomotion state selector plus retained animation assets",
    ),
)


def command_line_value(prefix):
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):].strip('"')
    return None


def object_name(value):
    try:
        return value.get_name()
    except Exception:
        return str(value)


def graph_description(graph):
    nodes = []
    for node in graph.Nodes:
        try:
            title = node.node_get_title()
        except Exception:
            title = object_name(node)
        nodes.append(
            {
                "name": object_name(node),
                "class": node.get_class().get_name(),
                "title": title,
            }
        )
    return {
        "name": graph.get_name(),
        "node_count": len(nodes),
        "nodes": nodes,
    }


def blueprint_description(role, path, migration_target):
    blueprint = ue.load_object(Blueprint, path)
    if blueprint is None or not blueprint.is_valid():
        raise RuntimeError("failed to load " + path)

    generated_class = blueprint.GeneratedClass
    cdo = generated_class.get_cdo()
    components = []
    try:
        components = [
            {
                "name": component.get_name(),
                "class": component.get_class().get_name(),
            }
            for component in cdo.get_components()
        ]
    except Exception:
        # Non-Actor Blueprints such as AnimBlueprints do not own components.
        pass

    graphs = [graph_description(graph) for graph in ue.blueprint_get_all_graphs(blueprint)]
    return {
        "role": role,
        "path": path,
        "parent_class": blueprint.ParentClass.get_name(),
        "generated_class": generated_class.get_name(),
        "graph_count": len(graphs),
        "node_count": sum(graph["node_count"] for graph in graphs),
        "graphs": graphs,
        "components": components,
        "reflected_properties": sorted(cdo.properties()),
        "migration_target": migration_target,
    }


result_value = command_line_value(RESULT_PREFIX)
if not result_value:
    raise RuntimeError(RESULT_PREFIX + "<path> is required")

result_path = Path(result_value)
try:
    assets = [blueprint_description(*entry) for entry in BLUEPRINTS]
    level = ue.load_object(World, "/Game/ThirdPerson/Lvl_ThirdPerson")
    if level is None or not level.is_valid():
        raise RuntimeError("failed to load the reference Third Person map")

    report = {
        "schema_version": 1,
        "status": "passed",
        "engine_version": [ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION],
        "blueprints": assets,
        "map": {
            "path": "/Game/ThirdPerson/Lvl_ThirdPerson",
            "asset_class": level.get_class().get_name(),
            "migration_decision": "retained unchanged as level and world-partition data",
        },
        "summary": {
            "blueprint_count": len(assets),
            "graph_count": sum(item["graph_count"] for item in assets),
            "node_count": sum(item["node_count"] for item in assets),
        },
    }
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    ue.log("UEP_TEMPLATE_AUDIT_PASSED")
except Exception:
    details = traceback.format_exc()
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(
        json.dumps({"schema_version": 1, "status": "failed", "error": details}, indent=2),
        encoding="utf-8",
    )
    ue.log_error(details)
    ue.log_error("UEP_TEMPLATE_AUDIT_FAILED")
    raise
