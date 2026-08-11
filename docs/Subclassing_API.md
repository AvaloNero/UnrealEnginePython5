# Native subclassing API

UnrealEnginePython 0.3.0 supports runtime Python subclass generation for Unreal
Engine 5.8. A Python class that derives from an imported Unreal class produces a
transient `UClass`; declared fields become UE5 `FProperty` objects and annotated
methods become reflected `UFunction` objects.

This API runs inside Unreal's Python interpreter. It cannot be imported from a
standalone Python process. The validated configuration is UE 5.8 on Win64 with
the engine-bundled CPython 3.11 runtime, and it does not require an Engine source
change.

## Defining a class

```python
import unreal_engine as ue
from unreal_engine.classes import Character, FloatProperty, StrProperty


class FloatingCharacter(Character):
    RiseSpeed = FloatProperty
    State = StrProperty

    def __init__(self):
        self.RiseSpeed = 100.0
        self.State = "ready"

    def PythonTick(self, DeltaSeconds: float):
        location = self.get_actor_location()
        location.z += self.RiseSpeed * DeltaSeconds
        self.set_actor_location(location)

    PythonTick.override = "ReceiveTick"


# Transient generated classes must be rooted for as long as Unreal may load,
# spawn or reference them by class object or path.
FloatingCharacter.add_to_root()
```

The Python `__init__` function is installed before Unreal creates the class
default object (CDO), so it can set reflected defaults and create default
subobjects. It also runs for subsequently constructed instances.

## Reflected properties

Declare a property by assigning an imported Unreal property type on the Python
class:

```python
from unreal_engine.classes import Actor, BoolProperty, FloatProperty, IntProperty, StrProperty


class PropertyExample(Actor):
    Enabled = BoolProperty
    Count = IntProperty
    Weight = FloatProperty
    Samples = [FloatProperty]
    Tags = {StrProperty}
    Target = Actor
    Scores = {StrProperty: IntProperty}
```

The UE 5.8 path supports scalar properties, arrays, maps, object references,
structs and enums. A one-element Python set declares `TSet<ElementProperty>`;
runtime reads return a Python `set`, while writes accept `set` or `frozenset`.
Set elements and map keys must map to a UE property type with a value hash.

## Reflected functions and overrides

Public Python methods are reflected when their arguments and return value use
supported annotations. Arguments without a supported annotation are omitted
from the reflected signature.

```python
class FunctionExample(Actor):
    def Add(self, Left: int, Right: int) -> int:
        return Left + Right
```

Use an explicit `override` attribute when replacing a parent reflected event.
This is the recommended UE5.8 form because it makes the Unreal event name
unambiguous and lets the Python implementation use a different name:

```python
class EventExample(Actor):
    def PythonBeginPlay(self):
        ue.log("begin play from Python")

    PythonBeginPlay.override = "ReceiveBeginPlay"
```

The parent function must be an overridable reflected function. In particular,
the 0.3.0 validation suite exercises a real `BlueprintNativeEvent` override,
not only a newly declared Python function.

The legacy metadata flags remain available for new reflected methods:

```python
FunctionExample.Add.pure = True
# Other supported flags include static, event, multicast, server, client and
# reliable where Unreal's corresponding function rules allow them.
```

## Default subobjects

Default components may be created in `__init__` and attached before the actor is
spawned:

```python
from unreal_engine.classes import CameraComponent, Character, SpringArmComponent


class CameraCharacter(Character):
    CameraBoom = SpringArmComponent
    FollowCamera = CameraComponent

    def __init__(self):
        self.CameraBoom = self.create_default_subobject(
            SpringArmComponent,
            "CameraBoom",
        )
        self.CameraBoom.setup_attachment(self.RootComponent)

        self.FollowCamera = self.create_default_subobject(
            CameraComponent,
            "FollowCamera",
        )
        self.FollowCamera.setup_attachment(self.CameraBoom, "SpringEndpoint")
```

`setup_attachment(parent, socket_name=None)` mirrors construction-time scene
component attachment. Runtime attachment after registration should use the
appropriate Unreal attachment function instead.

## Lifetime and redefinition rules

- Generated classes are transient. Call `add_to_root()` when a class must
  survive Unreal garbage collection or be resolved later from a URL/class path.
- Keep referenced transient assets rooted for the same reason when the class
  stores them as defaults.
- Defining a second generated class with the same Unreal name in one process is
  rejected. Class hot redefinition/reload is not supported in 0.3.0; restart
  the process to recreate it.
- UObject access belongs on Unreal's game thread. Python worker threads must not
  mutate UObjects.
- Python exceptions raised by reflected callbacks are reported to the Unreal
  log. Gameplay code should keep callbacks small and handle recoverable errors.

See `Lifecycle_Threading_API.md` for the complete interpreter, callback,
exception and worker-thread contract.

See the complete Python-first character, controller and GameMode implementation
in `Demos/UEPPythonThirdPerson/Overlay/Content/Scripts/uep_python_third_person.py`.
