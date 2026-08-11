# UE 5.8 Third Person demos

The demo driver stages assets from Unreal Engine's official Blueprint Third
Person template under `.build/Demos`, copies the current plugin into that
disposable project and builds it against the configured engine. It never edits
the engine, the engine template, or the reference assets in place. Packaging
uses UE's loose cooked-package writer so Cook and Stage are not coupled to a
transient local Zen process.

Two variants are intentionally kept:

| Variant | Purpose | Runtime gameplay owner |
| --- | --- | --- |
| `Overlay` | 0.1.0 compatibility sample | Official Blueprint template plus Python pickups and companion |
| `PythonFirst` | 0.2.0 acceptance sample | Dynamic Python GameMode, PlayerController and Character |

## Python-first gameplay

The 0.2.0 implementation is
`UEPPythonThirdPerson/Overlay/Content/Scripts/uep_python_third_person.py`.
At startup it creates transient reflected classes and roots them for Unreal's
garbage collector:

- `UEPThirdPersonGameMode` selects the Python pawn and controller classes;
- `UEPThirdPersonPlayerController` installs the official default and mouse-look
  Enhanced Input mapping contexts;
- `UEPThirdPersonCharacter` configures collision and movement, creates the
  spring arm and follow camera, binds move/look/jump actions, and executes those
  actions in Python;
- the character computes speed, falling and landing state in `ReceiveTick` and
  drives the retained blend space and animation sequences through a single-node
  animation instance.

The level, mannequin mesh, Input Actions, Input Mapping Contexts, blend space
and animation sequences remain Unreal assets. The original character,
controller, GameMode and AnimBlueprint assets are retained only as the
unchanged reference set; the Python-first game URL selects the transient Python
GameMode instead.

The overlay also contains an empty `UEPPythonThirdPerson` primary C++ module and
Game/Editor target files. They turn Epic's content-only template into a source
project so UBT can link and stage the source plugin in packaged builds; they do
not implement gameplay or initialize Python.

See [`UEPPythonThirdPerson/BLUEPRINT_INVENTORY.md`](UEPPythonThirdPerson/BLUEPRINT_INVENTORY.md)
for the migration boundary and the audited reference graph counts.

## Automated checks

All commands run from the repository root. Add `-Incremental` after the first
run to retain generated project build data.

Audit the unchanged Blueprint reference assets:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant PythonFirst `
    -Mode Audit `
    -Incremental
```

Run the headless gameplay smoke test:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant PythonFirst `
    -Mode Smoke `
    -Incremental
```

The Python-first smoke contract requires CPython 3.11, the three Python runtime
classes, both mapping contexts, exactly five action bindings, injected movement
and look response, Started/Completed jump callbacks, a real jump arc, camera
components, the observed `locomotion`, `jump`, `fall` and `land` animation-state
sequence, and a successful same-map server travel. JSON reports and full logs
are written under `.build/Demos/Results/<timestamp>`.

Cook, package and execute the same contract in the packaged Win64 build:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant PythonFirst `
    -Mode Package `
    -Incremental
```

## Play the Python-first demo

Launch a 1280 x 720 standalone window:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Variant PythonFirst `
    -Mode Play `
    -Incremental
```

Use WASD to move, the mouse to orbit the camera, and Space to jump. The on-screen
status displays the speed and Python locomotion state. The staged project is
`.build/Demos/UEPPythonThirdPerson/UEPPythonThirdPerson.uproject`.

## 0.1.0 overlay regression

The original integration sample remains available by omitting `-Variant` or
passing `-Variant Overlay`. Its smoke test requires the six Python pickups and
moving cube companion while the official Blueprint template continues to own
the base character controls.

## Lyra integration development

`UEPLyraIntegration` is the 0.4.0 project-side bridge and Python observation
probe. It is not a Lyra rewrite: Experience activation, Game Feature actions,
Enhanced Input ownership, ability grants and replication stay in Lyra C++ and
assets. The current automated lane proves the bridge against Lyra's UE5.8
source while readiness automation rejects the content-free Git sample for real
gameplay claims. See
[`UEPLyraIntegration/README.md`](UEPLyraIntegration/README.md) and
[`../docs/Lyra_Integration_0.4.0.md`](../docs/Lyra_Integration_0.4.0.md).
