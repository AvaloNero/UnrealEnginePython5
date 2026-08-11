# UE5 wrapper audit for 0.3.0

The 0.3.0 audit distinguishes an actual UE5.8 call from compile-only coverage.
The table records the automated release test for each targeted wrapper area.

| Area | Runtime evidence | Scope boundary |
| --- | --- | --- |
| Editor/UObject | Spawn, call and destroy `UEPValidationActor` in the editor world | Unattended NullRHI editor |
| Assets | Create, save and reload a Material and Level Sequence | Test files live only in the guarded staging project |
| Animation | Construct `AnimSequence`, query its skeleton and round-trip a `BlendSpace` parameter through reflected deep copy | Does not claim imported skeletal animation authoring parity |
| Slate | Create list/tree/combo widgets and verify exact Python item reference acquire/release | Headless ownership/lifetime, not visual styling |
| Sequencer | Create/save a Level Sequence; add/list/remove a master track; add/list a section; create/list a folder; set ranges | Editor asset API, not playback/render output |
| Networking | Bind a non-blocking UDP socket to loopback, start/stop its receiver thread, close twice and reject restart after close | No external network, replication or Python receive callback claim |

The corresponding cases are in
`Validation/UEP58Host/Tests/core_validation.py` and
`Validation/UEP58Host/Tests/editor_validation.py`. Compile/link validation still
covers all plugin modules, but modules not listed above do not gain a behavioral
support claim solely from compiling.

Networking here means the legacy `FSocket` wrapper's local lifecycle. Unreal
multiplayer authority, RPC semantics and replication are separate systems and
remain part of the Lyra-era work.

The combined packaged audit also runs the animation round trip before dynamic
`FString` call-frame stress. This ordering is intentional: it reproduced the
old shallow `FBlendParameter` copy as exit code `777003` during shutdown. The
fixed gate requires process exit code 0 plus `Object subsystem successfully
closed`, `Goodbye Python` and `Log file closed` markers, so a pre-shutdown JSON
pass cannot hide a destructor-time corruption.
