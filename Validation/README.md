# UEP 5.8 automated validation

`UEP58Host` is a disposable, minimal C++ project used to validate the current
plugin against Unreal Engine 5.8 and the engine-bundled CPython 3.11.

The validation driver copies the project and the current plugin sources into
`.build/Validation/UEP58Host`. It never installs the plugin into the engine and
does not modify Unreal Engine source code.

The desktop-only host disables the unrelated `AndroidFileServer` plugin for
editor, cook and packaged execution so Android deployment tooling is outside
this acceptance contract. Its package step waits for Unreal's global UAT mutex
instead of failing or interfering when another project is cooking. It also uses
UE's loose cooked-package writer so staging does not depend on a transient local
Zen server remaining alive after Cook exits.

## Core release gate

The core gate is complete only when a clean full run returns exit code 0 and its
`summary.json` reports `status: passed`, UE 5.8, Python 3.11 and all six required
suites. The 0.3.0 contract contains 67 required checks: 20 shared core checks,
20 standalone core checks, two isolated exception-recovery checks, five editor
checks and 20 packaged core checks. The core cases exercise UE5 `FField`-based
dynamic class/function generation, `TSet`, non-trivial string returns, thread
diagnostics, animation, UDP receiver lifecycle, parent event overrides and
Enhanced Input callback release. Known exclusions must remain explicitly listed
rather than being silently counted as passes.

The release gate also preserves the independent Python-first Third Person
audit, headless gameplay smoke and packaged gameplay smoke documented in
[`../Demos/README.md`](../Demos/README.md). A green core report alone does not
claim gameplay parity.

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
5. shared and standalone exception-boundary processes whose expected traceback
   is strictly filtered from any unexpected engine/Python errors;
6. full-editor Actor, asset, Sequencer and Slate lifetime tests;
7. Win64 Development cook/package and a packaged-runtime core test;
8. JSON result and log validation, including fatal/assert detection.

Results are written under `.build/Validation/Results/<timestamp>/summary.json`.
Any failed build, missing result, failed test, fatal log marker or non-zero child
process exit causes the driver to return exit code 1. The two exception lanes
may return 0 or 1 because UE commandlets count a handled logged Python exception
as an error; their JSON recovery result and every `Log*: Error:` line are still
validated explicitly.

Packaged success is not inferred from the JSON marker alone. The executable
must return exit code 0 after Unreal tears down its object system and UEP-owned
CPython. This caught a `BlendSpace` reflected-struct shallow copy that passed all
assertions but corrupted memory during process shutdown.

Each result directory keeps the UBT log, the complete AutomationTool log, the
per-suite Unreal logs and machine-readable JSON reports. A successful full run
contains six suites:

| Suite | Interpreter ownership | Required checks |
| --- | --- | --- |
| core/shared | Epic `PythonScriptPlugin` | reflection, values, `TSet`, animation, UDP lifecycle, dynamic classes/functions, Enhanced Input callback lifetime, delegates and GC |
| core/standalone | UEP | the same core contract with `-DisablePython` |
| exception/shared | Epic `PythonScriptPlugin` | reflected callback exception logging and subsequent-call recovery |
| exception/standalone | UEP | the same exception boundary with UEP-owned Python |
| editor/shared | Epic `PythonScriptPlugin` | Actor lifecycle, asset/Level Sequence save/load, Sequencer operations and Slate item ownership |
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

## Linux lane and platform readiness

The Win64 host can record whether another UBT platform has the required UE5.8
SDK without installing or changing that SDK:

```powershell
.\Validation\Test-UEP58PlatformReadiness.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Platform Linux
```

Add `-Strict` when an invalid platform should fail CI. A UE5.8 Linux machine can
run `Validation/Run-UEP58LinuxValidation.sh`; the matching GitHub workflow
targets a self-hosted runner labelled `linux` and `unreal-engine-5.8`. It builds
the Editor and foreign runtime plugin targets, then executes both interpreter
ownership modes. Merely defining this lane is not a Linux support claim: a
green `summary.json` artifact is required.

## Current intentional exclusions

The suite records, but does not fail for, these known limitations:

- Unreal's unattended asset deletion reference scan is not used; the driver
  removes only the two exact disposable Material/Level Sequence asset files
  after the editor process exits;
- the current local UE5.8 install lacks the required Linux
  `v26_clang-20.1.8-rockylinux8` SDK, so only Win64 is release-validated.

Runtime exclusions stay visible in suite JSON; platform availability is kept in
the separate readiness JSON until a real Linux lane result exists.
