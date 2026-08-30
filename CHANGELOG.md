# Changelog

This file records the UE5 port maintained in this repository. The historical
UE4 project history remains available in Git.

## [0.7.0] - 2026-08-29

### Added

- Reversible Blueprint-class Python mixins through `register_mixin`, the
  `mixin` decorator, `unregister_mixin`, `unregister_all_mixins` and
  `get_registered_mixins`.
- A class-level Mixin Router with named profiles, per-instance
  `set_mixin_profile`/`clear_mixin_profile` selection and automatic preserved-
  Blueprint fallback for functions omitted by the active profile.
- `UEPPythonMixinInterface` and `UEPPythonMixinSet` for CDO-declared profile
  sets with instance-selected profile names, plus loaded-class discovery and
  explicit rescan APIs.
- Per-UObject mixin initialization/state and on-demand helper-method binding
  while preserving the object's original Blueprint-generated `UClass`.
- `call_mixin_original()` for explicitly executing the Blueprint/native
  implementation hidden by a mixed Python function.
- Native conversion of reflected `FInputActionValue` parameters to Python
  bool, float, `FVector2D` or `FVector` values.
- Registration-time Python signature validation; a rejected replacement keeps
  the currently active mixin generation intact.
- A Third Person `Mixin` demo variant that retains the official Blueprint
  Character, Controller, GameMode, input graph and AnimBP while two instances
  of the same Character class use different Python profiles; one profile omits
  `Move` to prove automatic original-Blueprint fallback.
- A real asset-authored Interface/Mixin Set regression fixture with one direct
  Blueprint owner and one inherited child, generated deterministically through
  the Editor-only `blueprint_configure_mixin` helper.
- A playable `DA_ThirdPersonCharacterMixin` declaration and an example-owned
  same-path Character BP copy whose instance-editable `PythonMixinProfile`
  variable selects profiles without changing `BP_ThirdPersonCharacter_C`.

### Changed

- Python mixins are restored before the Python housekeeper/interpreter shuts
  down, so injected function maps cannot outlive their Python callables.
- The Third Person runner now stages and validates `Overlay`, `PythonFirst` and
  `Mixin` independently and has a dedicated mixin smoke/package report contract.
- Interface discovery now uses the all-build package-completion delegate and
  registers only the Blueprint class that directly declares the Interface.
- Automatic missing-profile fallback copies compatible native properties into
  the preserved UFunction's own frame before direct dispatch, retaining
  scalar/array returns and const-array inputs even when linked offsets differ.
- Registry mutation is rejected across selector, initializer, teardown, mixed
  callable and original-call callback boundaries; instance state is re-resolved
  after callbacks instead of retaining invalidatable container references.
- The Editor authoring helper can now create/reuse a scalar `Name` variable and
  wire `GetPythonMixinProfile` to it; the normal Third Person startup registers
  plain profile classes from the BP's Mixin Set, while direct registration is
  retained only as an explicit regression phase.
- Cook commandlets skip Mixin discovery/bootstrap and reject registry mutation,
  preventing transient routed `UFunction` entries from being serialized into
  cooked Blueprint assets; registration occurs after the packaged runtime starts.

### Supported boundaries

- Version 0.7 permits one router with multiple mutually exclusive profiles on a
  Blueprint-generated class and ordinary instance functions/events only. RPC,
  latent, static/delegate functions, non-const output/reference parameters and
  ordered profile chaining are rejected.
- Simultaneous registrations on related base/derived Blueprint classes are
  rejected so no restored function can depend on another generation.
- Blueprint assets should be unregistered before editor recompilation. The
  implementation changes neither Unreal Engine source nor the installed Third
  Person template. The examples overlay intentionally owns a configured copy of
  the Character BP; its original gameplay graphs remain intact.

### Validation

- UE5.8 repeat-generation result `20260831-012003` and schema-4 headless Mixin
  Standalone result `20260831-012054` pass on engine CPython 3.11.8. They cover
  the playable Character's BP-declared Mixin Set/profile variable, same-BP
  selection, Blueprint fallback, direct and declared re-registration, final
  restoration, the package-load Interface fixture, native const-array/return
  fallback and callback-reentry guards with clean build/runtime logs.
- Official-template audit `20260829-225525` and the existing 0.6 Python-first
  runtime regression `20260829-225336` pass.
