"""Python-first reconstruction of Unreal Engine 5.8's Third Person template.

The level, mannequin, Enhanced Input and animation assets come from Epic's
unchanged template. Character lifecycle, camera setup, input mapping/binding,
movement, look, jump, GameMode/PlayerController bootstrap and locomotion-state
selection live in this module.
"""

import json
import math
from pathlib import Path
import sys
import traceback

import unreal_engine as ue
from unreal_engine import FRotator, FVector, FVector2D
from unreal_engine.classes import (
    AnimSequence,
    BlendSpace,
    BoolProperty,
    CameraComponent,
    Character,
    FloatProperty,
    GameModeBase,
    InputAction,
    InputMappingContext,
    IntProperty,
    PlayerController,
    SkeletalMesh,
    SpringArmComponent,
    StrProperty,
)


GAME_WORLD_TYPES = (1, 3, 5)  # Game, PIE, GamePreview
GAME_MODE_URL = "/Engine/Transient.UEPThirdPersonGameMode"
MAP_URL = "/Game/ThirdPerson/Lvl_ThirdPerson?game=" + GAME_MODE_URL
SMOKE_RESULT_PREFIX = "-UEPPythonThirdPersonSmokeResult="

TRIGGERED = 1
STARTED = 2
COMPLETED = 16

IA_JUMP = ue.load_object(InputAction, "/Game/Actions/IA_Jump")
IA_MOVE = ue.load_object(InputAction, "/Game/Actions/IA_Move")
IA_LOOK = ue.load_object(InputAction, "/Game/Actions/IA_Look")
IA_MOUSE_LOOK = ue.load_object(InputAction, "/Game/Actions/IA_MouseLook")
IMC_DEFAULT = ue.load_object(InputMappingContext, "/Game/IMC_Default")
IMC_MOUSE_LOOK = ue.load_object(InputMappingContext, "/Game/IMC_MouseLook")

MANNY_MESH = ue.load_object(SkeletalMesh, "/Game/Mannequins/Meshes/SKM_Manny_Simple")
LOCOMOTION_BLEND_SPACE = ue.load_object(
    BlendSpace,
    "/Game/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run",
)
JUMP_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Mannequins/Anims/Unarmed/Jump/MM_Jump",
)
FALL_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop",
)
LAND_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Mannequins/Anims/Unarmed/Jump/MM_Land",
)


def _command_line_value(prefix):
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):].strip('"')
    return None


def _is_valid(obj):
    try:
        return obj is not None and obj.is_valid()
    except Exception:
        return False


ROOTED_ASSETS = (
    IA_JUMP,
    IA_MOVE,
    IA_LOOK,
    IA_MOUSE_LOOK,
    IMC_DEFAULT,
    IMC_MOUSE_LOOK,
    MANNY_MESH,
    LOCOMOTION_BLEND_SPACE,
    JUMP_ANIMATION,
    FALL_ANIMATION,
    LAND_ANIMATION,
)
for _asset in ROOTED_ASSETS:
    if not _is_valid(_asset):
        raise RuntimeError("required Third Person asset failed to load")
    _asset.add_to_root()


