import gc
import json
import pathlib
import re
import sys
import time
import traceback

import unreal_engine as ue


def command_line_value(name, default=None):
    prefix = f"-{name}="
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):]

    # -ExecutePythonScript runs after engine startup and only exposes its own
    # script path through sys.argv. Read the original UE command line here.
    try:
        import unreal

        command_line = unreal.SystemLibrary.get_command_line()
    except (ImportError, AttributeError):
        command_line = ""

    match = re.search(
        rf'(?:^|\s)-{re.escape(name)}=(?:"([^"]*)"|(\S+))',
        command_line,
        flags=re.IGNORECASE,
    )
    if match:
        return match.group(1) if match.group(1) is not None else match.group(2)
    return default


result_path_value = command_line_value("UEPValidationResult")
if not result_path_value:
    raise RuntimeError("-UEPValidationResult=<path> is required")

result_path = pathlib.Path(result_path_value)
results = []


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def run_case(name, function):
    started = time.perf_counter()
    try:
        function()
        results.append(
            {
                "name": name,
                "status": "passed",
                "duration_seconds": round(time.perf_counter() - started, 6),
            }
        )
    except Exception as exc:
        results.append(
            {
                "name": name,
                "status": "failed",
                "duration_seconds": round(time.perf_counter() - started, 6),
                "error": f"{type(exc).__name__}: {exc}",
                "traceback": traceback.format_exc(),
            }
        )


def validate_shared_editor_interpreter():
    check(sys.version_info[:2] == (3, 11), sys.version)
    check((ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION) == (5, 8), "Unexpected engine version")
    check("unreal" in sys.modules, "Epic's unreal module was not loaded")


def validate_actor_lifecycle():
    from unreal_engine import FVector
    from unreal_engine.classes import UEPValidationActor

    world = ue.get_editor_world()
    check(world is not None, "Editor world was unavailable")
    actor = world.actor_spawn(UEPValidationActor, FVector(10.0, 20.0, 30.0))
    check(actor is not None, "Could not spawn UEPValidationActor")
    ue.allow_actor_script_execution_in_editor(True)
    try:
        check(actor.Counter == 0, actor.Counter)
        check(actor.call_function("IncrementCounter", 5) == 5, actor.Counter)
        check(actor.Counter == 5, actor.Counter)
        check(actor.get_actor_location() == FVector(10.0, 20.0, 30.0), actor.get_actor_location())
    finally:
        ue.allow_actor_script_execution_in_editor(False)
        actor.actor_destroy()


def validate_asset_lifecycle():
    from unreal_engine.classes import Material

    material = Material()
    material.set_name("M_UEP58Validation")
    package_name = "/Game/UEPValidation/M_UEP58Validation"
    material.save_package(package_name)
    asset_path = f"{package_name}.M_UEP58Validation"
    loaded = ue.get_asset(asset_path)
    check(loaded is not None, f"Could not load saved asset {asset_path}")


def validate_slate_item_ownership():
    from unreal_engine import SPythonComboBox, SPythonListView, SPythonTreeView, STextBlock

    class Item:
        pass

    items = [Item(), Item(), Item()]
    baseline = [sys.getrefcount(item) for item in items]

    def assert_owned_once():
        current = [sys.getrefcount(item) for item in items]
        check(current == [value + 1 for value in baseline], (baseline, current))

    def assert_released():
        gc.collect()
        current = [sys.getrefcount(item) for item in items]
        check(current == baseline, (baseline, current))

    list_view = SPythonListView(
        list_items_source=items,
        on_generate_row=lambda item: STextBlock(text="list"),
    )
    assert_owned_once()
    list_view.update_item_source_list(items)
    assert_owned_once()
    del list_view
    assert_released()

    tree_view = SPythonTreeView(
        tree_items_source=items,
        on_generate_row=lambda item: STextBlock(text="tree"),
        on_get_children=lambda item: [],
    )
    assert_owned_once()
    del tree_view
    assert_released()

    combo_box = SPythonComboBox(
        options_source=items,
        on_generate_widget=lambda item: STextBlock(text="combo"),
    )
    assert_owned_once()
    del combo_box
    assert_released()


run_case("shared_editor_interpreter", validate_shared_editor_interpreter)
run_case("actor_spawn_call_and_destroy", validate_actor_lifecycle)
run_case("asset_create_save_and_load", validate_asset_lifecycle)
run_case("slate_item_reference_ownership", validate_slate_item_ownership)

failed = [item for item in results if item["status"] == "failed"]
report = {
    "schema_version": 1,
    "suite": "editor",
    "mode": "shared",
    "status": "failed" if failed else "passed",
    "python_version": sys.version.split()[0],
    "engine_version": [ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION],
    "epic_unreal_module_loaded": "unreal" in sys.modules,
    "passed": len(results) - len(failed),
    "failed": len(failed),
    "known_skips": [
        "delete_asset unattended reference scan; the driver removes the isolated test asset after editor exit",
    ],
    "tests": results,
}

result_path.parent.mkdir(parents=True, exist_ok=True)
result_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

if failed:
    for failure in failed:
        ue.log_error(f"UEP_VALIDATION_CASE_FAILED {failure['name']}: {failure['error']}")
    ue.log_error(f"UEP_VALIDATION_FAILED {result_path}")
else:
    ue.log(f"UEP_VALIDATION_PASSED {result_path}")