- Win64 Package result `20260831-014230` builds the Editor and Game targets,
  cooks/stages/archives 0.714 GiB and passes the schema-4 contract in the
  packaged executable. Its Cook log has one commandlet-bootstrap skip marker,
  zero Mixin registrations and clean strict diagnostics. The tested executable
  SHA-256 is
  `99BFA1CC83C89CB0E5A8898839547802CAE3CBFAA87B81BB277BA8EF3B775DB5`.
- The visible-client manual gate has not yet been recorded for this tree.

## [0.6.0] - 2026-08-24

### Added

- A modular `uep_third_person` runtime package separating generated Character,
  PlayerController and GameMode types from animation, collectible gameplay,
  viewport HUD, world lifecycle and opt-in smoke automation.
- A six-pickup playable round with cumulative score, a completion countdown,
  repeated rounds and a Python-driven companion actor.
- A hit-test-invisible Slate HUD attached directly to the game viewport. Python
  updates the objective, score, timer, speed and locomotion state without using
  on-screen debug messages or a gameplay Blueprint.
- Host-side tests for the Unreal-independent scoring and round-transition state.
- Runtime access to `get_discovered_plugins()`, `get_enabled_plugins()`,
  `find_plugin()` and `unreal_engine.IPlugin`, allowing packaged processes to
  inspect the descriptor of the plugin they actually loaded.

### Changed

- `PythonFirst` is now the demo driver's default variant; the 0.1.0 Blueprint
  overlay remains available explicitly as `-Variant Overlay`.
- `uep_python_third_person.py` is now a compatibility import. Normal startup
  loads `uep_third_person.bootstrap`, while smoke automation starts only when
  its result command-line argument is present.
- The Third Person smoke contract now collects the real spawned round, verifies
  HUD construction/update, performs same-map travel, validates a fresh session
  and explicitly checks HUD/input/runtime teardown.
- The input gate now sends `W`, `MouseX`/`MouseY` and `SpaceBar` through
  PlayerInput and the official mapping contexts instead of injecting InputAction
  values directly.
- Shared template packs now preserve their declared `Input`, `Characters` and
  `LevelPrototyping` mount roots. Incremental staging rejects legacy flattened
  assets so Python and the mapping contexts cannot silently load different
  InputAction objects.
- Package builds invoke the Game target directly with `-NoUBA`, then run UAT with
  `-skipbuild`; packaged smoke launches the inner target executable instead of
  the short-lived archive bootstrapper.
- The demo descriptor disables the unavailable local `PlatformCrypto` program
  plugin for packaging, and the driver now rejects fatal, assertion, unhandled
  exception and `Log*: Error` diagnostics from both UAT and runtime logs.
- The demo Game target disables Unreal Trace, preventing timestamped Development
  packages from opening the TCP 1985 control listener or repeatedly acquiring a
  new Windows Firewall application identity; the Engine source and shared
  UnrealEditor target remain unchanged.
- Demo history and working files now live in
  `AvaloNero/UnrealEnginePython5Examples`; this repository pins that public
  repository as the `Demos` submodule instead of duplicating the projects in the
  plugin source tree.

### Validation

- UE 5.8's bundled CPython 3.11.8 passes all four pure round-state tests, all 16
  staged Python files parse, the PowerShell driver parses and `git diff --check`
  reports no whitespace errors.
- Headless UE 5.8 Standalone result `20260824-211718` passes the schema-5
  contract through mapped `W`, `MouseX`/`MouseY` and `SpaceBar` input: 313.91
  units of movement, 28 degrees of look, 127.551 units of jump displacement,
  all animation states, 6/6 pickups, HUD, companion, travel and explicit
  HUD/input/runtime teardown with every key released.
- UE 5.8 Blueprint audit result `20260824-211829` passes with the retained
  reference baseline of four Blueprints, 27 graphs and 173 nodes.
- Win64 package result `20260824-203312` records `Build=False` in UAT after the
  explicit `-NoUBA` Game build, archives 1754 Pak entries and passes the same
  schema-5 contract inside the packaged executable. Strict UAT and runtime
  diagnostic scans both report zero release-blocking entries; the executable
  SHA-256 is `04A9CF37D82F31B6A9D6255E99841FD3D2BEEEE30BAB797F83D11C6C2F8272FD`.
