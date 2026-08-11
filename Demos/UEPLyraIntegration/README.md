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
works. The complete external project at
`F:\LyraStarterGame\LyraStarterGame.uproject` passes strict readiness, real
Experience gameplay, synchronized multiplayer, cook/package and packaged
CPython validation without modifying the reference tree.

To repeat every 0.4.0 gate against a complete project outside the engine tree:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode All
```

The server and client each publish a readiness marker, then wait behind one
driver-owned release signal. The server proves a real remote controller,
authoritative pawn, PlayerState and ASC while the client proves Client net mode,
an autonomous local pawn, replicated PlayerState, Enhanced Input and ASC. Only
after both markers exist can either process shut down.

Each process latches the snapshot that produced its readiness marker while it
waits on the release signal. A client that exits one tick earlier therefore
cannot erase the server's already-proven remote Pawn/PlayerState/ASC state.

The gameplay contract requires `ShooterCore` to be Active and the content-only
`ShooterMaps` plugin to be at least Registered, matching its
`ExplicitlyLoaded`/`BuiltInInitialFeatureState=Registered` descriptor. It does
not force feature activation from Python.
