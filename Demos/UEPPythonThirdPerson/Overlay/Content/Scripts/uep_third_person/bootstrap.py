"""Register the transient Python classes and start the playable runtime."""

import unreal_engine as ue

from .character import UEPThirdPersonCharacter
from .controller import UEPThirdPersonPlayerController
from .game_mode import UEPThirdPersonGameMode
from .runtime import ThirdPersonRuntime
from .smoke import start_smoke_if_requested


GENERATED_CLASSES = (
    UEPThirdPersonCharacter,
    UEPThirdPersonPlayerController,
    UEPThirdPersonGameMode,
)

for _dynamic_class in GENERATED_CLASSES:
    # Python references are not visible to Unreal's garbage collector. Keep the
    # transient gameplay classes alive through map loading and same-map travel.
    _dynamic_class.add_to_root()

ue.log("UEP_PYTHON_THIRD_PERSON_CLASSES_READY")
ue.log(
    "UEP_PYTHON_THIRD_PERSON_GAME_MODE "
    + UEPThirdPersonGameMode.get_path_name()
)

runtime = ThirdPersonRuntime()
runtime_ticker = ue.add_ticker(runtime.tick)
smoke, smoke_ticker = start_smoke_if_requested(runtime)
