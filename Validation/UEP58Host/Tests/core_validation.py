import gc
import json
import pathlib
import re
import sys
import threading
import time
import traceback
import weakref

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


def validate_set_properties():
    fixture = context["fixture"]
    fixture.StringSet = {"alpha", "beta", "gamma"}
    check(isinstance(fixture.StringSet, set), type(fixture.StringSet).__name__)
    check(fixture.StringSet == {"alpha", "beta", "gamma"}, fixture.StringSet)

    fixture.StringSet = frozenset({"delta", "epsilon"})
    check(fixture.StringSet == {"delta", "epsilon"}, fixture.StringSet)

    fixture.StringSet = set()
    check(fixture.StringSet == set(), fixture.StringSet)


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


def validate_animation_wrappers():
    from unreal_engine.classes import AnimSequence, BlendSpace

    sequence = AnimSequence()
    check(sequence.anim_get_skeleton() is None, "Transient AnimSequence unexpectedly has a skeleton")

    blend_space = BlendSpace()
    parameter = blend_space.get_blend_parameter(0)
    check(parameter is not None, "BlendSpace parameter wrapper was not returned")
    blend_space.set_blend_parameter(0, parameter)
    round_trip = blend_space.get_blend_parameter(0)
    check(round_trip is not None, "BlendSpace parameter round trip failed")


def validate_udp_socket_lifecycle():
    udp_socket = ue.FSocket("UEP58Validation", "127.0.0.1", 0, 4096)
    check("UEP58Validation" in str(udp_socket), str(udp_socket))
    udp_socket.start_receiver()
    time.sleep(0.15)
    udp_socket.stop_receiver()
    udp_socket.close()
    check("closed" in str(udp_socket), str(udp_socket))
    udp_socket.close()

    try:
        udp_socket.start_receiver()
    except RuntimeError:
        pass
    else:
        raise AssertionError("a closed UDP socket restarted its receiver")


def validate_dynamic_class_generation():
    namespace = {}
    exec(
        """
from unreal_engine.classes import (
    FloatProperty,
    IntProperty,
    StrProperty,
    UEPValidationObject,
)


class UEP58DynamicValidationObject(UEPValidationObject):
    Speed = FloatProperty
    Numbers = [IntProperty]
    Tags = {StrProperty}
    Labels = {StrProperty: IntProperty}
    Peer = UEPValidationObject

    def __init__(self):
        self.Speed = 125.5
        self.Numbers = [2, 3, 5, 7]
        self.Tags = {"dynamic", "python"}
        self.Labels = {"answer": 42}

    def AddDynamicValues(self, left: int, right: int) -> int:
        return left + right

    def EchoDynamicLabel(self, value: str) -> str:
        return value + "::python"

    def TripleOverrideableValue(self, Input: int) -> int:
        return Input * 3

    TripleOverrideableValue.override = "OverrideableValue"
""",
        namespace,
    )

    dynamic_type = namespace["UEP58DynamicValidationObject"]
    check(dynamic_type.get_name() == "UEP58DynamicValidationObject", dynamic_type.get_name())
    check(
        ue.find_class("UEP58DynamicValidationObject") == dynamic_type,
        "The generated class was not registered for reflected lookup",
    )

    cdo = dynamic_type.get_cdo()
    instance = dynamic_type()
    reflected_properties = set(instance.properties())
    expected_properties = {"Speed", "Numbers", "Tags", "Labels", "Peer"}
    check(
        expected_properties <= reflected_properties,
        sorted(expected_properties - reflected_properties),
    )

    check(abs(cdo.Speed - 125.5) < 0.0001, cdo.Speed)
    check(cdo.Numbers == [2, 3, 5, 7], cdo.Numbers)
    check(cdo.Tags == {"dynamic", "python"}, cdo.Tags)
    check(cdo.Labels == {"answer": 42}, cdo.Labels)
    check(abs(instance.Speed - 125.5) < 0.0001, instance.Speed)
    check(instance.Numbers == [2, 3, 5, 7], instance.Numbers)
    check(instance.Tags == {"dynamic", "python"}, instance.Tags)
    check(instance.Labels == {"answer": 42}, instance.Labels)

    peer = context["fixture_type"]()
    instance.Peer = peer
    check(instance.Peer == peer, "Generated UObject property did not preserve identity")
    check(
        instance.call_function("AddDynamicValues", 19, 23) == 42,
        "Generated UFunction returned the wrong value",
    )
    check(
        instance.call_function("OverrideableValue", 14) == 42,
        "Generated UFunction did not override the parent BlueprintNativeEvent",
    )

    context["dynamic_type"] = dynamic_type
    context["dynamic_instance"] = instance


