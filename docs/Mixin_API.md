# Blueprint mixin API

UEP 0.7 can route selected functions on an existing Blueprint-generated class
to Python without creating a new Unreal class. Placed and spawned objects keep
their original `BP_*_C` identity. One class-level router preserves each original
UFunction, while every UObject instance can select a different named Python
profile.

This differs from Python subclassing: subclassing creates a transient derived
`UClass` and requires a spawn/class-selection change. A mixin patches the loaded
Blueprint class, so instances already in a map participate without being
replaced.

## Named profiles and per-instance routing

Register all profiles that a class may use before gameplay starts:

```python
import unreal_engine as ue
from unreal_engine import FRotator


character_class = ue.load_class(
    "/Game/ThirdPerson/Blueprints/"
    "BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C"
)


@ue.mixin(character_class, profile="Python", make_default=True)
class PythonCharacterProfile:
    def __init__(self):
        self.python_move_events = 0

    def Move(self, right, forward):
        self.python_move_events += 1
        rotation = self.get_control_rotation()
        yaw = FRotator(0.0, 0.0, rotation.yaw)
        self.add_movement_input(ue.get_forward_vector(yaw), forward)
        self.add_movement_input(ue.get_right_vector(yaw), right)


@ue.mixin(character_class, profile="BlueprintMove")
class BlueprintMoveProfile:
    def Aim(self, yaw, pitch):
        self.add_controller_yaw_input(yaw * 0.35)
        self.add_controller_pitch_input(pitch * 0.35)
```

The router installs the union of reflected functions declared by all profiles.
In this example the union is `Move` plus `Aim`. An object using `Python` calls
Python for `Move`; an object using `BlueprintMove` does not implement `Move`, so
the router automatically invokes the preserved Blueprint implementation through
a native property-to-property parameter frame. This preserves native return
values and const reference inputs without a Python conversion round trip. The
fallback is per function and requires no forwarding stub.

Select profiles independently on two objects of the same Blueprint class:

```python
ue.set_mixin_profile(player, "Python")
ue.set_mixin_profile(training_dummy, "BlueprintMove")

assert player.get_class() is training_dummy.get_class()
assert ue.get_mixin_profile(player) == "Python"
assert ue.get_mixin_profile(training_dummy) == "BlueprintMove"
```

`clear_mixin_profile(object)` removes the explicit selection and invalidates the
instance cache. The next mixed call resolves the Blueprint interface selection,
then the class default. `set_default_mixin_profile(class, name)` changes that
fallback and invalidates cached default selections without disturbing explicit
instance selections.

The direct non-decorator API is:

```python
ue.register_mixin_profile(
    character_class,
    "Python",
    PythonCharacterProfile,
    make_default=True,
)
```

The original `register_mixin(class, python_class)` and `@mixin(class)` forms
remain compatible. They replace the complete router with one profile named
`Default`; use the profile API when multiple implementations must coexist.

## Blueprint Interface and Mixin Set

For an asset-authored workflow, add `UEPPythonMixinInterface` to the Blueprint
and create a `UEPPythonMixinSet` Data Asset. The set declares every allowed
profile with:

- `ProfileName`;
- importable `PythonModule`; and
- `PythonClass` (a class name or dotted attribute path in that module).

Set one declared name as `DefaultProfile`. Python classes referenced by a Mixin
Set are plain class definitions; do not decorate them separately.

Implement the two interface functions in the Blueprint:

1. `Get Python Mixin Set` returns the same Data Asset for the class. A constant
   return value is the normal CDO configuration.
2. `Get Python Mixin Profile` returns a `Name`. It may read an
   instance-editable Blueprint variable, allowing two placed instances of the
   same `BP_*_C` to choose different profiles.

Only the Blueprint that directly adds `UEPPythonMixinInterface` owns the class
router. Derived Blueprints inherit that router and the interface selection hook;
they are not registered as a second owner. Put the Interface and complete Mixin
Set on the highest Blueprint class that should share one routed function union.
This matches the rule that related base/derived classes cannot own simultaneous
routers.

UEP scans already loaded Blueprint classes after project startup imports and
also observes package completion in Editor, Game and packaged builds, so a class
loaded later by a map or soft reference is registered before subsequent mixed
calls. It imports the complete Mixin Set and builds one router for the directly
declaring class. `register_declared_mixin(class)` forces registration for one
loaded direct owner; `register_loaded_mixin_interfaces()` rescans all loaded
classes and returns the number of newly registered routers.

Cook commandlets are deliberately excluded from discovery and Python registry
mutation is rejected while Cook is running. A packaged game registers its
declared Mixins only after the runtime process starts. This prevents temporary
routed `UFunction` map entries from being serialized into cooked Blueprint
assets while preserving late package-load discovery in the actual Game process.

Editor automation can create the Interface graphs without manually editing a
Blueprint. Pass three arguments for a constant profile:

```python
ue.blueprint_configure_mixin(
    blueprint,
    mixin_set,
    "BlueprintMove",
)
ue.compile_blueprint(blueprint)
```

Pass a fourth argument to create or reuse a scalar `Name` variable, make it
instance editable, set its default, and wire the profile result to that
variable:

```python
ue.blueprint_configure_mixin(
    blueprint,
    mixin_set,
    "Python",
    "PythonMixinProfile",
)
ue.compile_blueprint(blueprint)
```

