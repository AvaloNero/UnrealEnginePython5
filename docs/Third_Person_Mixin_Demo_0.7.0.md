# Third Person Blueprint Mixin demo for 0.7.0

The 0.7 demo starts from Unreal Engine 5.8's official Blueprint Third Person
template and preserves its assets. Unlike the 0.6 Python-first variant, it does
not select transient Python Character, Controller or GameMode classes.

The active runtime types remain:

- `BP_ThirdPersonGameMode_C`;
- `BP_ThirdPersonPlayerController_C`; and
- `BP_ThirdPersonCharacter_C`.

The official Character EventGraph continues to receive Enhanced Input actions.
Its calls to the Blueprint `Move` and `Aim` functions resolve to Python-mixed
implementations. Jump/StopJumping, camera components, controller mapping setup
and `ABP_Unarmed_C` remain Blueprint/native. Python also reuses the six-pickup
round, moving companion and Slate viewport HUD from 0.6.

The implementation lives in the examples repository under
`UEPPythonThirdPerson/Mixin/Content/Scripts/uep_third_person_mixin`. The runner
stages it over a disposable copy of the official template and current plugin;
it does not edit `F:\UnrealEngine`, the installed template or source assets.

## Prepare without compiling

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Prepare `
    -Incremental
```

## Automated runtime proof

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Smoke `
    -Incremental
```

The smoke lane requires all of the following rather than treating successful
startup as acceptance:

- UE 5.8 and engine-bundled CPython 3.11;
- the three original `BP_*_C` runtime classes;
- exactly `Aim`, `Move` and `ReceiveBeginPlay` in the mixin registry;
- registration-time rejection of an invalid Python signature without removing
  the active generation;
- one mixed BeginPlay and one explicit original BeginPlay call;
- mapped `W` and mouse axes reaching Python Move/Aim with real movement/look;
- retained Blueprint/native jump and a valid official animation instance;
- all six Python pickups, companion movement and an attached/updated Slate HUD;
- explicit unregister on the live pawn, a different restored Move UFunction and
  real movement through the original Blueprint implementation;
- re-registration on that already spawned pawn, a new generation/function and
  Python-driven movement again; and
- two per-object initialization/teardown generations, final class-map
  restoration, helper removal, HUD/runtime teardown and clean process exit.

Results and logs are written under `.build/Demos/Results/<timestamp>`.
The current headless acceptance result is `20260829-225225`; it passed on UE
5.8.0 and CPython 3.11.8 with no fatal, assertion or `Log*: Error` diagnostics.
The 0.7 Mixin Cook/package and visible-client gates have not yet been run.

## Play

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Play `
    -Incremental
```

Use WASD, mouse and Space as in the template. The upper-left HUD identifies the
retained Blueprint-class + Python Move/Aim ownership model. `Overlay` and
`PythonFirst` remain separate driver variants for side-by-side comparison.
