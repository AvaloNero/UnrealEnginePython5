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

    # Epic's -ExecutePythonScript entry point does not populate sys.argv with
    # the engine command line. Shared-mode scripts can query it through the
    # built-in unreal module instead.
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


mode = command_line_value("UEPValidationMode", "unknown")
result_path_value = command_line_value("UEPValidationResult")
if not result_path_value:
    raise RuntimeError("-UEPValidationResult=<path> is required")

result_path = pathlib.Path(result_path_value)
results = []
context = {}


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


def validate_versions_and_mode():
    check(sys.version_info[:2] == (3, 11), sys.version)
    check(
        (ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION) == (5, 8),
        (ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION),
    )
    epic_unreal_loaded = "unreal" in sys.modules
    if mode == "shared":
        check(epic_unreal_loaded, "Epic's unreal module was not loaded in shared mode")
    else:
        check(not epic_unreal_loaded, f"Epic's unreal module unexpectedly loaded in {mode} mode")


def create_fixture():
    fixture_class = ue.find_class("UEPValidationObject")
    check(fixture_class is not None, "UEPValidationObject class was not found")
    check(fixture_class.get_name() == "UEPValidationObject", fixture_class.get_name())

    from unreal_engine.classes import UEPValidationObject

    fixture = UEPValidationObject()
    check(fixture is not None, "Could not construct UEPValidationObject")
    context["fixture"] = fixture
    context["fixture_type"] = UEPValidationObject


def validate_property_inventory():
    properties = set(context["fixture"].properties())
    expected = {
        "BoolValue",
        "Int32Value",
        "Int64Value",
        "FloatValue",
        "DoubleValue",
        "StringValue",
        "NameValue",
        "TextValue",
        "VectorValue",
        "RotatorValue",
        "TransformValue",
        "IntArray",
        "StringSet",
        "StringIntMap",
        "StructValue",
        "ChoiceValue",
        "ObjectValue",
        "OnSignal",
    }
    missing = sorted(expected - properties)
    check(not missing, f"Missing reflected properties: {missing}")


def validate_scalar_properties():
    fixture = context["fixture"]
    fixture.BoolValue = True
    fixture.Int32Value = -123456
    fixture.Int64Value = 5_000_000_123
    fixture.FloatValue = 1.25
    fixture.DoubleValue = 98765.125
    fixture.StringValue = "UEP 5.8"
    fixture.NameValue = "ValidationName"
    fixture.TextValue = "ValidationText"

    check(fixture.BoolValue is True, fixture.BoolValue)
    check(fixture.Int32Value == -123456, fixture.Int32Value)
    check(fixture.Int64Value == 5_000_000_123, fixture.Int64Value)
    check(abs(fixture.FloatValue - 1.25) < 0.0001, fixture.FloatValue)
    check(abs(fixture.DoubleValue - 98765.125) < 0.000001, fixture.DoubleValue)
    check(fixture.StringValue == "UEP 5.8", fixture.StringValue)
    check(fixture.NameValue == "ValidationName", fixture.NameValue)
    check(fixture.TextValue == "ValidationText", fixture.TextValue)


def validate_math_properties():
    from unreal_engine import FRotator, FTransform, FVector

    fixture = context["fixture"]
    fixture.VectorValue = FVector(1.5, -2.0, 3.25)
    fixture.RotatorValue = FRotator(10.0, 20.0, 30.0)
    fixture.TransformValue = FTransform()

    check(fixture.VectorValue == FVector(1.5, -2.0, 3.25), fixture.VectorValue)
    rotation = fixture.RotatorValue
    check(abs(rotation.roll - 10.0) < 0.001, rotation)
    check(abs(rotation.pitch - 20.0) < 0.001, rotation)
    check(abs(rotation.yaw - 30.0) < 0.001, rotation)
    transform = fixture.TransformValue
    check(transform.translation == FVector(0.0, 0.0, 0.0), transform.translation)
    check(transform.scale == FVector(1.0, 1.0, 1.0), transform.scale)


