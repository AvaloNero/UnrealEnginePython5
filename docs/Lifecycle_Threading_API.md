# Lifecycle, reload, exceptions and threading

This document records the current UnrealEnginePython runtime contract for UE
5.8 and CPython 3.11. The released 0.3.0 baseline remains identified below;
newer sections are explicitly marked as 0.4.0 development work.

## Interpreter ownership

UEP supports two tested process modes:

- shared: Epic's `PythonScriptPlugin` initializes CPython and UEP attaches to
  that interpreter;
- standalone: `-DisablePython` disables Epic's plugin and UEP initializes the
  engine-bundled CPython 3.11 runtime itself.

Do not finalize or replace the interpreter from gameplay code. UEP releases
only the interpreter it owns.

## Game-thread rule

Reflection, UObject construction/destruction, properties, functions, delegates,
world access and editor APIs belong on Unreal's game thread. Python worker
threads may perform Python-only computation or blocking I/O, but must hand
results back to code running on the game thread before touching a UObject.

```python
import unreal_engine as ue

if not ue.is_in_game_thread():
    raise RuntimeError("UObject access must be dispatched to the game thread")
```

`is_in_game_thread()` is diagnostic and is safe to call from a Python worker.
It does not make the surrounding operation thread-safe. A practical pattern is
for workers to put plain Python data into a queue and for a game-thread ticker
or gameplay callback to consume that queue.

## Headless process exit (0.4.0 development)

`UObject.quit_game()` needs a world with a local `PlayerController`, so it is
not a reliable shutdown primitive for dedicated servers or commandlets. Use the
module-level exit request instead:

```python
import unreal_engine as ue

ue.request_exit()
```

The default is a graceful engine-loop exit and allows UEP to close the object
subsystem and its owned interpreter. `ue.request_exit(True)` requests a forced
platform exit and should be reserved for an already-unrecoverable process.

## Delegate and input lifetime

Binding a Python callable keeps it alive. In 0.3.0, explicit
`unbind_event(...)` and `remove_enhanced_action_binding(handle)` immediately
clear the callable reference and unroot the bridge delegate. Owner destruction
also clears tracked callables during the housekeeper pass.

Keep the returned Enhanced Input handle and remove it when the owning gameplay
object tears down. Removing the same handle twice returns `False`.

## Dynamic class reload contract

Generated classes are transient but Unreal class names remain unique for the
life of the process. Defining a second dynamic class with the same name raises
`RuntimeError` and leaves the original class operational. In-process dynamic
class replacement is not supported; restart the Editor/game process to load a
new definition.

Python module reload for proxy-style `PyActor`/`PythonComponent` code does not
change this dynamic `UClass` rule.

## Exception boundary

An exception escaping a reflected Python `UFunction` is written to the Unreal
log and the native invocation returns without propagating a partially converted
value. Later calls remain usable. Because UE commandlets count the logged
traceback as an error, validation runs this behavior in a dedicated process and
allows only the expected traceback lines.

Treat logged callback exceptions as real gameplay failures: catch recoverable
conditions inside Python and keep reflected callbacks small.

## Non-trivial values and stress

Dynamic call frames are initialized and destroyed through UE reflection.
Returns such as `FString` are copied with the property's copy operation rather
than raw bytes. The required suite repeatedly calls integer and variable-length
string functions in both interpreter ownership modes to catch lifetime and
destructor regressions.
