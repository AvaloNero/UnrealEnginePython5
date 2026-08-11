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
suites. The 0.4.0 contract contains 70 required checks: 21 shared core checks,
21 standalone core checks, two isolated exception-recovery checks, five editor
checks and 21 packaged core checks. The core cases exercise UE5 `FField`-based
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

Release-candidate result `20260812-043408` passed all six suites and 70/70
checks against UE 5.8.0 and CPython 3.11.8. Its full Development BuildCookRun
reported `BUILD SUCCESSFUL` and ExitCode 0, and its hash-recorded stable package
completed the packaged core suite without fatal or error diagnostics.

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

Timestamped results keep per-run logs, JSON reports and the SHA-256 of the
tested game executable. The package itself is archived directly to the guarded
fixed path `.build/Validation/Package`; Windows Firewall keys unpackaged
listeners by their complete executable path, so later runs do not appear as new
`UEP58Host` applications. Preconfigure TCP/UDP inbound **Block** rules for
`.build/Validation/Package/Windows/UEP58Host/Binaries/Win64/UEP58Host.exe`, or
choose **Cancel** if Windows asks once. The loopback-only UDP lifecycle test does
not need public/private inbound access. Do not disable Firewall notifications
globally.

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
script-marked `.build/Validation/UEP58Host` and
`.build/Validation/Package` directories.

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

## Lyra 0.4.0 source and content gates

The Git Lyra sample under a source-engine checkout intentionally omits content.
Check a candidate project without changing it:

```powershell
.\Validation\Test-UEP58LyraReadiness.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject
```

The report separates `source_ready` from `content_ready`. Add `-Strict` only
when missing Marketplace content should fail the calling job. A content-ready
project must contain the base GameData, front-end/editor maps, a gameplay map
and root GameFeatureData assets for every Lyra Game Feature plugin. Critical
files must also carry a valid Unreal package header; zero-byte/place-holder
files cannot satisfy the gate.

The source-only compatibility lane is independently runnable:

```powershell
.\Validation\Run-UEP58LyraSourceValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\UnrealEngine\Samples\Games\Lyra\Lyra.uproject
```

It copies Lyra source/config/plugin code to the script-marked
`.build/LyraValidation/Stage/Lyra`, injects the current UEP source and
`UEPLyraBridge`, and disables content-backed Lyra startup only in that disposable
stage. It then builds `LyraEditor`, runs `/Engine/Maps/Entry` in Standalone, and
runs a second content-free dedicated server that must listen on its requested
UDP port and exit through `unreal_engine.request_exit()`. Passing proves
compiler/UHT compatibility, module loading, game-thread bridge access and clean
headless shutdown; it explicitly records
`source_content_gate: not_claimed` and is not a substitute for real Lyra
gameplay, networking or packaging.

Current evidence is clean source result `20260811-165230`, final source
regression `20260811-183357` (Standalone plus dedicated-server graceful exit),
full generic regression `20260811-184425` (70/70 including packaged runtime),
and full-content readiness result `20260812-005352` (`status: ready`, UE 5.8.0,
CPython 3.11.8, all five Game Features and all critical packages valid). The
reference project is `F:\LyraStarterGame\LyraStarterGame.uproject`; it remains
external and is never modified by validation.

### Full-content acceptance lane

Point the full driver at a complete project outside both this repository and
the Unreal Engine source tree:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode All
```

The driver first invokes strict readiness and refuses to create its disposable
stage unless all required assets/maps and root GameFeatureData assets exist. It
then copies the project to marker-protected
`.build/LyraValidation/Full/Stage/Lyra`, injects UEP and `UEPLyraBridge`, and
disables the unrelated desktop `AndroidFileServer` only in that disposable
stage. After compiling the complete Editor target, it disables the test-only
`ShooterTests` and `RuntimeTests` roots for gameplay/package execution so the
runtime evidence covers production Lyra features. ShooterTests content is still
staged and validated by readiness. Runtime launches pin `-culture=en` because
UE5.8's startup Core smoke tests compare source-language UnifiedError strings;
on a localized Windows culture those engine tests otherwise emit false
`LogAutomationTest: Error` diagnostics. The strict runtime error gate is not
relaxed. The driver runs these gates without changing the reference project:

1. warning/error-free `LyraEditor` build;
2. real `L_Expanse` Standalone Experience with active `ShooterCore`, registered
   content-only `ShooterMaps`, local pawn, PlayerState, Enhanced Input and ASC;
3. a dedicated server/client pair proving remote connection, server authority,
   client `AutonomousProxy`, replicated PlayerState and ASC on both roles. Both
   processes wait behind a shared release signal until both readiness markers
   have been observed; and
4. Win64 BuildCookRun followed by the same gameplay contract in `LyraGame.exe`
   with UEP-owned CPython 3.11.

Per-run logs and JSON remain under `Results/<timestamp>`, while the package is
archived directly to the marker-protected fixed path
`.build/LyraValidation/Full/Package`. UAT's internal executable remains at
`Windows/LyraStarterGame/Binaries/Win64/LyraGame.exe`; the driver synchronizes
its complete `Binaries/Win64` tree to the validation-only stable listener path
`Windows/LyraGame/Binaries/Win64/LyraGame.exe`. It verifies that both executable
SHA-256 values match before launch and passes the original directory through
UE's `-basedir=` override, so project, engine and ICU content still resolve from
the unmodified UAT layout. This avoids a package-sized mirror and a new Windows
Firewall application identity for every timestamp. Each result records the
wrapper, original internal executable, tested listener and both hashes.
Preconfigure TCP/UDP inbound **Block** rules for the stable listener path if the
host has no existing policy; packaged Standalone validation does not require
public/private inbound access. Do not disable Firewall notifications globally.

`-Incremental` retains injected-plugin `Binaries`/`Intermediate`, skips files
whose size and UTC timestamp are unchanged, and prunes stale owned source files.
Both direct UBT and BuildCookRun's UBT child default to two non-UBA actions via
`-MaxParallelActions`; override the script parameter only on a host with enough
memory.

`Readiness`, `Standalone`, `Network` and `Package` modes run narrower diagnostic
lanes; only `All` can set `full_acceptance: true`. Negative result
`20260811-184148` correctly reports `blocked` with 11 missing-content reasons
and no staging project for the local Git sample.

Pre-release `All` result `20260812-025232` passed readiness, Editor build,
Standalone and synchronized network roles, then completed its cold-DDC Win64
BuildCookRun with UAT ExitCode 0. Its post-archive executable guard exposed one
incorrect assumed internal directory before packaged launch. The corrected,
hash-verified package passed the exact packaged gameplay and strict lifecycle/
log contract in result `20260812-042711-packaged-resume`.

Formal named-commit result `20260812-051532` validates release-code commit
`c797d56` in one `All` invocation. Its summary reports `status: passed` and
`full_acceptance: true`; readiness, Editor, Standalone, synchronized server and
client, the 4,046-package Win64 BuildCookRun, and packaged gameplay all passed.
Every process exited 0, strict UAT/runtime error counts were zero, and the
archived internal and stable tested executable hashes matched.
