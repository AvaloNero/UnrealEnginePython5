# Blueprint mixin API

UEP 0.7 can attach Python behavior to a loaded Blueprint-generated class
without creating a new Unreal class. Existing placed objects and future spawned
objects keep their original `BP_*_C` type; calls to selected reflected functions
are redirected to Python until the mixin is unregistered.

This is separate from the native subclassing API. Subclassing creates a
transient Python-derived `UClass` and requires spawn/class-selection changes. A
mixin changes selected function entries on the existing Blueprint class and is
therefore suited to an asset-first workflow where the Blueprint already exists.

## Register a mixin

Register after loading the target class. Startup registration before BeginPlay
is the usual path, but registration also affects already spawned instances as
soon as the class function map is updated:

```python
import unreal_engine as ue
from unreal_engine import FRotator


character_class = ue.load_class(
    "/Game/ThirdPerson/Blueprints/"
    "BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C"
)


@ue.mixin(character_class)
class ThirdPersonCharacterMixin:
    def __init__(self):
        self.python_move_events = 0

    def Move(self, right, forward):
        self.python_move_events += 1
        rotation = self.get_control_rotation()
        yaw = FRotator(0.0, 0.0, rotation.yaw)
        self.add_movement_input(ue.get_forward_vector(yaw), forward)
        self.add_movement_input(ue.get_right_vector(yaw), right)

    def PythonBeginPlay(self):
        self.call_mixin_original("ReceiveBeginPlay")
        ue.log("Blueprint instance now has Python behavior")

    PythonBeginPlay.override = "ReceiveBeginPlay"
```

An exact Python method name such as `Move` replaces the same target UFunction.
The optional string `.override` alias remains available when the Python method
should have a different name.

`register_mixin(target_class, python_class)` is the non-decorator form. A second
registration for the same target first restores the previous generation, then
installs the new one. `get_registered_mixins()` returns the target class,
Python class, registration generation and mixed function names.

Registration validates that each Python method can accept `self` plus every
reflected input parameter. An incompatible replacement raises `TypeError`
before the active generation is removed.

## Object state and helpers

The Python wrapper for each UObject is passed as `self`. The locally declared
`__init__` runs on the first mixed call or mixin-helper lookup for that object
and may store ordinary Python attributes. Lower-case helper functions declared
on the mixin class are resolved and bound on demand:

```python
class ExampleMixin:
    def __init__(self):
        self.samples = []

    def _remember(self, value):
        self.samples.append(value)

    def SomeBlueprintFunction(self, value):
        self._remember(value)
```

`self` is still an `unreal_engine.UObject` wrapper, not a real Python instance
of `ExampleMixin`. Python `isinstance(self, ExampleMixin)`, cooperative Python
multiple inheritance and zero-argument `super()` are therefore not part of the
contract.

## Calling the original function

Every mixed function retains its prior UFunction pointer. Call it explicitly
with the original Unreal function name:

```python
def ReceiveBeginPlay(self):
    self.call_mixin_original("ReceiveBeginPlay")
    self.started_from_python = True
```

Additional positional/keyword arguments are marshalled with the normal UEP
UFunction caller. The original is not called implicitly: omitting this call
means Python fully replaces that function.

## Restore and reload

```python
ue.unregister_mixin(character_class)  # True when a registration existed
ue.unregister_all_mixins()
```

Unregister removes injected functions, restores target-owned function names and
map entries, clears derived-class lookup caches, calls an optional local
`__uep_mixin_teardown__(self)` for initialized objects and releases the Python
class. UEP also performs this restoration before interpreter shutdown.

The teardown hook owns cleanup of any ordinary Python attributes that should
not remain on a still-live UObject wrapper. UEP removes its private generation
marker and helper resolution, but does not guess which user state to delete.

Unregister before recompiling/reinstancing the target Blueprint in the editor.
Blueprint recompile while a live generation is patched is intentionally not a
0.7 hot-reload path.

## 0.7 safety boundary

- Registration and UObject access must run on Unreal's game thread.
- The target must be a Blueprint-generated class.
- One mixin is active per target class; a new registration replaces it.
- A base/derived Blueprint class chain cannot hold simultaneous registrations
  in 0.7; mix the intended owning class and let normal Unreal lookup apply.
- Methods must accept `self` plus the reflected input signature; ordinary
  instance methods are the supported Python form.
- Static functions, RPCs, latent functions and delegate signatures are rejected.
- Non-const output/reference parameters are rejected. Return values and const
  reference inputs are supported.
- Multiple mixins on one class, ordered chaining and implicit original calls
  are deferred.
- The mixin does not rewrite Blueprint assets or Unreal Engine source.

The complete retained-class example is documented in
`Third_Person_Mixin_Demo_0.7.0.md`. For the class-creating alternative, see
`Subclassing_API.md`.