- Visible 1280x720 Play result `20260824-044028` renders the Python HUD and
  gameplay, accepts physical mouse look and SpaceBar jump, and completes the
  jump/fall/land animation sequence. The final trace-disabled package was then
  relaunched with zero TCP listeners and no Windows Security picker; no firewall
  permission was broadened.

## [0.5.0] - 2026-08-13

### Added

- `FUEPLyraGameplayCommandResult` and the reflected
  `ApplyAuthorityHealthDelta` command for one bounded Python-driven Lyra
  gameplay write. Python owns command IDs, values, target role and sequencing;
  the adapter dispatches Lyra's configured SetByCaller Damage/Heal Gameplay
  Effects instead of writing attributes directly.
- Health/MaxHealth and damage-immunity observations in the Lyra runtime
  snapshot so Python can wait for the real game phase rather than bypassing GAS
  policy.
- Game-thread, server-authority, unambiguous player target, command-format,
  finite magnitude, non-lethal damage, non-overheal and command-idempotency
  guards.
- A machine-readable Standalone/packaged state machine proving damage,
  duplicate rejection, later-tick observation, healing and restoration.
- A dedicated-server/client Python handshake proving local client writes are
  rejected, server GAS changes replicate to the client, and restore occurs only
  after the client acknowledges the damaged value.

### Changed

- The full Lyra validator enables the 0.5 gameplay slice by default, requests
  three AI bots to advance Lyra's genuine Warmup phase, and reserves
  `-SkipGameplaySlice` for non-release diagnostics. An `All` run with the slice
  disabled cannot report `full_acceptance: true`.
- Dedicated-server validation binds to `127.0.0.1` in addition to its fixed UDP
  port, keeping the automated listener off the LAN.
- Runtime JSON schema version 2 retains the 0.4 readiness snapshot and adds the
  complete gameplay command/event evidence used by strict PowerShell
  assertions.
- The generic Win64 release driver now forwards its bounded parallel-action
  setting and `-NoUBA` policy into UAT's nested Game build, with a two-hour
  package timeout for low-memory source-engine workstations.

### Validation

- Incremental Standalone result `20260812-233341` passed UE 5.8.0 and CPython
  3.11.8 after respecting Lyra Warmup damage immunity. It records exact Health
  `100 -> 90 -> 100`, `Applied`, `RejectedDuplicateCommand`, `Applied`, clean
  shutdown and zero strict error diagnostics.
- Incremental Network result `20260812-233801` passed a real loopback dedicated
  server and client. Both reports record `RejectedNotAuthority` for the client,
  authority-applied server Damage/Heal commands, duplicate rejection and client
  replication observations at 90 and 100 Health.
- Formal `All` result `20260812-234501` passed the complete 0.5 gameplay-slice
  candidate with
  `full_acceptance: true`. Readiness, Editor build, Standalone, dedicated
  server, client, UAT and packaged gameplay all exited 0; UAT reported
  `BUILD SUCCESSFUL`, archived 1.89 GiB, and the internal/tested executable
  SHA-256 values matched. All four runtime logs contained the gameplay pass and
  orderly-close markers with zero strict fatal/assert/`Log*: Error`
  diagnostics.
- After the target selector was hardened to reject multiple matching human
  controllers, exact-source Network result `20260813-005436` rebuilt only
  `UEPLyraBridge` and passed client denial, server authority, duplicate-command
  rejection, replication and restoration. Standalone result
  `20260813-005835` reused that build and passed the local `100 -> 90 -> 100`
  sequence. All three runtime logs closed normally with zero strict errors.
- No new Cook/package was started after that final selector hardening. Result
  `20260812-234501` remains the latest package-level evidence, while the two
  2026-08-13 incremental results are the exact-source runtime evidence.

### Supported boundaries

- Lyra continues to own Experience/Game Feature activation, ability grants,
  input, GAS execution, Health storage and replication. Version 0.5 exposes no
  arbitrary Gameplay Effect launcher, raw attribute setter or automatic client
  RPC. Unreal Engine and the external Lyra reference project remain unchanged.

## [0.4.0] - 2026-08-12

### Added

- A disposable project-side `UEPLyraBridge` world subsystem that converts
  Lyra's native Experience completion callback into a reflected dynamic event
  and exposes read-only Game Feature, Enhanced Input, Ability System, authority
  and net-mode snapshots.
- A Python lifecycle probe with explicit callback unbinding, game-thread checks,
  role-aware assertions, machine-readable results and orderly process shutdown.
