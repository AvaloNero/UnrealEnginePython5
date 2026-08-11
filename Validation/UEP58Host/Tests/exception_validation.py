import json
import pathlib
import re
import sys
import traceback

import unreal_engine as ue
from unreal_engine.classes import UEPValidationObject


def command_line_value(name, default=None):
    prefix = f"-{name}="
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):]

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

results = []


def run_case(name, function):
    try:
        function()
        results.append({"name": name, "status": "passed"})
    except Exception as exc:
        results.append(
            {
                "name": name,
                "status": "failed",
                "error": f"{type(exc).__name__}: {exc}",
                "traceback": traceback.format_exc(),
            }
        )


class UEP58ExceptionValidationObject(UEPValidationObject):
    def MaybeFail(self, value: int) -> int:
        if value < 0:
            raise RuntimeError("UEP_EXPECTED_DYNAMIC_EXCEPTION")
        return value + 10


def validate_exception_boundary_and_recovery():
    instance = UEP58ExceptionValidationObject()
    try:
        instance.call_function("MaybeFail", -1)
    except Exception:
        # Some invocation paths propagate the Python exception, while native
        # ProcessEvent paths log it and return the property's default value.
        pass

    recovered = instance.call_function("MaybeFail", 32)
    if recovered != 42:
        raise AssertionError(f"dynamic function did not recover: {recovered!r}")


run_case("dynamic_exception_boundary_and_recovery", validate_exception_boundary_and_recovery)

failed = [item for item in results if item["status"] == "failed"]
report = {
    "schema_version": 1,
    "suite": "exception_boundary",
    "mode": mode,
    "status": "failed" if failed else "passed",
    "python_version": sys.version.split()[0],
    "engine_version": [ue.ENGINE_MAJOR_VERSION, ue.ENGINE_MINOR_VERSION],
    "passed": len(results) - len(failed),
    "failed": len(failed),
    "tests": results,
}

result_path = pathlib.Path(result_path_value)
result_path.parent.mkdir(parents=True, exist_ok=True)
result_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

if failed:
    ue.log_error(f"UEP_EXCEPTION_VALIDATION_FAILED {result_path}")
else:
    ue.log(f"UEP_EXCEPTION_VALIDATION_PASSED {result_path}")