`blueprint_configure_mixin` is Editor-only. It adds the Interface directly to
that project Blueprint, sets the constant `GetPythonMixinSet` result and either
sets a constant profile result or wires it to the requested variable. It is
intended for deterministic asset authoring and tests, not runtime mutation. It
refuses to replace an existing profile expression that it cannot identify as
the requested variable getter.

The interface result is cached after an instance's first dispatch. Change it
through `set_mixin_profile`, or update the Blueprint variable and call
`clear_mixin_profile` to re-read the interface. This keeps `Tick`, input and
other hot calls from executing a Blueprint interface graph on every dispatch.

## Dispatch and lifecycle order

For an interface-backed class the normal order is:

1. the Blueprint class loads;
2. its CDO returns a Mixin Set and UEP imports every declared profile;
3. the first mixed call on an object resolves and caches its profile;
4. that profile's local `__init__(self)` runs once for the object/profile
   generation;
5. the selected Python callable runs, or the preserved UFunction runs when the
   profile does not implement it;
6. switching/invalidation calls the old profile's optional local
   `__uep_mixin_teardown__(self)` before initializing the new profile; and
7. unregister or interpreter shutdown tears down initialized instances and
   restores the class function map.

Selector graphs, Python initializers, teardown hooks, mixed Python callables and
explicit/original Blueprint calls are callback boundaries. Registry-changing
APIs are rejected with `RuntimeError` while one of those callbacks is executing,
so a callback cannot invalidate the router/profile references that invoked it.
Nested dispatch to a different object remains supported. Recursive dispatch on
the same object while it is resolving, initializing or tearing down is rejected
with a deterministic busy-state error.

`ReceiveBeginPlay`, `ReceiveEndPlay` and ordinary Construction-script
UFunctions use the same router and can be mixed when their signatures satisfy
the safety contract. Timing still matters: registration cannot replay an event
that already happened. To override Construction deterministically, load and
register the class from an early project import/`ue_site` path before the map or
spawn occurs. Automatic late discovery is sufficient for future calls such as
BeginPlay only when registration finishes before those calls.

Mixing EndPlay and the mixin teardown hook are separate concepts. EndPlay is an
Unreal lifecycle event and may fall back or call its original implementation;
`__uep_mixin_teardown__` releases Python profile state when a router/profile is
switched or removed.

## Helpers and state

The Python wrapper for the UObject is passed as `self`. Profile helpers are
resolved and bound on demand:

```python
class ExampleProfile:
    def __init__(self):
        self.samples = []

    def _remember(self, value):
        self.samples.append(value)

    def SomeBlueprintFunction(self, value):
        self._remember(value)
```

`self` remains an `unreal_engine.UObject` wrapper, not a Python instance of the
profile class. `isinstance(self, ExampleProfile)`, cooperative Python multiple
inheritance and zero-argument `super()` are not part of the contract.

## Calling the original explicitly

When the active profile implements a function, Python fully replaces it. Call
the preserved implementation explicitly when both should run:

```python
def ReceiveBeginPlay(self):
    self.call_mixin_original("ReceiveBeginPlay")
    self.started_from_python = True
```

This is different from automatic fallback: fallback occurs only when the active
profile has no callable for that routed function. Automatic fallback copies
values between the injected and preserved UFunction's native layouts, then calls
the preserved function directly; `call_mixin_original` is the explicit
Python-facing API and therefore follows normal UEP argument/result marshalling.

## Introspection, restore and reload

`get_registered_mixins()` returns one item per class router. Each item includes
`target_class`, `registration_id`, `default_profile`, the routed function union,
and a `profiles` mapping containing each Python class, profile generation and
function subset. The legacy `python_class` key points at the default profile.

```python
ue.unregister_mixin(character_class)
ue.unregister_all_mixins()
```

Unregister restores target-owned function names and map entries, removes
transient dispatch functions, clears derived-class lookup caches, calls profile
teardown hooks and releases Python references. UEP also restores all routers
before interpreter shutdown.

Unregister before intentionally compiling the target Blueprint in the editor,
then call `register_declared_mixin` or import the direct registrations again.
UEP prunes superseded generated classes when it observes normal Blueprint
reinstancing, but editing a function signature while its live class is patched
is not treated as a supported in-place migration.

## 0.7 safety boundary

- Registration, selection and UObject access run on Unreal's game thread.
- Targets are Blueprint-generated classes.
- One router exists per target class; it may contain multiple mutually
  exclusive profiles, but profile chaining/stacking is not supported.
- Related base/derived Blueprint classes cannot have simultaneous routers.
- Only the Blueprint class that directly declares `UEPPythonMixinInterface`
  owns a router; derived classes inherit it.
- Profile methods accept `self` plus the reflected input signature.
- Static functions, RPCs, latent functions and delegate signatures are rejected.
- Non-const output/reference parameters are rejected. Return values and const
  reference inputs are supported.
- `GetPythonMixinSet` and `GetPythonMixinProfile` are reserved routing methods.
- Registry mutation is rejected from selector, initializer, teardown, mixed
  callable and original-call callback boundaries.
- Runtime routing does not mutate Blueprint assets or Unreal Engine source.
  The optional Editor authoring helper intentionally changes the project-owned
  Blueprint passed to it; it never edits Engine source or templates on its own.

See `Third_Person_Mixin_Demo_0.7.0.md` for the retained-class, multi-profile
runtime proof. For the class-creating alternative, see `Subclassing_API.md`.
