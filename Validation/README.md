# UEP 5.8 automated validation

`UEP58Host` is a disposable, minimal C++ project used to validate the current
plugin against Unreal Engine 5.8 and the engine-bundled CPython 3.11.

The validation driver copies the project and the current plugin sources into
`.build/Validation/UEP58Host`. It never installs the plugin into the engine and
does not modify Unreal Engine source code.

The desktop-only host disables the unrelated `AndroidFileServer` plugin for
editor, cook and packaged execution so Android deployment tooling is outside
this acceptance contract. Its package step waits for Unreal's global UAT mutex
instead of failing or interfering when another project is cooking.

## 0.1.0 release gate

Version 0.1.0 is complete only when a clean full run returns exit code 0 and
its `summary.json` reports `status: passed`, UE 5.8, Python 3.11 and all four
required suites. The current contract contains 40 required checks: 12 shared
core checks, 12 standalone core checks, four editor checks and 12 packaged
core checks. Known exclusions must remain explicitly listed rather than being
silently counted as passes.

The validation output is local evidence and is intentionally ignored by Git.
Source changes must be followed by a new run; an older green result is not a
substitute for testing the changed source.

## Full acceptance run

From the repository root:

```powershell
.\Validation\Run-UEP58Validation.ps1 -EngineRoot F:\UnrealEngine
```

The full run performs:

1. `UEP58HostEditor` compilation and plugin module linking;
2. `UEP58Host` Game compilation/linking as part of packaging;
3. core reflection tests while sharing Epic's Python interpreter;
4. the same core tests with `-DisablePython`, so UEP owns CPython 3.11;
5. full-editor Actor, asset and Slate lifetime tests;
6. Win64 Development cook/package and a packaged-runtime core test;
7. JSON result and log validation, including fatal/assert detection.

Results are written under `.build/Validation/Results/<timestamp>/summary.json`.
Any failed build, missing result, failed test, fatal log marker or non-zero child
process exit causes the driver to return exit code 1.

Each result directory keeps the UBT log, the complete AutomationTool log, the
per-suite Unreal logs and machine-readable JSON reports. A successful full run
contains four suites:

| Suite | Interpreter ownership | Required checks |
| --- | --- | --- |
| core/shared | Epic `PythonScriptPlugin` | reflection, values, containers, structs, functions, delegates and GC |
| core/standalone | UEP | the same core contract with `-DisablePython` |
| editor/shared | Epic `PythonScriptPlugin` | Actor lifecycle, asset save/load and Slate item ownership |
| core/packaged | UEP | cooked executable startup and the full core contract |

All modes require the engine-bundled CPython 3.11. The packaged suite also
requires the `UEP_PACKAGED_VALIDATION_PASSED` runtime marker.

## Faster development run

Skip packaging and reuse the existing staging directory:

```powershell
.\Validation\Run-UEP58Validation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -SkipPackage `
    -Incremental
```

Omit `-Incremental` for a clean staging copy. Clean-up is restricted to the
script-marked `.build/Validation/UEP58Host` directory.

The quick run still compiles the Runtime plugin through UBT's foreign-plugin
mode. While iterating only on Python tests, that compile can also be skipped:

```powershell
.\Validation\Run-UEP58Validation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -SkipPackage `
    -SkipRuntimeCompile `
    -Incremental
```

## Current intentional exclusions

The suite records, but does not fail for, these known limitations:

- dynamic Python-generated `UClass`/`UFunction` synthesis is disabled;
- generic `TSet` property marshalling is not currently implemented;
- Unreal's unattended asset deletion reference scan is not used; the driver
  removes only the exact disposable asset file after the editor process exits.

These exclusions must stay visible in the JSON results until the features are
implemented and their tests are promoted to required cases.
