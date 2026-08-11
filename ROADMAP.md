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

Status: next.

- Add generic `TSet` property marshalling and promote it into the required core
  suite.
- Expand lifecycle, reload, exception, threading and long-running stress tests.
- Audit the remaining editor, animation, Slate, Sequencer, networking and asset
  wrappers for UE5 behavior rather than compile-only compatibility.
- Add at least one non-Windows build/runtime validation lane before claiming
  broader platform support.
- Define reproducible source and optional binary release packaging.

## 0.4.0 — Lyra integration

Status: planned after 0.3.0 hardening is complete.

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
