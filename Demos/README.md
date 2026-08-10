# UEP Third Person Python demo

This demo combines the official Unreal Engine 5.8 Blueprint Third Person
template with the current UnrealEnginePython plugin. The template assets are
read from the configured engine and staged under `.build/Demos`; Unreal Engine
source and template directories are never modified.

In version 0.1.0 this is deliberately an integration demo, not a rewrite of the
template. The official Blueprint character, Enhanced Input setup, animation
Blueprint and map remain intact; Python adds a separate gameplay layer. Moving
the template's control logic into Python is tracked as the 0.2.0 milestone in
[`../ROADMAP.md`](../ROADMAP.md).

The visible gameplay is implemented in
`UEPThirdPersonDemo/Overlay/Content/Scripts/uep_third_person_demo.py`:

- a Python ticker waits for a Game or PIE world;
- six floating pickups are spawned around the player and animated every frame;
- a cube companion orbits the Third Person character;
- touching a pickup updates the score and completing a wave spawns another;
- an on-screen HUD confirms that the behavior is coming from UEP Python.

## Automated smoke test

From the repository root:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Mode Smoke
```

The smoke test builds the staged source plugin, starts the template in game
mode with `NullRHI`, and requires Python 3.11, six live pickups, a moving
companion, and all three lifecycle log markers. Results are written under
`.build/Demos/Results/<timestamp>`.

Use `-Incremental` after the first run to retain generated project build data.

## Play the demo

Launch the game directly:

```powershell
.\Demos\Run-UEPThirdPersonDemo.ps1 `
    -EngineRoot F:\UnrealEngine `
    -Mode Play `
    -Incremental
```

Alternatively, use `-Mode Editor`, press Play, and inspect the Python log in
the Output Log. The generated project is
`.build/Demos/UEPThirdPersonDemo/UEPThirdPersonDemo.uproject`.