- `unreal_engine.request_exit()` for graceful game, commandlet and dedicated-
  server shutdown without requiring a local `PlayerController`.
- Lyra source/readiness drivers that stage outside the reference project,
  require UE5.8 and engine-bundled CPython 3.11, and distinguish source
  compatibility from content-dependent acceptance.
- A strict full-content Lyra driver covering isolated staging, Editor build,
  real Experience/Game Feature readiness, Standalone pawn/input/GAS state,
  dedicated-server/client authority and replication, Win64 cook/package and
  packaged CPython runtime.
- Network-observation fields for local/remote controllers, PlayerState, pawn
  control/roles and an explicit two-process ready/release handshake so neither
  network role can terminate before both contracts are satisfied.

### Changed

- `PythonEditor` project-tree ownership now uses `TObjectPtr`; a separate
  non-reflected raw-pointer view feeds Slate. This satisfies Lyra's strict
  UE5.8 native-pointer target policy without weakening it.
- Packaged validation writes `UEP58Host` to a guarded fixed output path and
  records the tested executable hash in each timestamped result, preventing
  Windows Firewall from treating every run as a new application without an
  additional package-sized mirror copy.
- Full-content Lyra packaging uses the same guarded fixed-output model for
  `LyraGame`. The driver records UAT's original internal executable, synchronizes
  its project-binary tree to a stable firewall identity, verifies
  matching executable SHA-256 values and uses UE's `-basedir=` override so the
  unmodified package layout remains authoritative.
- After the complete Lyra Editor target compiles, gameplay/package staging
  disables test-only `ShooterTests` and `RuntimeTests`, keeping runtime evidence
  scoped to production Lyra features while their content remains readiness-
  validated.
- The Lyra Python probe now uses a monotonic wall-clock timeout, records its
  final snapshot and pending requirements on failure, and logs changing pending
  conditions instead of relying only on Unreal ticker delta time.
- Network probes latch their fully ready snapshot before waiting on the shared
  release gate, preventing one peer's faster shutdown from invalidating the
  other peer's already-observed replication state.
- Lyra Game Feature assertions now distinguish active gameplay code
  (`ShooterCore`) from registered content ownership (`ShooterMaps`), matching
  their actual plugin descriptors and observed UE5.8 lifecycle states.
- Runtime validation pins `-culture=en` so UE5.8's startup Core smoke tests keep
  their source-language string contract on localized Windows installations;
  strict fatal/assert/`Log*: Error` rejection remains unchanged.
- Incremental Lyra staging retains UEP/bridge build outputs, skips unchanged
  files and prunes stale owned sources. Editor and package builds default to two
  non-UBA actions to bound memory use on the validation host.

### Validation

- Clean source result `20260811-165230` built `LyraEditor` and all staged UEP
  modules, loaded `UEPLyraBridge`, ran 12 Standalone game-world ticks on
  CPython 3.11.8 and exited 0 with zero compiler, fatal and `Log*: Error`
  diagnostics. Unreal Engine source remained unchanged.
- Final source regression `20260811-183357` rebuilt UEP and the reflected
  network snapshot through UBT, passed the Standalone gate, then proved a real
  `DedicatedServer` listening on UDP 7789 can exit cleanly through
  `unreal_engine.request_exit()` with zero fatal or `Log*: Error` diagnostics.
- Full generic UE5.8 regression `20260811-184425` passed all 70 required shared,
  standalone, exception-boundary, Editor/Slate, cook/package and packaged-
  runtime checks; the packaged process used CPython 3.11.8 and exposed the new
  exit API.
- Final generic UE5.8 regression `20260812-043408` repeated all six suites after
  the release-driver changes: 70/70 checks passed, the full Development
  BuildCookRun reported `BUILD SUCCESSFUL` and ExitCode 0, and the stable
  packaged executable ran CPython 3.11.8 with zero fatal or error diagnostics.
- Readiness result `20260811-184203` reports source ready but content blocked:
  the local Lyra Git sample contains zero assets/maps and lacks all five
  required GameFeatureData assets.
- Complete external-project readiness results `20260812-005352` and
  `20260812-013646` pass UE 5.8.0, CPython 3.11.8, all five Game Features and
  every critical Unreal package without modifying the reference project.
- Full-driver negative result `20260811-184148` stopped before staging with 11
  machine-readable content blockers, proving that source-only evidence cannot
  accidentally satisfy the full acceptance lane.
