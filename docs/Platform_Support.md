# Platform support and readiness

## Release-validated platform

UnrealEnginePython 0.3.0 is release-validated on Win64 with Unreal Engine 5.8.0
and the engine-bundled CPython 3.11.8. The gate compiles Editor and Game plugin
targets, runs shared and UEP-owned interpreters, runs full editor tests, cooks a
Development package and executes the packaged game.

No Unreal Engine source change is required or permitted by these workflows.

## Linux lane

`Validation/Run-UEP58LinuxValidation.sh` is a real build/runtime lane for a
Linux UE5.8 installation. It compiles the Editor plugin and a foreign UnrealGame
plugin target, then executes the core suite with both interpreter owners. The
GitHub workflow requires a self-hosted runner labelled `linux` and
`unreal-engine-5.8`, with `UE_ENGINE_ROOT` configured as a repository variable.

A checked-in lane is not proof that Linux works. Linux becomes release-validated
only after its `summary.json` and logs are retained as a green artifact.

## Current local readiness

The Win64 UE5.8 installation at `F:\UnrealEngine` reports Linux as not ready and
identifies `v26_clang-20.1.8-rockylinux8` as the required SDK. UEP does not
install that SDK or alter the engine tree automatically.

Recheck without changing the SDK:

```powershell
.\Validation\Test-UEP58PlatformReadiness.ps1 -Platform Linux
```

Use `-Strict` in CI when missing readiness must return a failure code.

Mac, LinuxArm64, Android and console platforms have no 0.3.0 release-level
runtime result and are not included in the support claim.