class UEPThirdPersonCharacter(Character):
    """Template character whose behavior is implemented by Python callbacks."""

    CameraBoom = SpringArmComponent
    FollowCamera = CameraComponent
    PythonInputBound = BoolProperty
    PythonTickCount = IntProperty
    PythonMoveEvents = IntProperty
    PythonLookEvents = IntProperty
    PythonJumpStartedEvents = IntProperty
    PythonJumpCompletedEvents = IntProperty
    PythonAnimationSpeed = FloatProperty
    PythonAnimationStateElapsed = FloatProperty
    PythonAnimationState = StrProperty

    def __init__(self):
        self.CapsuleComponent.CapsuleRadius = 42.0
        self.CapsuleComponent.CapsuleHalfHeight = 96.0

        self.bUseControllerRotationPitch = False
        self.bUseControllerRotationYaw = False
        self.bUseControllerRotationRoll = False

        movement = self.CharacterMovement
        movement.bOrientRotationToMovement = True
        movement.RotationRate = FRotator(0.0, 0.0, 500.0)
        movement.JumpZVelocity = 500.0
        movement.AirControl = 0.35
        movement.MaxWalkSpeed = 500.0
        movement.MinAnalogWalkSpeed = 20.0
        movement.BrakingDecelerationWalking = 2000.0
        movement.BrakingDecelerationFalling = 1500.0

        self.CameraBoom = self.create_default_subobject(SpringArmComponent, "CameraBoom")
        self.CameraBoom.setup_attachment(self.RootComponent)
        self.CameraBoom.TargetArmLength = 400.0
        self.CameraBoom.bUsePawnControlRotation = True

        self.FollowCamera = self.create_default_subobject(CameraComponent, "FollowCamera")
        self.FollowCamera.setup_attachment(self.CameraBoom, "SpringEndpoint")
        self.FollowCamera.bUsePawnControlRotation = False

        self.Mesh.set_skeletal_mesh(MANNY_MESH)
        self.Mesh.set_relative_location(FVector(0.0, 0.0, -90.0))
        self.Mesh.set_relative_rotation(FRotator(0.0, 0.0, -90.0))

        self.PythonInputBound = False
        self.PythonTickCount = 0
        self.PythonMoveEvents = 0
        self.PythonLookEvents = 0
        self.PythonJumpStartedEvents = 0
        self.PythonJumpCompletedEvents = 0
        self.PythonAnimationSpeed = 0.0
        self.PythonAnimationStateElapsed = 0.0
        self.PythonAnimationState = "initializing"

    def _ensure_input_bindings(self):
        if self.PythonInputBound:
            return True
        try:
            self.bind_enhanced_action(IA_JUMP, STARTED, self._on_jump_started)
            self.bind_enhanced_action(IA_JUMP, COMPLETED, self._on_jump_completed)
            self.bind_enhanced_action(IA_MOVE, TRIGGERED, self._on_move)
            self.bind_enhanced_action(IA_MOUSE_LOOK, TRIGGERED, self._on_look)
            self.bind_enhanced_action(IA_LOOK, TRIGGERED, self._on_look)
            self.PythonInputBound = True
            ue.log("UEP_PYTHON_THIRD_PERSON_INPUT_BOUND")
            return True
        except Exception:
            # Possession creates the pawn InputComponent after construction and
            # can race BeginPlay, so retry on the following character tick.
            return False

    def _on_move(self, value):
        self.PythonMoveEvents += 1
        rotation = self.get_control_rotation()
        yaw_rotation = FRotator(0.0, 0.0, rotation.yaw)
        self.add_movement_input(ue.get_forward_vector(yaw_rotation), value.y)
        self.add_movement_input(ue.get_right_vector(yaw_rotation), value.x)

    def _on_look(self, value):
        self.PythonLookEvents += 1
        self.add_controller_yaw_input(value.x)
        self.add_controller_pitch_input(value.y)

    def _on_jump_started(self, _value):
        self.PythonJumpStartedEvents += 1
        self.jump()

    def _on_jump_completed(self, _value):
        self.PythonJumpCompletedEvents += 1
        self.stop_jumping()

    def _play_animation_state(self, state, asset, looping):
        if self.PythonAnimationState == state:
            return
        self.Mesh.call_function("PlayAnimation", asset, looping)
        self.PythonAnimationState = state
        self.PythonAnimationStateElapsed = 0.0
        ue.log("UEP_PYTHON_THIRD_PERSON_ANIMATION " + state)

    def _update_animation(self, delta_seconds):
        self.PythonAnimationStateElapsed += delta_seconds
        velocity = self.get_actor_velocity()
        speed = math.sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y))
        self.PythonAnimationSpeed = speed

        falling = self.is_falling()
        if falling and velocity.z > 40.0:
            self._play_animation_state("jump", JUMP_ANIMATION, False)
        elif falling:
            self._play_animation_state("fall", FALL_ANIMATION, True)
        elif self.PythonAnimationState in ("jump", "fall"):
            self._play_animation_state("land", LAND_ANIMATION, False)
        elif self.PythonAnimationState == "initializing":
            self._play_animation_state("locomotion", LOCOMOTION_BLEND_SPACE, True)
        elif (
            self.PythonAnimationState == "land"
            and self.PythonAnimationStateElapsed >= 0.18
        ):
            self._play_animation_state("locomotion", LOCOMOTION_BLEND_SPACE, True)

        if self.PythonAnimationState == "locomotion":
            animation_instance = self.Mesh.get_anim_instance()
            if _is_valid(animation_instance):
                animation_instance.call_function(
                    "SetBlendSpacePosition",
                    FVector(speed, 0.0, 0.0),
                )

    def PythonBeginPlay(self):
        self._ensure_input_bindings()
        self._update_animation(0.0)
        ue.log("UEP_PYTHON_THIRD_PERSON_CHARACTER_READY")

    PythonBeginPlay.override = "ReceiveBeginPlay"

    def PythonTick(self, DeltaSeconds: float):
        self.PythonTickCount += 1
        self._ensure_input_bindings()
        self._update_animation(DeltaSeconds)
        ue.add_on_screen_debug_message(
            58101,
            0.2,
            "UEP PYTHON-FIRST | speed {0:.0f} | {1}".format(
                self.PythonAnimationSpeed,
                self.PythonAnimationState,
            ),
        )

    PythonTick.override = "ReceiveTick"