def validate_containers():
    fixture = context["fixture"]
    fixture.IntArray = [3, 1, 4, 1, 5]
    fixture.StringIntMap = {"one": 1, "two": 2, "answer": 42}
    check(fixture.IntArray == [3, 1, 4, 1, 5], fixture.IntArray)
    check(fixture.StringIntMap == {"one": 1, "two": 2, "answer": 42}, fixture.StringIntMap)


def validate_struct_and_enum():
    from unreal_engine import FVector
    from unreal_engine.enums import EUEPValidationChoice
    from unreal_engine.structs import UEPValidationStruct

    fixture = context["fixture"]
    value = UEPValidationStruct(Count=7, Label="fixture", Vector=FVector(4.0, 5.0, 6.0))
    fixture.StructValue = value
    fixture.ChoiceValue = EUEPValidationChoice.Second

    reflected = fixture.StructValue
    check(reflected.Count == 7, reflected.Count)
    check(reflected.Label == "fixture", reflected.Label)
    check(reflected.Vector == FVector(4.0, 5.0, 6.0), reflected.Vector)
    check(fixture.ChoiceValue == EUEPValidationChoice.Second, fixture.ChoiceValue)

    echoed = fixture.call_function("EchoStruct", value)
    check(echoed.Count == 7, echoed.Count)
    check(echoed.Label == "fixture", echoed.Label)
    check(echoed.Vector == FVector(4.0, 5.0, 6.0), echoed.Vector)


def validate_object_property():
    fixture = context["fixture"]
    other = context["fixture_type"]()
    fixture.ObjectValue = other
    check(fixture.ObjectValue == other, "UObject property did not preserve identity")
    fixture.ObjectValue = None
    check(fixture.ObjectValue is None, fixture.ObjectValue)


def validate_function_calls():
    fixture = context["fixture"]
    check(fixture.call_function("AddIntegers", 20, 22) == 42, "AddIntegers returned the wrong value")
    outputs = fixture.call_function("ComputeOutputs", 21)
    check(outputs == (True, 42, "Value=21"), outputs)


def validate_delegate_binding():
    fixture = context["fixture"]
    received = []

    def callback(value):
        received.append(value)

    fixture.bind_event("OnSignal", callback)
    fixture.call_function("BroadcastSignal", 77)
    check(received == [77], received)
    fixture.unbind_event("OnSignal", callback)
    fixture.call_function("BroadcastSignal", 88)
    check(received == [77], received)


def validate_negative_conversion():
    fixture = context["fixture"]
    try:
        fixture.Int32Value = "not an integer"
    except (TypeError, ValueError, RuntimeError, Exception):
        return
    raise AssertionError("Invalid string assignment to Int32Value did not fail")


def validate_housekeeper_gc():
    transient = context["fixture_type"]()
    transient.StringValue = "gc"
    del transient
    gc.collect()
    garbaged = ue.py_gc()
    check(isinstance(garbaged, int), type(garbaged).__name__)
    check(garbaged >= 0, garbaged)


run_case("version_and_interpreter_mode", validate_versions_and_mode)
run_case("class_lookup_and_construction", create_fixture)
run_case("property_inventory", validate_property_inventory)
run_case("scalar_properties", validate_scalar_properties)
run_case("math_properties", validate_math_properties)
run_case("array_and_map_properties", validate_containers)
run_case("struct_enum_and_struct_function", validate_struct_and_enum)
run_case("object_property", validate_object_property)
run_case("function_return_and_out_params", validate_function_calls)
run_case("dynamic_multicast_delegate", validate_delegate_binding)
run_case("invalid_property_conversion", validate_negative_conversion)
run_case("housekeeper_gc", validate_housekeeper_gc)

failed = [item for item in results if item["status"] == "failed"]
report = {
    "schema_version": 1,
    "suite": "core",
    "mode": mode,
    "status": "failed" if failed else "passed",
    "python_version": sys.version.split()[0],
    "engine_version": [ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION],
    "epic_unreal_module_loaded": "unreal" in sys.modules,
    "passed": len(results) - len(failed),
    "failed": len(failed),
    "known_skips": [
        "dynamic Python-generated UClass/UFunction synthesis",
        "TSet property marshalling",
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
