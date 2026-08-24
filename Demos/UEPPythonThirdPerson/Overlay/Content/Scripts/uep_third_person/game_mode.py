"""Python-generated GameMode selecting the Python pawn and controller."""

from unreal_engine.classes import GameModeBase

from .character import UEPThirdPersonCharacter
from .controller import UEPThirdPersonPlayerController


class UEPThirdPersonGameMode(GameModeBase):
    def __init__(self):
        self.DefaultPawnClass = UEPThirdPersonCharacter
        self.PlayerControllerClass = UEPThirdPersonPlayerController
