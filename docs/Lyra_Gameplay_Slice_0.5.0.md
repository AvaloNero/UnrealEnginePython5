# Python-driven Lyra gameplay slice for 0.5.0

## Scope

Version 0.5.0 advances the 0.4 Lyra bridge from observation to one deliberately
narrow gameplay write. Python chooses the command ID, health delta, target role
and timing. A project-side C++ adapter validates policy and dispatches Lyra's
existing Gameplay Effects. Lyra still owns its Ability System, attributes,
damage execution, game phases and replication.

The implementation is staged into a disposable copy of a complete Lyra project.
It does not edit Unreal Engine source or the external Lyra reference project.
The supported interpreter remains the engine-bundled CPython 3.11.

## Ownership boundary

| Concern | Owner | 0.5 behavior |
| --- | --- | --- |
| command ID, damage/heal value and sequencing | Python probe | chooses a 10-point damage command, waits for replicated observation, then chooses the restore command |
| thread, authority, target and value policy | `UEPLyraBridge` | rejects calls outside the game thread/server, ambiguous targets, unsafe IDs or magnitudes, lethal damage, overheal and duplicates |
| Experience and Game Feature activation | Lyra | Python waits for the native Experience and feature states; it does not activate them |
| damage immunity | Lyra GAS/game phase | the snapshot exposes immunity and the command returns `RejectedDamageImmune`; Python waits instead of bypassing it |
| damage/heal execution | Lyra GAS | bridge creates Lyra's configured SetByCaller Damage/Heal spec and applies it through the target ASC |
| Health storage and replication | Lyra attribute sets | no direct Health property write or custom replication path is introduced |
| client/server synchronization | Python probe plus filesystem evidence | peers use atomic JSON acknowledgements so restore cannot race ahead of client damage observation |

This is a Python-driven vertical slice, not a Python rewrite of Lyra. The
behavioral decision and orchestration are Python; the performance- and
authority-sensitive primitives remain native.

## Reflected command

`UUEPLyraWorldSubsystem::ApplyAuthorityHealthDelta` accepts:

- a command ID containing only letters, digits, `.`, `_` or `-`, up to 96
  characters;
- a finite, non-zero health delta whose absolute value is at most 25; and
- an exact, unambiguous target policy: one local player for Standalone or one
  remote `APlayerController` for the dedicated server.

The command requires:

- Unreal's game thread;
- a non-client world and an authoritative target Pawn;
- an initialized Lyra Pawn Extension, ASC and Health Component;
- damage that leaves at least one Health;
- healing that does not exceed MaxHealth; and
- an unused command ID.

Accepted damage uses `ULyraGameData::DamageGameplayEffect_SetByCaller` with
`LyraGameplayTags::SetByCaller_Damage`. Healing uses the matching configured
Heal effect and tag. The bridge reads Health back immediately and reports
failure if the observed delta is absent or differs from the request.

The result struct records the command/status, accepted/applied flags, server
authority, target path, requested delta and before/after/max Health. Expected
rejections are data, not `Log*: Error` messages.

Important result statuses include:

| Status | Meaning |
| --- | --- |
| `Applied` | Lyra GAS changed Health by the exact requested delta |
| `RejectedNotAuthority` | a client attempted the write; no RPC is generated |
| `RejectedDuplicateCommand` | the authority already consumed that command ID |
| `RejectedDamageImmune` | Lyra's current game phase blocks damage |
| `RejectedAmbiguousTarget` | more than one player controller matches the requested local/remote role |
| `RejectedInvalidCommandId` / `RejectedInvalidMagnitude` | the input is outside the bounded command format |
| `RejectedUnsafeDamage` / `RejectedUnsafeHeal` | the request would kill or over-heal the target |
| `RejectedNoTarget` / `RejectedNoAbilitySystem` / `RejectedNoHealth` | the exact Lyra gameplay target is not ready |
| `FailedNoObservedChange` / `FailedUnexpectedDelta` | the native effect did not produce the promised result |

