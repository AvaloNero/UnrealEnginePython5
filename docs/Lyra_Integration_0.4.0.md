# Lyra integration boundary for 0.4.0

## Current status

The source integration is implemented and validated against Unreal Engine
5.8.0 with the engine-bundled CPython 3.11.8. The local project at
`F:\UnrealEngine\Samples\Games\Lyra\Lyra.uproject` is Epic's Git source sample,
not a complete Launcher project: it contains zero `.uasset` and zero `.umap`
files. Version 0.4.0 therefore remains in progress.

`UEPLyraBridge` lives with this repository's demo/validation material and is
copied into a disposable project under `.build`. Neither Unreal Engine source
nor the Lyra reference tree is edited.

## Capability audit

| Lyra boundary | Native owner | Python extension in 0.4 | Rule |
| --- | --- | --- | --- |
| Experience load | `ULyraExperienceManagerComponent` and its native multicast delegate | bridge emits `OnExperienceReady` and reports the current Experience | Python observes completion; it never calls `SetCurrentExperience` |
| Game Features | Experience actions and `UGameFeaturesSubsystem` | snapshot lists feature names/states | Python does not load, activate, deactivate or unload a feature |
| Modular gameplay | `UGameFrameworkComponentManager`, Pawn/Hero init states | Python waits until the world bridge and Lyra readiness signals exist | no polling write or forced init-state transition |
| Enhanced Input | `ULyraHeroComponent` and Lyra input configs | snapshot reports `IsReadyToBindInputs()` | Lyra owns mappings/bindings; validation does not inject authoritative gameplay input |
| Gameplay Ability System | PlayerState ASC and `ULyraPawnExtensionComponent` | snapshot reports whether the pawn has an initialized ASC | ability grants, effects and gameplay tags remain authority-owned |
| Asset management | `ULyraAssetManager`, primary assets and Experience bundles | readiness checks critical assets before runtime | Python does not synchronously fabricate or replace missing primary assets |
| Multiplayer | GameMode/GameState/PlayerState replication | snapshot reports local/remote controller counts, PlayerState count/readiness, net mode, authority and pawn roles | server remains authoritative; bridge fields are derived local observations and are not replicated |

Several important Lyra callbacks are native C++ delegates, not reflected
dynamic delegates. Binding them by guessing memory layouts from Python would be
fragile. The bridge binds those APIs in C++, follows `UWorld` lifetime, exposes
only reflected values/events and does not add a dependency from Lyra back to
UEP.

## Thread, lifetime and reload contract

- Bridge calls and UObject access occur only on Unreal's game thread.
- One bridge subsystem exists per supported Game/PIE/GamePreview world.
- The bridge observes `GameStateSetEvent`, so a client that receives its
  `GameState` after world begin play still attaches to the Experience manager.
- The Experience callback uses a UObject-bound native delegate, so a destroyed
  subsystem cannot be called through a dangling raw object.
- Python stores its exact callable, unbinds it before world replacement or
  shutdown, and writes no bytecode cache into staged content.
- `ClearPythonListeners` affects only the bridge-owned dynamic event; it does
  not alter Lyra delegates or gameplay state.
- No bridge property is replicated and no bridge function performs an
  authority-sensitive mutation.

## Automated evidence

`Validation/Run-UEP58LyraSourceValidation.ps1` creates a marker-protected stage,
copies only source/config/plugin files, injects UEP and the bridge, and applies
source-only configuration overrides inside the stage. Clean result
`20260811-165230` produced:

- `LyraEditor` build exit 0 in 192.502 seconds;
- runtime exit 0 in 38.914 seconds;
- CPython 3.11.8, Standalone game world, game-thread snapshot and 12 ticks;
- bridge/script/pass markers followed by object subsystem closure, `Goodbye
  Python` and log closure;
- zero compiler, fatal/assert and `Log*: Error` diagnostics; and
- zero Unreal Engine source-code modifications during the run.

Final source result `20260811-183357` rebuilt UEP, the reflected network
snapshot and late-`GameState` lifecycle through UBT. It passed the 12-tick
Standalone gate, then ran a second 30-tick `DedicatedServer` on UDP 7789 and
proved `unreal_engine.request_exit()` produces exit code 0, object-subsystem and
Python closure, and zero fatal or `Log*: Error` diagnostics. Generic regression
`20260811-184425` passed 70/70 required checks across both interpreter modes,
the Editor, UAT cook/package and packaged CPython 3.11.8 runtime.

The source-only stage intentionally uses `/Engine/Maps/Entry`, the base Asset
Manager and no Lyra content-backed Game Feature plugins. This is a compatibility
test, not gameplay evidence.

`Validation/Test-UEP58LyraReadiness.ps1` result `20260811-184203` reports:

- UE 5.8.0 and CPython 3.11.8 ready;
- Lyra project/targets/config and five explicit Registered Game Feature plugin
  descriptors ready;
- zero project assets/maps; and
- missing base GameData, front-end/editor/gameplay maps and all five root
  GameFeatureData assets.

`Validation/Run-UEP58LyraValidation.ps1` encodes the remaining acceptance as a
single `All` lane: strict readiness, isolated full-project staging, clean Editor
build, real Standalone Experience/Game Feature/pawn/input/GAS readiness,
dedicated-server/client authority and replicated player lifecycle with an
explicit two-role ready/release handshake, Win64 cook and package, and packaged
CPython gameplay. Negative result
`20260811-184148` rejected the local source sample before staging with all 11
content blockers preserved in JSON. This proves the guard, not the remaining
runtime gates.

## Remaining 0.4.0 acceptance gates

Use a complete Launcher/Marketplace Lyra project outside the Unreal Engine
source checkout. Do not copy Marketplace content into `F:\UnrealEngine`.
0.4.0 can be marked complete only after one named commit passes all of these:

1. strict readiness with real base and Game Feature content;
2. clean Editor build/startup with the reference content unchanged;
3. real Experience completion and required Game Features reaching Active;
4. a local pawn with Lyra Enhanced Input and ASC readiness;
5. separate server/client results proving correct net modes, authority and
   replicated player lifecycle;
6. Win64 cook/package and packaged UEP-owned CPython 3.11 execution; and
7. orderly shutdown with no compiler, fatal/assert or unexpected error
   diagnostics.

Only after those gates pass should the main plugin descriptor and release
documentation change from 0.3.0 to 0.4.0.