def validate_dynamic_function_stress():
    instance = context["dynamic_instance"]

    for value in range(512):
        check(
            instance.call_function("AddDynamicValues", value, 7) == value + 7,
            f"integer stress call failed at {value}",
        )

    for value in range(256):
        label = f"lifecycle-{value}-" + ("x" * (value % 31))
        check(
            instance.call_function("EchoDynamicLabel", label) == label + "::python",
            f"string stress call failed at {value}",
        )

def validate_dynamic_reload_contract():
    original_type = context["dynamic_type"]
    namespace = {}
    try:
        exec(
            "from unreal_engine.classes import UEPValidationObject\n"
            "class UEP58DynamicValidationObject(UEPValidationObject):\n"
            "    pass\n",
            namespace,
        )
    except RuntimeError as exc:
        check("already registered" in str(exc), str(exc))
    else:
        raise AssertionError("duplicate dynamic class registration unexpectedly succeeded")

    check(
        ue.find_class("UEP58DynamicValidationObject") == original_type,
        "duplicate registration changed the live dynamic class",
    )
    check(
        context["dynamic_instance"].call_function("AddDynamicValues", 20, 22) == 42,
        "the original dynamic class stopped working after duplicate rejection",
    )


def validate_game_thread_diagnostic():
    check(ue.is_in_game_thread() is True, "validation script is not on Unreal's game thread")
    observations = []
    failures = []

    def probe():
        try:
            observations.append(ue.is_in_game_thread())
        except Exception as exc:
            failures.append(f"{type(exc).__name__}: {exc}")

    worker = threading.Thread(target=probe, name="UEPValidationThread")
    worker.start()
    worker.join(timeout=5.0)
    check(not worker.is_alive(), "thread diagnostic worker did not finish")
    check(not failures, failures)
    check(observations == [False], observations)


def validate_enhanced_input_binding_lifecycle():
    from unreal_engine.classes import EnhancedInputComponent, InputAction

    component = EnhancedInputComponent()
    action = InputAction()
    received = []

    class Callback:
        def __call__(self, value):
            received.append(value)

    callback = Callback()
    callback_ref = weakref.ref(callback)
    handle = component.bind_enhanced_action(action, 1, callback)

    check(isinstance(handle, int) and handle > 0, handle)
    check(component.get_enhanced_action_binding_count() == 1, "Binding was not registered")
    del callback
    gc.collect()
    check(callback_ref() is not None, "Bound callback was released too early")
    check(component.remove_enhanced_action_binding(handle), "Binding handle was not removed")
    check(component.get_enhanced_action_binding_count() == 0, "Binding remained after removal")
    gc.collect()
    check(callback_ref() is None, "Removed binding retained its Python callback")
    check(not component.remove_enhanced_action_binding(handle), "Removed handle succeeded twice")


def validate_delegate_binding():
    fixture = context["fixture"]
    received = []

    class Callback:
        def __call__(self, value):
            received.append(value)

    callback = Callback()
    callback_ref = weakref.ref(callback)
    fixture.bind_event("OnSignal", callback)
    fixture.call_function("BroadcastSignal", 77)
    check(received == [77], received)
    fixture.unbind_event("OnSignal", callback)
    fixture.call_function("BroadcastSignal", 88)
    check(received == [77], received)
    del callback
    gc.collect()
    check(callback_ref() is None, "Unbound multicast delegate retained its Python callback")


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
run_case("set_properties", validate_set_properties)
run_case("struct_enum_and_struct_function", validate_struct_and_enum)
run_case("object_property", validate_object_property)
run_case("function_return_and_out_params", validate_function_calls)
run_case("animation_wrapper_round_trip", validate_animation_wrappers)
run_case("udp_socket_receiver_lifecycle", validate_udp_socket_lifecycle)
run_case("dynamic_uclass_fproperty_and_ufunction", validate_dynamic_class_generation)
run_case("dynamic_function_stress", validate_dynamic_function_stress)
run_case("dynamic_class_reload_contract", validate_dynamic_reload_contract)
run_case("game_thread_diagnostic", validate_game_thread_diagnostic)
run_case("enhanced_input_binding_lifecycle", validate_enhanced_input_binding_lifecycle)
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
    "known_skips": [],
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
