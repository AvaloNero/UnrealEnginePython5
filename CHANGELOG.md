# Changelog

This file records the UE5 port maintained in this repository. The historical
UE4 project history remains available in Git.

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