## Automated sequence

The probe waits for Experience, `ShooterCore`, the correct local/remote role,
Pawn, PlayerState, ASC, Health and the end of Lyra damage immunity. Runtime URLs
request three Lyra AI bots so the real Warmup phase advances; bot controllers
are not `APlayerController` targets.

Standalone and packaged execution prove:

1. Python submits `-10` on authority and receives `Applied` with `100 -> 90`.
2. Python repeats the same command ID and receives
   `RejectedDuplicateCommand` with no second change.
3. A later Python tick observes Health 90 through the snapshot.
4. Python submits `+10`, receives `Applied`, and a later tick observes Health
   restored to 100.

Dedicated-server/client execution proves:

1. Client Python submits `-10`, receives `RejectedNotAuthority`, verifies Health
   stayed 100 and publishes `client-denied.json` atomically.
2. Server Python waits for that evidence, applies `-10` to the exact remote
   player's ASC, proves duplicate rejection and publishes
   `server-damaged.json`.
3. Client Python waits until replicated Health is 90, then publishes
   `client-damage-observed.json`.
4. Server Python waits for that client observation before applying `+10` and
   publishing `server-restored.json`.
5. Client Python waits until replicated Health returns to 100 and publishes
   `client-restore-observed.json`.
6. Both processes complete only after the full sequence, then cross the
   driver's existing shared exit gate.

The dedicated server binds only `127.0.0.1`; the validation does not expose a
listener to the LAN.

## Reproducing the gates

Fast local gameplay iteration (Editor build plus Standalone, no Cook/package):

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode Standalone `
    -Incremental
```

Dedicated-server/client replication lane (no Cook/package):

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode Network `
    -Incremental
```

Formal release acceptance adds a fresh Standalone/network pass, Win64
BuildCookRun and packaged execution:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode All `
    -Incremental
```

The gameplay slice is enabled by default for every runtime mode. Diagnostic
back-compat runs may use `-SkipGameplaySlice`, but such an `All` run cannot set
`full_acceptance: true`. `-GameplaySliceDamage` may select a value from 0.01
through 25; the default and release contract use 10.

## Current evidence

- Standalone result `20260812-233341`: UE 5.8.0, CPython 3.11.8, strict build
  and runtime passed; machine evidence records `Applied`,
  `RejectedDuplicateCommand`, `Applied` and Health `100 -> 90 -> 100`.
- Network result `20260812-233801`: dedicated server and client passed; the
  client records `RejectedNotAuthority`, both roles record the same server GAS
  commands, and client events show replicated Health `100 -> 90 -> 100`.
- Formal `All` result `20260812-234501`: `full_acceptance: true` on UE 5.8.0 and
  CPython 3.11.8. Readiness, Editor, Standalone, loopback server/client, UAT and
  packaged gameplay all exited 0. BuildCookRun reported `BUILD SUCCESSFUL`, the
  internal and stable tested executable hashes matched, and all four runtime
  logs contained the gameplay/normal-close markers with zero strict
  fatal/assert/`Log*: Error` diagnostics.
- Exact-source Network result `20260813-005436`: the final selector hardening
  rebuilt only `UEPLyraBridge`; server and client repeated authority rejection,
  idempotency, replication and restoration with clean shutdown.
- Exact-source Standalone result `20260813-005835`: the bridge build was up to
  date and the local gameplay sequence repeated successfully. No Cook/package
  was run after selector hardening, so `20260812-234501` remains the latest
  package evidence rather than an exact-source package result.

## Intentional limits

0.5 does not expose arbitrary Gameplay Effect classes, raw attribute writes,
feature activation, ability grants, input remapping, generic target selection or
an automatic client-to-server RPC. It does not claim that Lyra's full gameplay
stack has moved to Python. Those omissions keep the first write surface small,
auditable and safe enough to validate before broader Python ownership is
considered.
