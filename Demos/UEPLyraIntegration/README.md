# UEP Lyra integration

This integration is staged into a disposable Lyra copy; neither Lyra nor
Unreal Engine source is edited. `UEPLyraBridge` translates the few native Lyra
lifecycle observations and the one bounded write operation that Python cannot
bind safely into reflected, game-thread-only APIs:

- Experience readiness becomes a dynamic multicast event;
- Game Feature state, Enhanced Input readiness, Ability System/Health readiness,
  damage immunity, authority, net mode, local/remote controllers, PlayerState
  and pawn roles are exposed as a read-only snapshot;
- the subsystem and its listeners follow `UWorld` lifetime, and the Python
  probe explicitly unbinds its callback before shutdown or reload; late client
  `GameState` assignment is observed before binding the Experience manager;
- `ApplyAuthorityHealthDelta` accepts only game-thread server authority, one
  unambiguous local/remote player target, a short safe command ID, a finite
  non-zero delta no larger than 25, non-lethal damage and non-overhealing
  restoration;
- accepted commands dispatch Lyra's existing SetByCaller Damage/Heal Gameplay
  Effects through the target ASC. The bridge never writes the Health attribute
  directly and respects Lyra's `Gameplay.DamageImmunity`; and
- command IDs are consumed once, clients receive `RejectedNotAuthority`, and no
  bridge API silently forwards a client call as an RPC.

The source-only smoke lane disables Lyra's content-backed startup systems in
the disposable stage so it can prove UE5.8 compilation, module loading,
CPython 3.11 ownership and bridge lifecycle without pretending that gameplay
works. The complete external project at
`F:\LyraStarterGame\LyraStarterGame.uproject` passes strict readiness, real
Experience gameplay, synchronized multiplayer, cook/package and packaged
CPython validation without modifying the reference tree.

To repeat every 0.5.0 gate against a complete project outside the engine tree:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode All
```

The Python probe owns the gameplay sequence. In Standalone and the packaged
game it waits for Lyra damage immunity to end, applies 10 damage, proves the
same command ID cannot apply twice, observes 90 health, heals 10 and observes
the original 100 health. In the network lane:

1. the client calls the same reflected API and proves `RejectedNotAuthority`
   without a health change;
2. the dedicated-server Python process applies the damage to the real remote
   player's ASC and proves duplicate rejection;
3. the client Python process observes replicated health at 90 before
   acknowledging the server; and
4. server Python restores health only after that acknowledgement, then waits
   until client Python observes the replicated value at 100.

Both peers then publish a readiness marker and wait behind one driver-owned
release signal. Only after both markers exist can either process shut down.

Each process latches the snapshot that produced its readiness marker while it
waits on the release signal. A client that exits one tick earlier therefore
cannot erase the server's already-proven remote Pawn/PlayerState/ASC state.

The gameplay contract requires `ShooterCore` to be Active and the content-only
`ShooterMaps` plugin to be at least Registered, matching its
`ExplicitlyLoaded`/`BuiltInInitialFeatureState=Registered` descriptor. It does
not force feature activation from Python. Runtime URLs request three Lyra AI
bots to advance the real Warmup phase; bots use `AModularAIController` and are
not candidates for the remote `APlayerController` target selector.

See [`../../docs/Lyra_Gameplay_Slice_0.5.0.md`](../../docs/Lyra_Gameplay_Slice_0.5.0.md)
for the ownership table, rejection contract and machine-readable evidence.
Formal result `20260812-234501` passed the complete `All` lane, including the
packaged game, and reported `full_acceptance: true`. After the selector was
hardened to reject ambiguous human targets, exact-source result
`20260813-005436` passed the Network lane and `20260813-005835` passed
Standalone. No package was rebuilt after that small hardening change; the
former result remains package evidence and the latter two are current-source
runtime evidence.