class UEPThirdPersonPlayerController(PlayerController):
    PythonMappingReady = BoolProperty

    def __init__(self):
        self.PythonMappingReady = False

    def _ensure_mapping_contexts(self):
        if self.PythonMappingReady:
            return True
        try:
            self.add_enhanced_input_mapping_context(IMC_DEFAULT, 0)
            self.add_enhanced_input_mapping_context(IMC_MOUSE_LOOK, 0)
            self.PythonMappingReady = True
            ue.log("UEP_PYTHON_THIRD_PERSON_MAPPING_READY")
            return True
        except Exception:
            return False

    def PythonBeginPlay(self):
        self._ensure_mapping_contexts()
        ue.log("UEP_PYTHON_THIRD_PERSON_CONTROLLER_READY")

    PythonBeginPlay.override = "ReceiveBeginPlay"


class UEPThirdPersonGameMode(GameModeBase):
    """Python bootstrap replacing BP_ThirdPersonGameMode behavior."""

    def __init__(self):
        self.DefaultPawnClass = UEPThirdPersonCharacter
        self.PlayerControllerClass = UEPThirdPersonPlayerController


for _dynamic_class in (
    UEPThirdPersonCharacter,
    UEPThirdPersonPlayerController,
    UEPThirdPersonGameMode,
):
    # Python references are not visible to Unreal's garbage collector. Keep the
    # transient gameplay classes alive through map loading and map travel.
    _dynamic_class.add_to_root()


ue.log("UEP_PYTHON_THIRD_PERSON_CLASSES_READY")
ue.log("UEP_PYTHON_THIRD_PERSON_GAME_MODE " + UEPThirdPersonGameMode.get_path_name())


def _gameplay_objects():
    for world in ue.all_worlds():
        try:
            if world.get_world_type() not in GAME_WORLD_TYPES:
                continue
            controller = world.get_player_controller()
            pawn = world.get_player_pawn()
            game_mode = world.get_auth_game_mode()
            if _is_valid(controller) and _is_valid(pawn) and _is_valid(game_mode):
                return world, game_mode, controller, pawn
        except Exception:
            continue
    return None, None, None, None


