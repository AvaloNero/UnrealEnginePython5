# Changelog

This file records the UE5 port maintained in this repository. The historical
UE4 project history remains available in Git.

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
