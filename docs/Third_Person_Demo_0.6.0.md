# Python-first Third Person demo for 0.6.0

## Scope

Version 0.6.0 turns the earlier template-parity proof into a small playable
sample. Unreal Engine's UE 5.8 Third Person map, mannequin, Enhanced Input and
animation assets remain unchanged data. Python owns the generated GameMode,
PlayerController and Character, camera construction, input callbacks, locomotion
state selection, collectible round loop, companion motion and viewport HUD.

The demo is staged into `.build/Demos/UEPPythonThirdPerson`. It does not edit the
engine template or Unreal Engine source. The runtime uses the engine-bundled
CPython 3.11.

## Runtime layout

Runtime and automation no longer share one large module:

| Module | Responsibility |
| --- | --- |
| `bootstrap.py` | roots the generated classes and starts runtime/optional smoke tickers |
| `character.py` | Character components, movement, look, jump and animation state |
| `controller.py` | local Enhanced Input mapping-context ownership |
| `game_mode.py` | selects the generated Character and PlayerController |
| `state.py` | Unreal-independent scoring and round transitions |
| `gameplay.py` | collectible and companion actors plus the playable round |
| `hud.py` | one hit-test-invisible Slate tree attached to the game viewport |
| `runtime.py` | one gameplay/HUD session per active world and teardown |
| `smoke.py` | opt-in automated input, collection, travel and cleanup test |

`uep_python_third_person.py` remains as a compatibility import only. Normal
startup imports `uep_third_person.bootstrap` directly.

## Play contract

The player can move, look and jump using the official template Input Actions.
Six floating Python-spawned orbs form one round. Collecting all six enters a
visible round-complete state, then starts a new round after a short countdown.
The Python HUD displays the objective, round score, total score, timer, movement
speed and current Python animation state. A small companion actor demonstrates
continuous Python-owned world behavior.

The HUD is a real Slate widget in the game viewport, not an on-screen debug
message. It is removed when its world session closes and recreated exactly once
after travel.

## Lightweight checks

Stage or refresh the disposable project without compiling:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Mode Prepare `
    -Incremental
```

The round state can be tested without building Unreal:

```powershell
$env:PYTHONDONTWRITEBYTECODE = "1"
& F:\UnrealEngine\Engine\Binaries\ThirdParty\Python3\Win64\python.exe `
    -m unittest discover `
    -s Demos\UEPPythonThirdPerson\Tests `
    -p "test_*.py" `
    -v
```

## Unreal validation

Fast runtime validation performs an incremental Editor-target build and then a
headless Standalone smoke test; it does not Cook/package:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Mode Smoke `
    -Incremental
```

The smoke test sends `W`, `MouseX`/`MouseY` and `SpaceBar` through PlayerInput and
the official Input Mapping Contexts; it does not call direct InputAction
injection. It observes the resulting Python callbacks, movement, camera rotation
and jump arc, checks locomotion/jump/fall/land states, collects all six gameplay
orbs, checks the Slate HUD, performs same-map travel, verifies a clean new round,
then explicitly releases the HUD, input bindings and runtime session. Every
Demo process disables Android File Server, UDP Messaging and TCP Messaging and
does not use gameplay networking. The demo Game target sets
`bEnableTrace = false`, so Development packages do not open UE's TCP 1985 trace
control listener and do not acquire a new Firewall identity on every timestamped
package path. This is a project Target rule and does not modify Engine source.

The shared `UnrealEditor.exe` target keeps Engine tracing enabled. `Mode Play`
therefore uses the stable Editor executable path. On a machine where that path
has not been classified yet and remote Unreal Insights control is not required,
an administrator may create an explicit inbound block rule:

```powershell
New-NetFirewallRule `
    -DisplayName "UEP Validation - Block Unreal Trace Control 1985" `
    -Group "UnrealEnginePython5 Validation" `
    -Direction Inbound `
    -Action Block `
    -Protocol TCP `
    -LocalPort 1985 `
    -Profile Any
```

The demo driver does not change firewall policy itself and never broadens
network access.

Interactive Standalone play is:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Mode Play `
    -Incremental
```

Formal release promotion additionally requires `-Mode Audit` and `-Mode
Package`, a visible Standalone capture, CPython 3.11, clean strict logs and an
unchanged engine/template source tree.

The first clean `Package` run against a source-built engine may compile a large
Game-target dependency graph into the disposable project. It does not edit
Engine source. Subsequent `-Incremental` runs reuse those project-local objects;
`Prepare` never compiles, while `Smoke` builds the Editor target but does not
Cook or package.

## Current status

Version 0.6.0 is complete. Headless UE 5.8 Standalone result
`20260824-211718` passes the schema-5 runtime contract under engine-bundled
CPython 3.11.8, including the loaded UEP 0.6.0 descriptor, mapped `W`,
`MouseX`/`MouseY` and `SpaceBar` delivery, all six pickups, HUD recreation after
travel and verified HUD/input/runtime teardown. Blueprint audit result
`20260824-211829` passes with the retained four-Blueprint, 27-graph, 173-node
reference baseline. Win64 package result `20260824-203312` records UAT
`Build=False`, archives 1754 Pak entries and passes the same schema-5 contract
from the inner packaged target with zero release-blocking UAT/runtime
diagnostics. Visible 1280x720 Play result `20260824-044028` confirms the rendered
Python HUD, physical mouse look and the SpaceBar jump/fall/land sequence; the
final trace-disabled packaged executable also relaunches with zero TCP listeners
and no Windows Security picker.
