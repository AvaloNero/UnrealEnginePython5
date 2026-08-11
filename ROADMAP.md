# Roadmap

This roadmap describes capability milestones, not promised delivery dates.
Each version advances only after its acceptance criteria are automated and pass
without modifying Unreal Engine source code. UE's binary content assets remain
assets unless replacing them with generated data provides a concrete benefit;
the goal is to move behavior and control logic into Python.

## 0.1.0 — UE 5.8 foundation

Status: complete.

- Build all runtime and editor plugin modules against UE 5.8.
- Use the engine-bundled CPython 3.11 runtime.
- Support both shared and UEP-owned interpreter lifecycles.
- Validate reflection, common property types, functions, delegates, garbage
  collection, editor objects and packaged runtime behavior.
- Provide a Third Person integration demo with additional Python gameplay.

Acceptance gate: the four-suite validation run passes all 40 required checks,
the Third Person smoke test passes, and neither workflow changes engine source.

## 0.2.0 — Python-first Third Person

Status: complete.

- Rebuild dynamic Python `UClass`/`UFunction` generation on UE5's `FField`
  reflection model where it is required for Python subclasses and event
  overrides.
- Produce a Python-first variant of the official UE 5.8 Third Person template:
  character/Pawn lifecycle, camera boom and control rotation, Enhanced Input
  mapping setup, move/look/jump actions, PlayerController/GameMode bootstrap and
  locomotion state calculation move out of Blueprint event logic.
- Drive the animation instance from Python while retaining skeletal meshes,
  animation clips and pose-blending assets as Unreal assets. Blueprint graphs
  should be removed or reduced to asset-only glue with documented reasons.
- Keep the reference Blueprint template unchanged and stage the Python variant
  separately so behavior can be compared automatically.
- Add deterministic tests for input binding, movement, jump, camera response,
  animation state changes, map travel, standalone play and packaged execution.

Acceptance gate: the Python-first variant reaches feature parity with the basic
Third Person template for keyboard/mouse play, its remaining Blueprint logic is
inventoried, and all gameplay checks pass in visible and headless modes.

Completed gate: UE 5.8.0 and Python 3.11.8 passed 46 shared, standalone,
editor and packaged core checks. The Python-first demo passed Blueprint audit,
headless smoke, packaged smoke and a rendered Standalone capture while the
unchanged template overlay continued to pass its regression smoke test.

## 0.3.0 — API and platform hardening

Status: complete.

- Add generic `TSet` property marshalling and promote it into the required core
  suite.
- Expand lifecycle, reload, exception, threading and long-running stress tests.
- Audit the remaining editor, animation, Slate, Sequencer, networking and asset
  wrappers for UE5 behavior rather than compile-only compatibility.
- Add at least one non-Windows build/runtime validation lane before claiming
  broader platform support.
- Define reproducible source and optional binary release packaging.

Acceptance gate: the six-suite Win64 matrix passes all 67 required checks,
including cooked packaged execution and orderly interpreter/process shutdown;
the wrapper audit has behavioral coverage, platform claims match retained
artifacts, and release archives are generated from a clean named commit.

Completed gate: UE 5.8.0 and CPython 3.11.8 passed 20 shared core, 20
standalone core, two isolated exception-boundary, five editor and 20 packaged
core checks in full result `20260811-153622`. The run exposed and fixed a
`BlendSpace` parameter shallow-copy shutdown corruption, then completed with
zero fatal, compiler or unexpected error diagnostics and process exit code 0.
Linux remains explicitly unclaimed: readiness result `20260811-154243` reports
the required `v26_clang-20.1.8-rockylinux8` SDK is not installed locally.

## 0.4.0 — Lyra integration

Status: in progress. The UE5.8 source/bridge gate passes; content-dependent
acceptance is blocked on a complete Launcher/Marketplace Lyra project.

- Start with a capability audit of Lyra's Game Feature plugins, modular gameplay,
  Enhanced Input, Gameplay Ability System, asset management and multiplayer
  lifecycle boundaries.
- Introduce narrow Python extension points with explicit thread, authority,
  replication and hot-reload rules instead of attempting an immediate Lyra
  rewrite.
- Validate editor startup, Game Feature activation, client/server play, cook and
  package behavior against an unchanged Lyra reference project.
- Expand Python ownership only where performance and lifecycle measurements show
  it is safe and maintainable.

Lyra is intentionally not a 0.2.0 acceptance dependency.

Source gate result `20260811-165230` built a clean disposable `LyraEditor`
stage, linked `UEPLyraBridge`, started a game world with UEP-owned CPython
3.11.8, captured a game-thread/Standalone snapshot and shut down with zero
compiler, fatal or `Log*: Error` diagnostics. The readiness result
`20260811-172340` reports `source_ready: true` and `content_ready: false`: the
local Git sample contains zero `.uasset` and zero `.umap` files, including no
project GameData/maps or GameFeatureData for its five feature plugins.
Final lifecycle/UBT regression result `20260811-172252` also passed.

0.4.0 is not complete and the main plugin version must not be bumped until a
complete external Lyra project passes Experience activation, gameplay input and
Ability System readiness, client/server authority and replication, cook,
package and packaged-runtime gates. The reference project and Unreal Engine
source must remain unchanged throughout those runs.
