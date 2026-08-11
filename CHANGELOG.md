# Changelog

This file records the UE5 port maintained in this repository. The historical
UE4 project history remains available in Git.

## [Unreleased] - 0.4.0 Lyra integration

### Added

- A disposable project-side `UEPLyraBridge` world subsystem that converts
  Lyra's native Experience completion callback into a reflected dynamic event
  and exposes read-only Game Feature, Enhanced Input, Ability System, authority
  and net-mode snapshots.
- A Python lifecycle probe with explicit callback unbinding, game-thread checks,
  role-aware assertions, machine-readable results and orderly process shutdown.
- Lyra source/readiness drivers that stage outside the reference project,
  require UE5.8 and engine-bundled CPython 3.11, and distinguish source
  compatibility from content-dependent acceptance.
- A strict full-content Lyra driver covering isolated staging, Editor build,
  real Experience/Game Feature readiness, Standalone pawn/input/GAS state,
  dedicated-server/client authority and replication, Win64 cook/package and
  packaged CPython runtime.
- Network-observation fields for local/remote controllers, PlayerState, pawn
  control/roles and a readiness hold window so a server cannot terminate before
  its validating client finishes.

### Changed

- `PythonEditor` project-tree ownership now uses `TObjectPtr`; a separate
  non-reflected raw-pointer view feeds Slate. This satisfies Lyra's strict
  UE5.8 native-pointer target policy without weakening it.

### Validation

- Clean source result `20260811-165230` built `LyraEditor` and all staged UEP
  modules, loaded `UEPLyraBridge`, ran 12 Standalone game-world ticks on
  CPython 3.11.8 and exited 0 with zero compiler, fatal and `Log*: Error`
  diagnostics. Unreal Engine source remained unchanged.
- Incremental regression result `20260811-174727` rebuilt the final reflected
  network snapshot through UBT and repeated the source runtime gate
  successfully.
- Readiness result `20260811-180834` reports source ready but content blocked:
  the local Lyra Git sample contains zero assets/maps and lacks all five
  required GameFeatureData assets.
- Full-driver negative result `20260811-180836` stopped before staging with 11
  machine-readable content blockers, proving that source-only evidence cannot
  accidentally satisfy the full acceptance lane.

### Not yet accepted

- Version 0.4.0 is not released. Experience/Game Feature activation, real Lyra
  pawn/input/GAS behavior, client/server replication, cook, package and
  packaged runtime require a complete external Launcher/Marketplace Lyra
  project and remain unclaimed.

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
