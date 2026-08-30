# Third Person Blueprint Mixin demo for 0.7.0

The 0.7 demo starts from Unreal Engine 5.8's official Blueprint Third Person
template and preserves its object paths and runtime class identities:

- `BP_ThirdPersonGameMode_C`;
- `BP_ThirdPersonPlayerController_C`; and
- `BP_ThirdPersonCharacter_C`.

The Character EventGraph still receives Enhanced Input actions. The examples
overlay contains a project-owned copy of `BP_ThirdPersonCharacter` with only the
UEP Interface configuration added: `GetPythonMixinSet`,
`GetPythonMixinProfile`, and an instance-editable `Name` variable named
`PythonMixinProfile`. Its original gameplay graphs remain intact. One UEP class
router exposes two Python profiles on that same Character class:

| Profile | Python functions | `Move` behavior |
| --- | --- | --- |
| `Python` (default) | `ReceiveBeginPlay`, `Move`, `Aim` | Python movement implementation |
| `BlueprintFallback` | `ReceiveBeginPlay`, `Aim` | automatically calls the preserved BP `Move` |

The runtime spawns a second `BP_ThirdPersonCharacter_C` and selects
`BlueprintFallback` only for that instance while the player keeps `Python`.
This proves that Profile selection belongs to the UObject, not the CDO or
UClass. The smoke lane also switches the live player to `BlueprintFallback`,
sends mapped `W` input through the original Blueprint function, then switches
back to Python without changing the pawn type.

Jump/StopJumping, camera components, controller mapping setup and
`ABP_Unarmed_C` remain Blueprint/native. Python also reuses the six-pickup
round, moving companion and Slate viewport HUD from 0.6.

The implementation lives in the examples repository under
`UEPPythonThirdPerson/Mixin`. The runner stages it over a disposable copy of the
official template and current plugin. Normal Play loads the Character's
`DA_ThirdPersonCharacterMixin` declaration; the profile classes are plain Python
classes and are not decorators that secretly bind the BP at startup. The smoke
lane separately exercises the explicit registration API as a regression, then
returns the live class to its declared Mixin Set.

The examples overlay also includes a small asset-authored probe under
`/Game/UEPTests`: a `UEPPythonMixinSet`, one Blueprint that directly declares
`UEPPythonMixinInterface`, and a child Blueprint that inherits the router. All
five generated assets are example-owned. The generator modifies only the
disposable staged Character copy before copying it into this overlay; it never
edits `F:\UnrealEngine`, the installed template, or Unreal Engine source.

## Prepare without compiling

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Prepare `
    -Incremental
```

## Automated runtime proof

Generate or refresh the committed Interface/Mixin Set regression assets after a
reflection/API change:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode GenerateMixinAssets `
    -Incremental
```

This builds only the Editor target, generates five assets in the disposable
staging project, verifies their report/log and copies exactly those `.uasset`
files into the examples overlay: the playable Character BP and its Mixin Set,
plus the three regression assets. It does not cook, package or modify Engine
source/template assets.

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Smoke `
    -Incremental
```

The smoke lane requires all of the following:

- UE 5.8 and engine-bundled CPython 3.11;
- the three original `BP_*_C` runtime classes;
- a playable Character registered from its BP Interface and
  `DA_ThirdPersonCharacterMixin`, with the exact per-profile function subsets
  shown above;
- late package-load discovery of a real Interface/Mixin Set Blueprint, with its
  inherited child sharing exactly one router owner;
- native automatic fallback for `AddInstances(const TArray<FTransform>&, ...)`,
  including its `TArray<int32>` return and untouched const-array input;
- native automatic fallback for
  `BatchUpdateInstancesTransforms(..., const TArray<FTransform>&, ...)`,
  including its scalar `bool` return;
- two simultaneous objects of the same Character BP class selecting different
  profiles through `PythonMixinProfile`, including explicit-selector precedence
  and cache invalidation that re-reads the Blueprint variable;
- automatic original-BP fallback when `BlueprintFallback` has no `Move`;
- registration-time rejection of an invalid signature without disturbing the
  active router;
- one mixed BeginPlay and one explicit original BeginPlay call;
- mapped `W` and mouse axes reaching Python Move/Aim with real movement/look;
- retained Blueprint/native jump and a valid official animation instance;
- all six Python pickups, companion movement and an attached/updated Slate HUD;
- explicit unregister on the live pawn, a different restored Move UFunction and
  real movement through the original Blueprint implementation;
- direct-API re-registration of both profiles on that already spawned pawn,
  followed by restoration and re-registration from the declared Mixin Set, with
  a new router generation/function and Python-driven movement in both phases;
- rejected registry mutation from Python initialization/teardown callbacks plus
  successful nested initialization of a different object; and
- balanced per-profile initialization/teardown, final class-map restoration,
  helper removal, HUD/runtime teardown and clean process exit.

Repeat-generation result `20260831-012003` and schema-4 headless Standalone
result `20260831-012054` pass on UE 5.8.0 and CPython 3.11.8. The headless
runtime report identifies `DA_ThirdPersonCharacterMixin` and
`PythonMixinProfile` as the Character's declared selection source. Two live
`BP_ThirdPersonCharacter_C` objects select `Python` and `BlueprintFallback`
independently; Blueprint fallback moves 350.876 Unreal units. Full unregister
restores Blueprint movement for 245.374 units, direct-API re-registration moves
480.133 units, and declared-Mixin-Set re-registration moves 520.127 units. The
report also records three rejected initializer mutations, three rejected
teardown mutations, one nested peer initialization, distinct injected/restored/
direct/declared function identities, and balanced `5/5` Character lifecycle
events.

Win64 Package result `20260831-014230` builds the Editor and Game targets, cooks,
stages and archives 0.714 GiB, then passes the same schema-4 contract inside the
packaged executable. It retains `BP_ThirdPersonCharacter_C`, records 358.246
units of automatic Blueprint fallback, 480.46 units after direct registration
and 484.185 units after declared-Mixin-Set re-registration, with balanced `5/5`
Character lifecycle events and a final router count of zero. Cook emits
`UEP_MIXIN_COMMANDLET_BOOTSTRAP_SKIPPED` and contains no Mixin registration,
proving transient routed functions are not serialized into cooked assets. The
tested executable SHA-256 is
`99BFA1CC83C89CB0E5A8898839547802CAE3CBFAA87B81BB277BA8EF3B775DB5`.
Build, generation, Cook and runtime logs contain no compiler/linker, fatal,
assertion or `Log*: Error` diagnostics. Only the visible-client manual gate
remains to be recorded for this tree.

## Play

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant Mixin `
    -Mode Play `
    -Incremental
```

Use WASD, mouse and Space as in the template. The upper-left HUD identifies the
retained Blueprint-class model. `Overlay` and `PythonFirst` remain separate
variants for side-by-side comparison.
