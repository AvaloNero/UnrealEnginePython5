# UEP Lyra integration

This integration is staged into a disposable Lyra copy; neither Lyra nor
Unreal Engine source is edited. `UEPLyraBridge` translates the few native Lyra
lifecycle observations that Python cannot bind safely into reflected,
game-thread-only APIs:

- Experience readiness becomes a dynamic multicast event;
- Game Feature state, Enhanced Input readiness, Ability System readiness,
  authority, net mode, local/remote controllers, PlayerState and pawn roles are
  exposed as a read-only snapshot;
- the subsystem and its listeners follow `UWorld` lifetime, and the Python
  probe explicitly unbinds its callback before shutdown or reload; late client
  `GameState` assignment is observed before binding the Experience manager;
- no API activates a feature, grants an ability, changes input, writes a
  replicated property or bypasses server authority.

The source-only smoke lane disables Lyra's content-backed startup systems in
the disposable stage so it can prove UE5.8 compilation, module loading,
CPython 3.11 ownership and bridge lifecycle without pretending that gameplay
works. Full Experience, multiplayer, cook and packaged gates remain blocked
until the Launcher/Marketplace `Content` trees are present.

Once a complete project exists outside the engine tree, run every 0.4.0 gate:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\UEProjects\Lyra\Lyra.uproject `
    -Mode All
```

The server and client each publish a readiness marker, then wait behind one
driver-owned release signal. The server proves a real remote controller,
authoritative pawn, PlayerState and ASC while the client proves Client net mode,
an autonomous local pawn, replicated PlayerState, Enhanced Input and ASC. Only
after both markers exist can either process shut down.
