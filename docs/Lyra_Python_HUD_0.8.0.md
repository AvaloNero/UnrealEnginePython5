# Lyra Python HUD foundation for 0.8.0

UEP 0.8.0 moves the behavior of Lyra's existing health bar into Python without
replacing `ALyraHUD`, bypassing CommonUI, changing the widget class, or editing
the Lyra/Engine source trees. The disposable validation stage overlays a
configured copy of `/Game/UI/Hud/W_Healthbar`; the live object is still
`W_Healthbar_C` and is still created and removed by Lyra's Game Feature
`Add Widgets` action.

## Ownership boundary

| Concern | Owner in 0.8.0 |
| --- | --- |
| HUD layout, CommonUI layers and widget creation/removal | Lyra Game Feature actions |
| Health authority, Gameplay Effects and replication | Lyra GAS and `ULyraHealthComponent` |
| Widget `Construct`/`Destruct`, controller and health subscriptions | Python mixin presenter |
| Current-pawn selection and rebinding after possession changes | Python mixin presenter |
| Health normalization and refresh timing | Python mixin presenter |
| Dynamic materials, bar animation and numeric widget primitives | Existing `W_Healthbar` Blueprint functions |
| Dedicated-server UI | None; the acceptance contract requires zero widget/presenter instances |

The presenter is intentionally narrow. It reads replicated health and invokes
Lyra's existing visual helper functions; it does not set attributes, activate
an Experience, grant abilities, replace input mappings, or move gameplay
authority to Python. The bounded 0.5 health-delta bridge remains responsible
for producing the validation sequence through Lyra's native Gameplay Effects.

## Mixin declaration and selection

The Examples repository owns two overlay assets:

- `/Game/UI/Hud/W_Healthbar`, a same-path copy of the official widget with
  `UEPPythonMixinInterface` and the `PythonMixinProfile` selector added;
- `/Game/UEPMixins/DA_LyraHealthbarMixin`, whose `Python` and
  `BlueprintFallback` profiles point to the two presenter classes.

The default `Python` profile replaces the widget's reflected `Construct` and
`Destruct` functions at runtime. `BlueprintFallback` calls the preserved
Blueprint `Construct`, providing a per-instance escape hatch without changing
the object's Unreal type. The original Blueprint graphs remain in the copied
asset and are restored when the router unregisters.

The assets are generated only in a marker-protected disposable copy of Lyra:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -OutputRoot F:\UnrealEnginePython5\.build\LyraValidation\HUD08 `
    -Mode GenerateHUDAssets `
    -Incremental
```

The generator resets the disposable stage to the official health-bar package,
creates a fresh Mixin DataAsset, compiles and saves both staged assets, then
copies only those two verified files into the Examples overlay. It compares
the SHA-256 of the reference `W_Healthbar.uasset` before and after and never
writes to `F:\LyraStarterGame` or `F:\UnrealEngine`. Unreal package-save
metadata is generation-specific, so acceptance checks semantic configuration
and runtime behavior rather than requiring byte-identical generated uassets.

## Runtime sequence

1. `ue_site.py` skips all runtime registration in Cook and other commandlets.
2. A launched game loads the staged `W_Healthbar_C` and registers the Mixin Set
   declared by its Interface.
3. Lyra activates its normal Experience and Game Feature action, which creates
   `W_Healthbar_C` through CommonUI.
4. The routed `Construct` keeps the exact callback objects required for later
   unbinding, creates Lyra's dynamic materials through the existing Blueprint
   helper, subscribes to `APlayerController.OnPossessedPawnChanged`, then binds
   the current Pawn's `OnHealthChanged` and `OnMaxHealthChanged` delegates.
5. Python reads health/max health, clamps the normalized value, updates the
   widget state and calls the existing Blueprint visual primitives.
6. A possession change unbinds the old Health Component before binding the new
   one. `Destruct`, router teardown and interpreter shutdown all clear health
   and controller delegates before releasing presenter state.

No Tick poll drives the health bar. The initial render is explicit; later
updates are caused by Health Component or possession delegates.

## Acceptance contract

The 0.8 full-content lane must prove all of the following on UE 5.8 and the
engine-bundled CPython 3.11:

- seven host-side presenter/bootstrap tests cover source-only asset absence,
  exact callback identity, event-driven rendering, Pawn replacement, Blueprint
  fallback, Destruct and registry teardown before an Unreal process is launched;
- generation preserves the untouched reference widget, while the read-only
  audit proves that the staged copy declares the expected Interface, Mixin Set,
  selector, profiles, reflected state and ShooterCore `Add Widgets` shape;
- Standalone and a network client render the real replicated
  `100 -> 90 -> 100` sequence from delegate callbacks;
- an authority-driven Pawn restart replaces the Pawn and produces exactly one
  active binding to the new Health Component;
- a first same-map Experience replacement drives the real
  `LAS_ShooterGame_StandardHUD` `Add Widgets` deactivation/activation path,
  tears down the old widget and reconstructs one clean presenter;
- a second independent same-map travel repeats teardown/reconstruction, so a
  client must finish with at least three Constructs, two Destructs and one
  balanced active presenter;
- a dedicated server records zero health-bar constructs, subscriptions or
  health presentation events;
- explicit widget destruction, mixin teardown and Python shutdown leave no
  stale callback balance; and
- strict logs contain no compiler, fatal, assertion, unhandled-exception or
  unexpected `Log*: Error` diagnostics.

A fresh staged Lyra tree has no asset-registry cache. Before multi-process
runtime tests, the driver completes one headless Editor scan and retains the
resulting read-only cache. This prevents two `Editor -game` processes from
competing over an uncached initial Lyra scan before the client can service its
network handshake. The cache path and byte counts are recorded in the run
summary.

Run the complete lane with:

```powershell
.\Validation\Run-UEP58LyraValidation.ps1 `
    -EngineRoot F:\UnrealEngine `
    -LyraProject F:\LyraStarterGame\LyraStarterGame.uproject `
    -Mode All
```

`All` includes a full Win64 cook/package and can take substantially longer
than `Standalone` or `Network`. Use the narrower modes during iteration.

## Retained acceptance evidence

- HUD audit `20260903-032632`: the official action set has one
  `GameFeatureAction_AddWidgets`, 1 layout entry and 11 widget entries.
- Clean-base asset generation `20260903-033001` and `20260903-033049`: both
  pass the same `W_Healthbar_C`, `Python` default, two-profile and selector
  postconditions; the reference package hash remains unchanged.
- Full run `20260903-033426`: `full_acceptance: true` on UE 5.8.0 and CPython
  3.11.8. Standalone, network client and packaged game each report the real
  `100 -> 90 -> 100` sequence, 3 Constructs and 2 Destructs; the dedicated
  server reports zero HUD state. BuildCookRun and packaged runtime exit 0,
  strict logs pass, and archived/tested executable hashes match.

The complete run was headless; it did not open a visible manual client.

## Deliberate boundary

0.8.0 covers only the health bar and lifecycle foundation. QuickBar,
weapon/ammo, reticle, team score, elimination feed, accolade presentation and
a reusable Gameplay Message subscription API remain 0.9.0 work. The runtime
Mixin API also retains the 0.7 restrictions documented in `Mixin_API.md`.
