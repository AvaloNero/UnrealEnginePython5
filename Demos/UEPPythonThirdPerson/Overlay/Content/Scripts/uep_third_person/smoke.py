"""Opt-in automated gameplay test kept separate from the playable runtime."""

import json
from pathlib import Path
import sys
import traceback

import unreal_engine as ue
from unreal_engine.structs import Key

from . import __version__ as DEMO_VERSION
from .assets import (
    IMC_DEFAULT,
    IMC_MOUSE_LOOK,
)
from .constants import MAP_URL, PICKUP_COUNT, SMOKE_RESULT_PREFIX
from .utils import command_line_value, gameplay_objects, is_valid, request_exit


PRESSED = 0
RELEASED = 1
MOVE_KEY = "W"
LOOK_KEY = "Mouse2D"
LOOK_AXES = ("MouseX", "MouseY")
JUMP_KEY = "SpaceBar"


class PythonThirdPersonSmoke:
    def __init__(self, runtime, result_path):
        self.runtime = runtime
        self.result_path = result_path
        self.elapsed = 0.0
        self.phase_ticks = 0
        self.phase_elapsed = 0.0
        self.phase = "waiting"
        self.finished = False
        self.pawn = None
        self.start_location = None
        self.start_rotation = None
        self.max_vertical_displacement = 0.0
        self.movement_distance = 0.0
        self.look_delta = 0.0
        self.travel_count = 0
        self.first_generation_report = None
        self.animation_states = []
        self.gameplay_report = None
        self.post_travel_report = None
        self.teardown_report = None
        self.final_mapping_contexts = None
        self.final_input_binding_count = None
        self.held_keys = set()
        self.key_events = []
        self.axis_samples = 0

    def _set_key(self, controller, key_name, event_type, amount):
        handled = bool(
            controller.input_key(Key(KeyName=key_name), event_type, amount)
        )
        if event_type == PRESSED:
            self.held_keys.add(key_name)
        else:
            self.held_keys.discard(key_name)
        self.key_events.append(
            {
                "key": key_name,
                "event": "pressed" if event_type == PRESSED else "released",
                "handled": handled,
            }
        )
        return handled

    def _send_axis(self, controller, key_name, value, delta_seconds):
        # The bool returned by APlayerController::InputKey reports legacy input
        # consumption, not whether the axis state reached Enhanced Input.
        controller.input_axis(
            Key(KeyName=key_name),
            value,
            delta_seconds,
        )
        self.axis_samples += 1

    def _release_held_keys(self, controller):
        if not is_valid(controller):
            self.held_keys.clear()
            return
        for key_name in tuple(self.held_keys):
            self._set_key(controller, key_name, RELEASED, 0.0)

    def _session_report(self):
        session = self.runtime.session
        if session is None or session.closed:
            return None
        return {
            "generation": session.generation,
            "progress": session.gameplay.progress.snapshot(),
            "remaining_pickups": len(session.gameplay.pickups),
            "companion_valid": is_valid(session.gameplay.companion),
            "companion_movement": round(
                session.gameplay.companion_movement(),
                3,
            ),
            "hud": session.hud.snapshot(),
        }

    def _write_result(self, status, world, game_mode, controller, pawn, error=None):
        if self.finished:
            return
        self.finished = True
        controller_is_python = (
            is_valid(controller)
            and controller.get_class().get_name() == "UEPThirdPersonPlayerController"
        )
        pawn_is_python = (
            is_valid(pawn)
            and pawn.get_class().get_name() == "UEPThirdPersonCharacter"
        )
        gameplay = self.gameplay_report or self._session_report() or {}
        post_travel = self.post_travel_report or {}
        uep_plugin = ue.find_plugin("UnrealEnginePython")
        report = {
            "schema_version": 5,
            "status": status,
            "engine_version": [
                ue.ENGINE_MAJOR_VERSION,
                ue.ENGINE_MINOR_VERSION,
                ue.ENGINE_PATCH_VERSION,
            ],
            "python_version": ".".join(
                str(value) for value in sys.version_info[:3]
            ),
            "uep_version": (
                uep_plugin.version_name if uep_plugin is not None else None
            ),
            "demo_version": DEMO_VERSION,
            "game_mode_class": (
                game_mode.get_class().get_name() if is_valid(game_mode) else None
            ),
            "controller_class": (
                controller.get_class().get_name() if is_valid(controller) else None
            ),
            "character_class": (
                pawn.get_class().get_name() if is_valid(pawn) else None
            ),
            "mapping_contexts": self.final_mapping_contexts or {
                "default": (
                    controller.has_enhanced_input_mapping_context(IMC_DEFAULT)
                    if controller_is_python
                    else False
                ),
                "mouse_look": (
                    controller.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK)
                    if controller_is_python
                    else False
                ),
            },
            "input_binding_count": (
                self.final_input_binding_count
                if self.final_input_binding_count is not None
                else pawn.get_enhanced_action_binding_count()
                if pawn_is_python
                else 0
            ),
            "movement_distance": round(self.movement_distance, 3),
            "look_delta": round(self.look_delta, 3),
            "jump_observed": self.max_vertical_displacement > 10.0,
            "max_vertical_displacement": round(
                self.max_vertical_displacement,
                3,
            ),
            "animation_state": (
                pawn.PythonAnimationState if pawn_is_python else None
            ),
            "animation_states_observed": self.animation_states,
            "animation_speed": (
                pawn.PythonAnimationSpeed if pawn_is_python else None
            ),
            "python_tick_count": pawn.PythonTickCount if pawn_is_python else 0,
            "input_events": self.first_generation_report,
            "input_delivery": {
                "mode": "enhanced_input_mapped_keys",
                "move_key": MOVE_KEY,
                "look_key": LOOK_KEY,
                "look_axes": LOOK_AXES,
                "jump_key": JUMP_KEY,
                "events": self.key_events,
                "axis_samples": self.axis_samples,
                "all_keys_released": not self.held_keys,
            },
            "camera_boom_valid": (
                is_valid(pawn.CameraBoom) if pawn_is_python else False
            ),
            "follow_camera_valid": (
                is_valid(pawn.FollowCamera) if pawn_is_python else False
            ),
            "map_travel_count": self.travel_count,
            "runtime_session_generation": self.runtime.session_generation,
            "gameplay": gameplay,
            "post_travel": post_travel,
            "teardown": self.teardown_report,
            "error": error,
        }
        path = Path(self.result_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        marker = (
            "UEP_PYTHON_THIRD_PERSON_SMOKE_PASSED"
            if status == "passed"
            else "UEP_PYTHON_THIRD_PERSON_SMOKE_FAILED"
        )
        (ue.log if status == "passed" else ue.log_error)(marker)

    def _assert_classes(self, game_mode, controller, pawn):
        actual = (
            game_mode.get_class().get_name(),
            controller.get_class().get_name(),
            pawn.get_class().get_name(),
        )
        expected = (
            "UEPThirdPersonGameMode",
            "UEPThirdPersonPlayerController",
            "UEPThirdPersonCharacter",
        )
        if actual != expected:
            raise RuntimeError(
                "Python gameplay classes are not active: " + ", ".join(actual)
            )

    def _begin_generation(self, controller, pawn):
        controller._ensure_mapping_contexts()
        pawn._ensure_input_bindings()
        self.pawn = pawn
        self.start_location = pawn.get_actor_location()
        self.start_rotation = pawn.get_control_rotation()
        if self.travel_count == 0:
            self.max_vertical_displacement = 0.0
        self.phase_ticks = 0
        self.phase_elapsed = 0.0
        self.phase = "settling" if self.travel_count == 0 else "post_travel"

    def _validate_runtime_contract(self, controller, pawn):
        if self.runtime.last_error:
            raise RuntimeError("runtime failed:\n" + self.runtime.last_error)
        if not controller.PythonMappingReady:
            raise RuntimeError("Python PlayerController did not install mapping contexts")
        if not controller.has_enhanced_input_mapping_context(IMC_DEFAULT):
            raise RuntimeError("default mapping context is missing")
        if not controller.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK):
            raise RuntimeError("mouse-look mapping context is missing")
        if not pawn.PythonInputBound or pawn.get_enhanced_action_binding_count() != 5:
            raise RuntimeError("Python character did not install five input bindings")
        if not is_valid(pawn.CameraBoom) or not is_valid(pawn.FollowCamera):
            raise RuntimeError("Python camera components were not created")
        if pawn.PythonTickCount <= 0:
            raise RuntimeError("Python ReceiveTick override did not execute")

        session = self.runtime.session
        if session is None or session.closed or session.pawn != pawn:
            raise RuntimeError("Python gameplay session does not own the active pawn")
        if not session.hud.attached or session.hud.root is None:
            raise RuntimeError("Python Slate HUD was not attached")
        if session.hud.update_count <= 0:
            raise RuntimeError("Python Slate HUD did not receive state updates")
        if len(session.gameplay.pickups) != PICKUP_COUNT:
            raise RuntimeError("Python collectible round is incomplete")
        if not is_valid(session.gameplay.companion):
            raise RuntimeError("Python companion was not created")

    def _capture_gameplay_report(self):
        report = self._session_report()
        if report is None:
            raise RuntimeError("gameplay session disappeared before capture")
        progress = report["progress"]
        if progress["phase"] != "victory":
            raise RuntimeError("collecting every pickup did not complete the round")
        if progress["round_score"] != PICKUP_COUNT:
            raise RuntimeError("round score does not match the pickup target")
        if report["remaining_pickups"] != 0:
            raise RuntimeError("collected pickup actors remain in the world")
        if report["companion_movement"] <= 1.0:
            raise RuntimeError("Python companion did not move")
        if not report["hud"]["attached"] or report["hud"]["update_count"] <= 0:
            raise RuntimeError("Python HUD did not present the completed round")
        self.gameplay_report = report

    def _finish_successfully(self, world, game_mode, controller, pawn):
        self.post_travel_report = self._session_report()
        if self.post_travel_report is None:
            raise RuntimeError("post-travel gameplay session is missing")
        if self.post_travel_report["generation"] < 2:
            raise RuntimeError("map travel did not create a second runtime session")
        if self.post_travel_report["progress"]["round_score"] != 0:
            raise RuntimeError("post-travel round did not reset")
        if self.post_travel_report["remaining_pickups"] != PICKUP_COUNT:
            raise RuntimeError("post-travel pickup round is incomplete")
        if not self.post_travel_report["hud"]["attached"]:
            raise RuntimeError("post-travel Python HUD is not attached")

        self.final_mapping_contexts = {
            "default": controller.has_enhanced_input_mapping_context(IMC_DEFAULT),
            "mouse_look": controller.has_enhanced_input_mapping_context(
                IMC_MOUSE_LOOK
            ),
        }
        self.final_input_binding_count = pawn.get_enhanced_action_binding_count()
        self._release_held_keys(controller)
        old_hud = self.runtime.session.hud
        self.runtime.shutdown()
        self.teardown_report = {
            "runtime_active": self.runtime.active,
            "session_released": self.runtime.session is None,
            "hud_detached": not old_hud.attached,
            "input_bindings_removed": (
                pawn.get_enhanced_action_binding_count() == 0
            ),
            "mapping_contexts_removed": (
                not controller.has_enhanced_input_mapping_context(IMC_DEFAULT)
                and not controller.has_enhanced_input_mapping_context(
                    IMC_MOUSE_LOOK
                )
            ),
        }
        if not self.teardown_report["session_released"]:
            raise RuntimeError("runtime session survived explicit teardown")
        if not self.teardown_report["hud_detached"]:
            raise RuntimeError("Python HUD survived explicit teardown")
        if not self.teardown_report["input_bindings_removed"]:
            raise RuntimeError("Python input bindings survived explicit teardown")
        if not self.teardown_report["mapping_contexts_removed"]:
            raise RuntimeError("Python mapping contexts survived explicit teardown")

        self._write_result("passed", world, game_mode, controller, pawn)
        request_exit()

    def tick(self, delta_seconds):
        world = game_mode = controller = pawn = None
        try:
            self.elapsed += delta_seconds
            world, game_mode, controller, pawn = gameplay_objects()
            if not all(is_valid(value) for value in (world, game_mode, controller, pawn)):
                if self.elapsed > 60.0:
                    raise RuntimeError("Python gameplay world was not ready before timeout")
                return True

            self._assert_classes(game_mode, controller, pawn)
            if not is_valid(self.pawn) or pawn != self.pawn:
                if self.phase == "traveling":
                    self.travel_count += 1
                self._begin_generation(controller, pawn)

            controller._ensure_mapping_contexts()
            pawn._ensure_input_bindings()
            animation_state = pawn.PythonAnimationState
            if animation_state and (
                not self.animation_states
                or self.animation_states[-1] != animation_state
            ):
                self.animation_states.append(animation_state)
            self.phase_ticks += 1
            self.phase_elapsed += delta_seconds

            if self.phase == "settling":
                if self.phase_elapsed >= 0.25:
                    self._validate_runtime_contract(controller, pawn)
                    self.start_location = pawn.get_actor_location()
                    self.phase = "moving"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "moving":
                if MOVE_KEY not in self.held_keys:
                    self._set_key(controller, MOVE_KEY, PRESSED, 1.0)
                current = pawn.get_actor_location()
                self.movement_distance = (current - self.start_location).length()
                if self.phase_elapsed >= 0.75:
                    self._set_key(controller, MOVE_KEY, RELEASED, 0.0)
                    if pawn.PythonMoveEvents <= 0 or self.movement_distance <= 25.0:
                        raise RuntimeError(
                            "W did not traverse IMC_Default into Python movement"
                        )
                    self.start_rotation = pawn.get_control_rotation()
                    self.phase = "looking"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "looking":
                # Mouse2D is a paired Enhanced Input key. Feed its real MouseX
                # and MouseY component axes so UPlayerInput builds Mouse2D and
                # the IMC_MouseLook mapping, rather than injecting IA_MouseLook.
                self._send_axis(controller, LOOK_AXES[0], 1.5, delta_seconds)
                self._send_axis(controller, LOOK_AXES[1], 0.5, delta_seconds)
                rotation = pawn.get_control_rotation()
                self.look_delta = abs(
                    rotation.yaw - self.start_rotation.yaw
                ) + abs(rotation.pitch - self.start_rotation.pitch)
                if self.phase_elapsed >= 0.3:
                    if pawn.PythonLookEvents <= 0 or self.look_delta <= 0.1:
                        raise RuntimeError(
                            "Mouse2D did not traverse IMC_MouseLook into Python look"
                        )
                    self.start_location = pawn.get_actor_location()
                    self._set_key(controller, JUMP_KEY, PRESSED, 1.0)
                    self.phase = "jumping"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "jumping":
                location = pawn.get_actor_location()
                self.max_vertical_displacement = max(
                    self.max_vertical_displacement,
                    location.z - self.start_location.z,
                )
                if self.phase_ticks == 6:
                    self._set_key(controller, JUMP_KEY, RELEASED, 0.0)
                if self.phase_elapsed >= 1.5:
                    if pawn.PythonJumpStartedEvents <= 0:
                        raise RuntimeError("jump input did not reach Python")
                    if pawn.PythonJumpCompletedEvents <= 0:
                        raise RuntimeError("jump release did not reach Python")
                    if self.max_vertical_displacement <= 10.0:
                        raise RuntimeError("Python jump did not move the character")
                    expected = {"locomotion", "jump", "fall", "land"}
                    missing = expected.difference(self.animation_states)
                    if missing:
                        raise RuntimeError(
                            "Python animation state machine missed: "
                            + ", ".join(sorted(missing))
                        )
                    self.first_generation_report = {
                        "move": pawn.PythonMoveEvents,
                        "look": pawn.PythonLookEvents,
                        "jump_started": pawn.PythonJumpStartedEvents,
                        "jump_completed": pawn.PythonJumpCompletedEvents,
                        "source": "enhanced_input_mapped_keys",
                    }
                    self.phase = "collecting"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "collecting":
                session = self.runtime.session
                if session is None or session.pawn != pawn:
                    raise RuntimeError("runtime session changed during collection")
                if session.gameplay.progress.phase == "victory":
                    self._capture_gameplay_report()
                    self.phase = "traveling"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
                    if not world.server_travel(MAP_URL):
                        raise RuntimeError("UE rejected the Python-requested map travel")
                else:
                    locations = session.gameplay.pickup_locations()
                    if not locations:
                        raise RuntimeError("no pickup remains before round completion")
                    pawn.set_actor_location(locations[0])
                    if self.phase_elapsed >= 10.0:
                        raise RuntimeError("automated pickup collection timed out")
            elif self.phase == "post_travel":
                if self.phase_ticks >= 10:
                    self._validate_runtime_contract(controller, pawn)
                    if self.travel_count != 1:
                        raise RuntimeError("same-map travel did not replace the world")
                    self._finish_successfully(world, game_mode, controller, pawn)
                    return False
            return True
        except Exception:
            details = traceback.format_exc()
            ue.log_error(details)
            self._release_held_keys(controller)
            self._write_result("failed", world, game_mode, controller, pawn, details)
            self.runtime.shutdown()
            request_exit()
            return False


def start_smoke_if_requested(runtime):
    result_path = command_line_value(SMOKE_RESULT_PREFIX)
    if not result_path:
        return None, None
    smoke = PythonThirdPersonSmoke(runtime, result_path)
    return smoke, ue.add_ticker(smoke.tick)
