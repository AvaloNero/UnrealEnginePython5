"""World lifecycle coordinator for the Python-first Third Person demo."""

import traceback

import unreal_engine as ue

from .gameplay import ThirdPersonGameplay
from .hud import ThirdPersonHUD
from .utils import gameplay_objects, is_valid


class ThirdPersonSession:
    def __init__(self, world, game_mode, controller, pawn, generation):
        self.world = world
        self.game_mode = game_mode
        self.controller = controller
        self.pawn = pawn
        self.generation = generation
        self.gameplay = ThirdPersonGameplay(world, pawn)
        self.hud = ThirdPersonHUD()
        self.hud.attach(world)
        self.closed = False

    def tick(self, delta_seconds, pawn):
        self.pawn = pawn
        self.gameplay.tick(delta_seconds, pawn)
        self.hud.update(self.gameplay.progress, pawn)

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.hud.detach()
        self.gameplay.close()
        if is_valid(self.pawn):
            try:
                self.pawn._remove_input_bindings()
            except Exception:
                pass
        if is_valid(self.controller):
            try:
                self.controller._remove_mapping_contexts()
            except Exception:
                pass


class ThirdPersonRuntime:
    """Maintain exactly one gameplay/HUD session for the active world."""

    def __init__(self):
        self.session = None
        self.session_generation = 0
        self.last_error = None
        self.active = True

    def _matches_session(self, world, controller, pawn):
        return (
            self.session is not None
            and not self.session.closed
            and is_valid(self.session.world)
            and is_valid(self.session.pawn)
            and self.session.world == world
            and self.session.controller == controller
            and self.session.pawn == pawn
        )

    def _start_session(self, world, game_mode, controller, pawn):
        self.close_session()
        self.session_generation += 1
        self.session = ThirdPersonSession(
            world,
            game_mode,
            controller,
            pawn,
            self.session_generation,
        )
        ue.log("UEP_PYTHON_THIRD_PERSON_WORLD_READY")

    def close_session(self):
        if self.session is not None:
            self.session.close()
        self.session = None

    def shutdown(self):
        self.active = False
        self.close_session()

    def tick(self, delta_seconds):
        if not self.active:
            return False
        try:
            world, game_mode, controller, pawn = gameplay_objects()
            if not all(is_valid(value) for value in (world, game_mode, controller, pawn)):
                if self.session is not None and not is_valid(self.session.world):
                    self.close_session()
                return True

            if not self._matches_session(world, controller, pawn):
                self._start_session(world, game_mode, controller, pawn)

            self.session.tick(delta_seconds, pawn)
            return True
        except Exception:
            self.last_error = traceback.format_exc()
            ue.log_error(self.last_error)
            self.close_session()
            return False