- Pre-release `All` result `20260812-025232` passed strict readiness, the
  incremental `LyraEditor` build, real Standalone gameplay, synchronized
  dedicated-server/client authority and replication, and a full Win64
  BuildCookRun. UAT completed its cold-DDC cook, stage and archive with
  `BUILD SUCCESSFUL` and ExitCode 0; the driver then correctly stopped on an
  incorrect assumed internal executable path before launching the package.
- After correcting that guarded path, strict packaged result
  `20260812-042711-packaged-resume` ran the bit-identical stable listener with
  the original UAT BaseDir. It passed CPython 3.11.8, Experience,
  `ShooterCore` Active, `ShooterMaps` Registered, Pawn/Input/ASC and orderly
  shutdown with zero fatal/assert or `Log*: Error` diagnostics.
- Named release-code commit `c797d56` then passed the complete lane in one
  formal `All` result, `20260812-051532`, with `full_acceptance: true`. The run
  repeated readiness, Editor build, Standalone and synchronized network roles,
  cooked 4,046 packages, archived Win64, and passed the packaged game. Every
  process exited 0, UAT and all runtime logs contained zero error diagnostics,
  and the internal/tested executable SHA-256 values matched.

### Supported boundaries

- Release-level runtime validation remains Win64-only on UE 5.8.0 with the
  engine-bundled CPython 3.11.8. The Lyra bridge observes native ownership
  boundaries; it does not activate features, grant abilities, replace input
  mappings or mutate replicated authority-sensitive state. The reference Lyra
  project and Unreal Engine source remain unchanged.

## [0.3.0] - 2026-08-11

### Added

- Generic reflected `TSet` conversion between Unreal properties and Python
  `set`/`frozenset`, including dynamic `{ElementProperty}` declarations and
  hashability validation.
- `unreal_engine.is_in_game_thread()` for explicit thread-policy checks.
- Separate shared/standalone exception-boundary suites, dynamic function stress
  coverage, duplicate-class reload-contract checks and callback weak-reference
  lifetime tests.
- Runtime wrapper tests for animation parameter round trips and local UDP
  receiver lifecycle, plus editor tests for Level Sequence tracks, sections,
  folders and asset persistence.
- A UE5.8 platform-readiness probe and a self-hosted Linux build/runtime lane.
  Linux support remains unclaimed until that lane has a green artifact.
- Commit-addressed source archives, SHA-256 manifests and optional UE5.8
  `BuildPlugin` binary packaging.

### Changed

- Explicit delegate and Enhanced Input unbinding now releases the corresponding
  Python callable immediately instead of retaining it until owner garbage
  collection.
- Dynamic Python constructors/functions now replace callable references safely;
  non-trivial call frames are initialized/destroyed through reflection and
  return values use `FProperty::CopyCompleteValue` instead of byte copies.
- Dynamic class flag inspection no longer leaks temporary Python references and
  duplicate class creation remains an explicit restart-required error.
- The UDP socket wrapper validates addresses, ports and buffers, initializes its
  native pointers, protects closed sockets and supports idempotent close.
- `BlendSpace.set_blend_parameter()` now deep-copies the reflected
  `FBlendParameter` instead of byte-copying its `FString`, eliminating heap
  aliasing and the packaged shutdown corruption found by the combined wrapper
  and dynamic-string stress run.
- Core validation promotes `TSet` from a known exclusion and keeps expected
  Python exceptions in a dedicated, strictly filtered process lane.

### Validation

- The Win64 quick matrix passes 47 checks: 20 shared core, 20 standalone core,
  two exception-boundary recovery checks and five editor/Slate/Sequencer checks.
- The full Win64 gate additionally runs the 20 core checks in a cooked packaged
  executable, for 67 required checks total. Full result `20260811-153622`
  passed with an orderly CPython/object-subsystem shutdown and exit code 0;
  independent scans found no fatal, compiler or unexpected error diagnostics.
- UE5.8.0 reports the local Linux platform as not ready because
  `v26_clang-20.1.8-rockylinux8` is not installed. This is recorded as a
  platform limitation, not a passing Linux result.
- Unreal Engine source remains unchanged.

### Known limitations

- Dynamic `UClass` redefinition is restart-required; in-process class hot reload
  is not supported.
