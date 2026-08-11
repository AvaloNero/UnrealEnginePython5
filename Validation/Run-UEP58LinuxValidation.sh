#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
engine_root="${1:-${UE_ENGINE_ROOT:-}}"
configuration="${UEP_CONFIGURATION:-Development}"
max_parallel_actions="${UEP_MAX_PARALLEL_ACTIONS:-4}"

if [[ -z "${engine_root}" ]]; then
    echo "Usage: $0 /path/to/UnrealEngine (or set UE_ENGINE_ROOT)" >&2
    exit 2
fi

engine_root="$(cd "${engine_root}" && pwd -P)"
build_script="${engine_root}/Engine/Build/BatchFiles/Linux/Build.sh"
editor_cmd="${engine_root}/Engine/Binaries/Linux/UnrealEditor-Cmd"
engine_version_file="${engine_root}/Engine/Build/Build.version"

for required_file in "${build_script}" "${editor_cmd}" "${engine_version_file}"; do
    if [[ ! -f "${required_file}" ]]; then
        echo "Required UE5.8 file was not found: ${required_file}" >&2
        exit 2
    fi
done

python3 - "${engine_version_file}" <<'PY'
import json
import pathlib
import sys

version = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
actual = (version["MajorVersion"], version["MinorVersion"])
if actual != (5, 8):
    raise SystemExit(f"Expected Unreal Engine 5.8, found {actual[0]}.{actual[1]}")
PY

run_id="$(date -u +%Y%m%d-%H%M%S)"
run_root="${repo_root}/.build/Validation/Linux/Runs/${run_id}"
stage_root="${run_root}/UEP58Host"
result_root="${repo_root}/.build/Validation/Linux/Results/${run_id}"
plugin_stage="${stage_root}/Plugins/UnrealEnginePython"
foreign_root="${stage_root}/ForeignHost"
foreign_plugin="${foreign_root}/Plugins/UnrealEnginePython"

mkdir -p "${stage_root}" "${result_root}" "${plugin_stage}" "${foreign_plugin}"
cp -a "${repo_root}/Validation/UEP58Host/." "${stage_root}/"
cp "${repo_root}/Validation/UEP58PluginHost/UEP58PluginHost.uproject" "${foreign_root}/"

for directory_name in Source Config Resources; do
    if [[ -d "${repo_root}/${directory_name}" ]]; then
        cp -a "${repo_root}/${directory_name}" "${plugin_stage}/"
        cp -a "${repo_root}/${directory_name}" "${foreign_plugin}/"
    fi
done
cp "${repo_root}/UnrealEnginePython.uplugin" "${plugin_stage}/"
cp "${repo_root}/UnrealEnginePython.uplugin" "${foreign_plugin}/"

project_path="${stage_root}/UEP58Host.uproject"
core_script="${stage_root}/Tests/core_validation.py"
editor_build_log="${result_root}/build-editor.log"
runtime_build_log="${result_root}/build-runtime-foreign.log"

"${build_script}" UEP58HostEditor Linux "${configuration}" \
    "-Project=${project_path}" -NoHotReload -NoMutex -NoUBA \
    "-MaxParallelActions=${max_parallel_actions}" "-Log=${editor_build_log}"

"${build_script}" UnrealGame Linux "${configuration}" \
    "-Project=${foreign_root}/UEP58PluginHost.uproject" \
    "-plugin=${foreign_plugin}/UnrealEnginePython.uplugin" \
    -NoHotReload -NoMutex -NoUBA \
    "-MaxParallelActions=${max_parallel_actions}" "-Log=${runtime_build_log}"

result_paths=()
log_paths=()
for mode in shared standalone; do
    result_path="${result_root}/core-${mode}.json"
    log_path="${result_root}/core-${mode}.log"
    arguments=(
        "${project_path}"
        -unattended
        -nop4
        -NullRHI
        -NoSound
        -NoSplash
        -UTF8Output
        -run=Py
        "${core_script}"
        "-UEPValidationMode=${mode}"
        "-UEPValidationResult=${result_path}"
        "-abslog=${log_path}"
    )
    if [[ "${mode}" == standalone ]]; then
        arguments+=(-DisablePython)
    fi
    "${editor_cmd}" "${arguments[@]}"
    result_paths+=("${result_path}")
    log_paths+=("${log_path}")
done

python3 - "${result_root}" "${engine_version_file}" \
    "${result_paths[0]}" "${log_paths[0]}" \
    "${result_paths[1]}" "${log_paths[1]}" <<'PY'
import json
import pathlib
import sys

result_root = pathlib.Path(sys.argv[1])
engine_version = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
suites = []
for result_name, log_name in ((sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])):
    result_path = pathlib.Path(result_name)
    log_path = pathlib.Path(log_name)
    report = json.loads(result_path.read_text(encoding="utf-8"))
    if report.get("status") != "passed":
        raise SystemExit(f"Validation failed: {result_path}")
    log = log_path.read_text(encoding="utf-8", errors="replace")
    if "UEP_VALIDATION_PASSED" not in log:
        raise SystemExit(f"Validation marker missing: {log_path}")
    for marker in ("Fatal error:", "Assertion failed:", "Unhandled Exception:", "UEP_VALIDATION_FAILED"):
        if marker in log:
            raise SystemExit(f"Validation log contains {marker!r}: {log_path}")
    suites.append(report)

summary = {
    "schema_version": 1,
    "status": "passed",
    "host_platform": "Linux",
    "target_platform": "Linux",
    "engine": f'{engine_version["MajorVersion"]}.{engine_version["MinorVersion"]}.{engine_version["PatchVersion"]}',
    "suites": suites,
}
(result_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(f"UEP 5.8 Linux validation passed: {result_root / 'summary.json'}")
PY
