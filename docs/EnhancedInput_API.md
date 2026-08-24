# Enhanced Input API

UnrealEnginePython 0.3.0 exposes the UE5 Enhanced Input operations needed by
runtime Python gameplay. The methods are available on compatible wrapped
UObjects and use the local player's `UEnhancedInputLocalPlayerSubsystem` or an
`UEnhancedInputComponent` owned by the player/possessed pawn.

## Trigger events

`bind_enhanced_action` accepts the integer value of one Unreal
`ETriggerEvent` member:

| Name | Value |
| --- | ---: |
| `Triggered` | 1 |
| `Started` | 2 |
| `Ongoing` | 4 |
| `Canceled` | 8 |
| `Completed` | 16 |

## Binding actions

```python
TRIGGERED = 1
STARTED = 2
COMPLETED = 16

move_handle = pawn.bind_enhanced_action(IA_MOVE, TRIGGERED, on_move)
jump_start_handle = pawn.bind_enhanced_action(IA_JUMP, STARTED, on_jump)
jump_end_handle = pawn.bind_enhanced_action(IA_JUMP, COMPLETED, on_stop_jump)

count = pawn.get_enhanced_action_binding_count()
removed = pawn.remove_enhanced_action_binding(move_handle)
```

The binding owns a strong Python reference to its callback. Successful removal
clears that reference immediately; keep the numeric handle and remove it during
the owning object's teardown. A second removal of the same handle returns
`False`.

`bind_enhanced_action(action, trigger_event, callback)` returns the binding's
unsigned handle. The callback receives one Python value derived from the input
action's configured value type:

| Input action value type | Python callback value |
| --- | --- |
| Boolean | `bool` |
| Axis 1D | `float` |
| Axis 2D | `unreal_engine.FVector2D` |
| Axis 3D | `unreal_engine.FVector` |

Binding may fail during pawn construction or early BeginPlay because possession
has not created its input component yet. Bind from setup-input time when that
hook is available, or retry from a later game-thread callback as the demo does.

## Mapping contexts

```python
controller.add_enhanced_input_mapping_context(IMC_DEFAULT, 0)
ready = controller.has_enhanced_input_mapping_context(IMC_DEFAULT)
controller.remove_enhanced_input_mapping_context(IMC_DEFAULT)
```

- `add_enhanced_input_mapping_context(context, priority=0)` adds a context.
- `has_enhanced_input_mapping_context(context)` returns `True` when active.
- `remove_enhanced_input_mapping_context(context)` removes it.

These methods require a local player. They are not a server-side replication
API, and adding a mapping context on one client does not configure another.

## Deterministic injection

Tests and local tooling can feed the same action path used by gameplay:

```python
controller.inject_enhanced_input_for_action(IA_JUMP, True)
controller.inject_enhanced_input_for_action(IA_MOVE, FVector2D(0.0, 1.0))
```

`inject_enhanced_input_for_action(action, value)` validates the Python value
against the action's configured value type. It requires a local Enhanced Input
subsystem and must be called on Unreal's game thread.

See
`Demos/UEPPythonThirdPerson/Overlay/Content/Scripts/uep_third_person/character.py`
and `controller.py` for mapping ownership, failure-safe binding, movement, look
and jump together in a packaged-capable example. The old
`uep_python_third_person.py` path remains a compatibility import.
