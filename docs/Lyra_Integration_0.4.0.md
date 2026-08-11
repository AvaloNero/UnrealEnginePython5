# Lyra integration boundary for 0.4.0

## Current status

The source integration is implemented and validated against Unreal Engine
5.8.0 with the engine-bundled CPython 3.11.8. The complete external project at
`F:\LyraStarterGame\LyraStarterGame.uproject` passes strict source/content
readiness, real Standalone gameplay, dedicated-server/client authority and
replication, Win64 cook/package and packaged gameplay. Version 0.4.0 keeps Lyra
authoritative and exposes only the narrow observations described below.

`UEPLyraBridge` lives with this repository's demo/validation material and is
copied into a disposable project under `.build`. Neither Unreal Engine source
nor the Lyra reference tree is edited. The disposable descriptor disables
desktop-only `AndroidFileServer`. After the complete Editor target compiles, the
runtime/package lanes also disable test-only `ShooterTests` and `RuntimeTests`
so gameplay evidence covers production features. ShooterTests content remains
staged and readiness-validated; production Lyra gameplay features remain
enabled. Runtime launches use deterministic English culture because UE5.8's
startup Core smoke tests compare source-language UnifiedError strings and fail
spuriously after Chinese localization. Fatal/assert/`Log*: Error` filtering
remains strict.

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

Historical `Validation/Test-UEP58LyraReadiness.ps1` result `20260811-184203`
reports the local Git source sample as correctly blocked:

- UE 5.8.0 and CPython 3.11.8 ready;
- Lyra project/targets/config and five explicit Registered Game Feature plugin
  descriptors ready;
- zero project assets/maps; and
- missing base GameData, front-end/editor/gameplay maps and all five root
  GameFeatureData assets.

Complete-project result `20260812-005352` and full-driver Readiness result
`20260812-013646` now report `status: ready` for UE 5.8.0, CPython 3.11.8, all
five explicit Registered Game Feature plugins, 2,837 base-project assets, three
base-project maps and every critical Unreal package. The Game Feature content
adds another 5,746 assets and 17 maps.

`Validation/Run-UEP58LyraValidation.ps1` encodes release acceptance as a single
`All` lane: strict readiness, isolated full-project staging, clean Editor build,
real Standalone Experience/Game Feature/pawn/input/GAS readiness, dedicated-
server/client authority and replicated player lifecycle with an explicit two-
role ready/release handshake, Win64 cook and package, and packaged CPython
gameplay. Negative result
`20260811-184148` rejected the local source sample before staging with all 11
content blockers preserved in JSON.

## 0.4.0 acceptance contract and evidence

Use a complete Launcher/Marketplace Lyra project outside the Unreal Engine
source checkout. Do not copy Marketplace content into `F:\UnrealEngine`.
The complete external project is available. The release contract requires:

1. strict readiness with real base and Game Feature content;
2. clean Editor build/startup with the reference content unchanged;
3. real Experience completion, `ShooterCore` Active and content-only
   `ShooterMaps` at least Registered;
4. a local pawn with Lyra Enhanced Input and ASC readiness;
5. separate server/client results proving correct net modes, authority and
   replicated player lifecycle;
6. Win64 cook/package and packaged UEP-owned CPython 3.11 execution; and
7. orderly shutdown with no compiler, fatal/assert or unexpected error
   diagnostics.

Readiness result `20260812-013646` passed item 1. Pre-release `All` result
`20260812-025232` passed items 1–5 and completed the full Win64 BuildCookRun
with UAT ExitCode 0. Its guarded post-archive check then exposed an incorrect
assumed internal executable directory before packaged launch. After correcting
the driver to use UAT's real `LyraStarterGame` layout plus a hash-verified stable
firewall identity, strict packaged result `20260812-042711-packaged-resume`
passed items 6–7 on the same artifact: CPython 3.11.8, the expected Experience,
Pawn/Input/ASC, active/registered Game Feature states, orderly shutdown and zero
fatal/assert or `Log*: Error` diagnostics.

The final named release-code gate is commit `c797d56`, result
`20260812-051532`. One `All` invocation repeated all seven items and wrote
`full_acceptance: true`: UE 5.8.0, CPython 3.11.8, 4,046 cooked packages,
Standalone and packaged Input/ASC readiness, synchronized Client and
DedicatedServer authority/replication, zero process failures or strict log
errors, and matching archived/tested executable SHA-256 values.
