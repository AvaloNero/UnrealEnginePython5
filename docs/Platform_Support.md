# Platform support and readiness

## Release-validated platform

UnrealEnginePython 0.4.0 is release-validated on Win64 with Unreal Engine 5.8.0
and the engine-bundled CPython 3.11.8. The core gate compiles Editor and Game
plugin targets, runs shared and UEP-owned interpreters, runs full editor tests,
cooks a Development package and executes the packaged game. The Lyra gate adds
strict content readiness, Standalone gameplay, synchronized dedicated-
server/client authority and replication, full Win64 BuildCookRun and packaged
Experience/Pawn/Input/ASC validation.

The final generic Win64 result `20260812-043408` passed 70/70 shared,
standalone, exception-boundary, Editor/Slate and packaged-runtime checks; its
Development BuildCookRun completed with UAT ExitCode 0.

The final Lyra result `20260812-051532` passed the complete `All` lane for named
release-code commit `c797d56` and reported `full_acceptance: true`, including
synchronized client/server evidence and a 4,046-package Win64 BuildCookRun.

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

Mac, LinuxArm64, Android and console platforms have no 0.4.0 release-level
runtime result and are not included in the support claim.