class PythonThirdPersonSmoke:
    def __init__(self):
        self.result_path = _command_line_value(SMOKE_RESULT_PREFIX)
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

    def _request_quit(self, world):
        try:
            world.quit_game()
        except Exception:
            world.world_exec("QUIT")

    def _write_result(self, status, world, game_mode, controller, pawn, error=None):
        if not self.result_path or self.finished:
            return
        self.finished = True
        controller_is_python = (
            _is_valid(controller)
            and controller.get_class().get_name() == "UEPThirdPersonPlayerController"
        )
        pawn_is_python = (
            _is_valid(pawn)
            and pawn.get_class().get_name() == "UEPThirdPersonCharacter"
        )
        report = {
            "schema_version": 1,
            "status": status,
            "python_version": ".".join(str(value) for value in sys.version_info[:3]),
            "game_mode_class": game_mode.get_class().get_name() if _is_valid(game_mode) else None,
            "controller_class": controller.get_class().get_name() if _is_valid(controller) else None,
            "character_class": pawn.get_class().get_name() if _is_valid(pawn) else None,
            "mapping_contexts": {
                "default": controller.has_enhanced_input_mapping_context(IMC_DEFAULT) if controller_is_python else False,
                "mouse_look": controller.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK) if controller_is_python else False,
            },
            "input_binding_count": pawn.get_enhanced_action_binding_count() if pawn_is_python else 0,
            "movement_distance": round(self.movement_distance, 3),
            "look_delta": round(self.look_delta, 3),
            "jump_observed": self.max_vertical_displacement > 10.0,
            "max_vertical_displacement": round(self.max_vertical_displacement, 3),
            "animation_state": pawn.PythonAnimationState if pawn_is_python else None,
            "animation_states_observed": self.animation_states,
            "animation_speed": pawn.PythonAnimationSpeed if pawn_is_python else None,
            "python_tick_count": pawn.PythonTickCount if pawn_is_python else 0,
            "input_events": self.first_generation_report,
            "camera_boom_valid": _is_valid(pawn.CameraBoom) if pawn_is_python else False,
            "follow_camera_valid": _is_valid(pawn.FollowCamera) if pawn_is_python else False,
            "map_travel_count": self.travel_count,
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
        expected = {
            game_mode.get_class().get_name(): "UEPThirdPersonGameMode",
            controller.get_class().get_name(): "UEPThirdPersonPlayerController",
            pawn.get_class().get_name(): "UEPThirdPersonCharacter",
        }
        wrong = [actual for actual, wanted in expected.items() if actual != wanted]
        if wrong:
            raise RuntimeError("Python gameplay classes are not active: " + ", ".join(wrong))

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
        ue.log("UEP_PYTHON_THIRD_PERSON_WORLD_READY")

    def _validate_runtime_contract(self, controller, pawn):
        if not controller.PythonMappingReady:
            raise RuntimeError("Python PlayerController did not install mapping contexts")
        if not controller.has_enhanced_input_mapping_context(IMC_DEFAULT):
            raise RuntimeError("default mapping context is missing")
        if not controller.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK):
            raise RuntimeError("mouse-look mapping context is missing")
        if not pawn.PythonInputBound or pawn.get_enhanced_action_binding_count() != 5:
            raise RuntimeError("Python character did not install five Enhanced Input bindings")
        if not _is_valid(pawn.CameraBoom) or not _is_valid(pawn.FollowCamera):
            raise RuntimeError("Python camera components were not created")
        if pawn.PythonTickCount <= 0:
            raise RuntimeError("Python ReceiveTick override did not execute")

    def tick(self, delta_seconds):
        world = game_mode = controller = pawn = None
        try:
            self.elapsed += delta_seconds
            world, game_mode, controller, pawn = _gameplay_objects()
            if not all(_is_valid(value) for value in (world, game_mode, controller, pawn)):
                if self.result_path and self.elapsed > 60.0:
                    raise RuntimeError("Python gameplay world was not ready before timeout")
                return True

            self._assert_classes(game_mode, controller, pawn)
            if not _is_valid(self.pawn) or pawn != self.pawn:
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

            if not self.result_path:
                return True

            if self.phase == "settling":
                if self.phase_elapsed >= 0.25:
                    self._validate_runtime_contract(controller, pawn)
                    self.start_location = pawn.get_actor_location()
                    self.phase = "moving"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "moving":
                controller.inject_enhanced_input_for_action(IA_MOVE, FVector2D(0.0, 1.0))
                current = pawn.get_actor_location()
                self.movement_distance = (current - self.start_location).length()
                if self.phase_elapsed >= 0.75:
                    if pawn.PythonMoveEvents <= 0 or self.movement_distance <= 25.0:
                        raise RuntimeError("injected move input did not move the Python character")
                    self.start_rotation = pawn.get_control_rotation()
                    self.phase = "looking"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
            elif self.phase == "looking":
                controller.inject_enhanced_input_for_action(IA_MOUSE_LOOK, FVector2D(1.5, 0.5))
                rotation = pawn.get_control_rotation()
                self.look_delta = abs(rotation.yaw - self.start_rotation.yaw) + abs(
                    rotation.pitch - self.start_rotation.pitch
                )
                if self.phase_elapsed >= 0.2:
                    if pawn.PythonLookEvents <= 0 or self.look_delta <= 0.1:
                        raise RuntimeError("injected look input did not rotate the controller")
                    self.start_location = pawn.get_actor_location()
                    controller.inject_enhanced_input_for_action(IA_JUMP, True)
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
                    controller.inject_enhanced_input_for_action(IA_JUMP, False)
                if self.phase_elapsed >= 1.5:
                    if pawn.PythonJumpStartedEvents <= 0:
                        raise RuntimeError("injected jump input did not reach the Python callback")
                    if pawn.PythonJumpCompletedEvents <= 0:
                        raise RuntimeError("released jump input did not reach the Python callback")
                    if self.max_vertical_displacement <= 10.0:
                        raise RuntimeError("Python jump callback did not move the character vertically")
                    expected_animation_states = {"locomotion", "jump", "fall", "land"}
                    missing_animation_states = expected_animation_states.difference(
                        self.animation_states
                    )
                    if missing_animation_states:
                        raise RuntimeError(
                            "Python animation state machine missed: "
                            + ", ".join(sorted(missing_animation_states))
                        )
                    self.first_generation_report = {
                        "move": pawn.PythonMoveEvents,
                        "look": pawn.PythonLookEvents,
                        "jump_started": pawn.PythonJumpStartedEvents,
                        "jump_completed": pawn.PythonJumpCompletedEvents,
                    }
                    self.phase = "traveling"
                    self.phase_ticks = 0
                    self.phase_elapsed = 0.0
                    if not world.server_travel(MAP_URL):
                        raise RuntimeError("UE rejected the Python-requested map travel")
            elif self.phase == "post_travel":
                if self.phase_ticks >= 10:
                    self._validate_runtime_contract(controller, pawn)
                    if self.travel_count != 1:
                        raise RuntimeError("same-map travel did not create a new Python gameplay world")
                    self._write_result("passed", world, game_mode, controller, pawn)
                    self._request_quit(world)
                    return False
            return True
        except Exception:
            details = traceback.format_exc()
            ue.log_error(details)
            self._write_result("failed", world, game_mode, controller, pawn, details)
            if _is_valid(world):
                self._request_quit(world)
            return False


runtime = PythonThirdPersonSmoke()
ticker = ue.add_ticker(runtime.tick)