- UObject/reflection operations are supported only on Unreal's game thread.
- Linux has an automated lane but no release-level green artifact yet.
- Lyra authority, replication and Game Feature integration begin in 0.4.0.

## [0.2.0] - 2026-08-11

### Added

- UE5 `FField`-based dynamic Python `UClass`, reflected scalar/container/object
  properties and `UFunction` generation, including parent event overrides.
- Enhanced Input adapters for mapping-context ownership, action binding and
  removal, binding inspection, action-value conversion and deterministic input
  injection.
- Runtime access to the authoritative GameMode and safe deferred server travel.
- A separately staged Python-first Third Person variant whose dynamic Python
  GameMode, PlayerController and Character own camera, movement, look, jump and
  locomotion-state behavior.
- A minimal no-gameplay C++ sample host target so packaged source-plugin builds
  link and stage UnrealEnginePython deterministically.
- Automated reference-Blueprint inventory, headless gameplay input/travel smoke
  and packaged Win64 gameplay smoke workflows.

### Changed

- Re-enabled dynamic class generation for UE 5.8 and finalized generated
  classes only after their Python properties/functions and constructors are
  installed.
- Configured the UEP-owned isolated interpreter not to write bytecode caches
  beside staged project or engine scripts.
- Updated property discovery and conversion order for UE5 `FFieldClass`
  declarations, UFunction return/input order and supported array/map/object,
  struct and enum declarations.
- Suppressed editor property-change notifications during UObject construction
  so Python constructors can initialize default subobjects safely.
- Added the Enhanced Input plugin/module as an explicit runtime dependency.
- Promoted dynamic class/function and Enhanced Input lifecycle cases into the
  required core suite; only generic `TSet` marshalling remains a known core
  exclusion.

### Validation

- Shared and UEP-owned CPython 3.11 core suites pass with dynamic reflected
  class construction, two-input return functions and a real
  `BlueprintNativeEvent` override.
- The Python-first Third Person smoke verifies the three active Python classes,
  two mapping contexts, five bindings, injected movement/look/jump, camera
  components, animation state changes and same-map travel.
- The unchanged UE 5.8 reference assets are audited as four Blueprints with 27
  graphs and 173 nodes; the level and content assets remain unmodified.
- Unreal Engine source remains unchanged.

### Known limitations

- Generic `TSet` property marshalling is not implemented.
- Win64 keyboard/mouse is the release-level gameplay contract; mobile touch,
  non-Windows release lanes, networking and Lyra integration remain later
  milestones.

## [0.1.0] - 2026-08-11

### Added

- A repeatable UE 5.8 validation host and PowerShell driver covering editor,
  standalone-interpreter and packaged-runtime execution.
- Machine-readable reports for core reflection, properties, containers,
  functions, delegates, garbage collection, Actor lifecycle, asset persistence
  and Slate ownership.
- A staged UE 5.8 Third Person template demo with visible Python-driven pickups,
  score waves, HUD output and a moving companion.
- Plugin packaging filters for the release documentation, validation project
  and demo sources.

### Changed

- Ported the runtime and editor modules from legacy UE4 APIs to UE 5.8 APIs,
  including the `FField`/`FProperty` reflection model and current editor,
  animation, Sequencer, Slate and asset interfaces used by the plugin.
- Replaced external Python discovery and linkage with UnrealBuildTool's
  engine-provided `Python3` module and CPython 3.11 runtime.
- Added interpreter ownership handling so UEP can share Epic's initialized
  Python VM or initialize and shut down the VM itself when Epic Python is
  disabled.
- Delayed editor companion modules until post-engine initialization so both
  interpreter ownership modes load consistently.

### Validation

- UE 5.8.0 Development Editor and Game targets build and link on Win64.
- The full acceptance run passes 40 required checks across shared, standalone,
  editor and packaged modes with CPython 3.11.8.
- The Third Person smoke test reaches a Game world, creates all six pickups,
  moves the companion and completes its required Python ticks.
- No Unreal Engine source modification is required.

### Known limitations

- Dynamic Python-generated `UClass` and `UFunction` synthesis is disabled while
  it is rebuilt for UE5's `FField` model.
- Generic `TSet` property marshalling is not implemented.
- Only Windows/Win64 has release-level validation in 0.1.0.
- The Third Person sample layers Python gameplay over the original template;
  it does not yet replace the template's Blueprint control logic.
