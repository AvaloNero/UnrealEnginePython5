"""Python-generated Character for the UE 5.8 Third Person template."""

import math

import unreal_engine as ue
from unreal_engine import FRotator, FVector
from unreal_engine.classes import (
    BoolProperty,
    CameraComponent,
    Character,
    FloatProperty,
    IntProperty,
    SpringArmComponent,
    StrProperty,
)

from .assets import (
    FALL_ANIMATION,
    IA_JUMP,
    IA_LOOK,
    IA_MOUSE_LOOK,
    IA_MOVE,
    JUMP_ANIMATION,
    LAND_ANIMATION,
    LOCOMOTION_BLEND_SPACE,
    MANNY_MESH,
)
from .constants import COMPLETED, STARTED, TRIGGERED
from .utils import is_valid


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
    PythonJumpStartHandle = IntProperty
    PythonJumpCompleteHandle = IntProperty
    PythonMoveHandle = IntProperty
    PythonMouseLookHandle = IntProperty
    PythonGamepadLookHandle = IntProperty

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

        self.CameraBoom = self.create_default_subobject(
            SpringArmComponent,
            "CameraBoom",
        )
        self.CameraBoom.setup_attachment(self.RootComponent)
        self.CameraBoom.TargetArmLength = 400.0
        self.CameraBoom.bUsePawnControlRotation = True

        self.FollowCamera = self.create_default_subobject(
            CameraComponent,
            "FollowCamera",
        )
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
        self.PythonJumpStartHandle = -1
        self.PythonJumpCompleteHandle = -1
        self.PythonMoveHandle = -1
        self.PythonMouseLookHandle = -1
        self.PythonGamepadLookHandle = -1

    def _binding_handles(self):
        return (
            self.PythonJumpStartHandle,
            self.PythonJumpCompleteHandle,
            self.PythonMoveHandle,
            self.PythonMouseLookHandle,
            self.PythonGamepadLookHandle,
        )

    def _remove_input_bindings(self):
        for handle in self._binding_handles():
            if handle >= 0:
                try:
                    self.remove_enhanced_action_binding(handle)
                except Exception:
                    pass
        self.PythonJumpStartHandle = -1
        self.PythonJumpCompleteHandle = -1
        self.PythonMoveHandle = -1
        self.PythonMouseLookHandle = -1
        self.PythonGamepadLookHandle = -1
        self.PythonInputBound = False

    def _ensure_input_bindings(self):
        if self.PythonInputBound:
            return True

        handles = []
        try:
            handles.append(
                self.bind_enhanced_action(IA_JUMP, STARTED, self._on_jump_started)
            )
            handles.append(
                self.bind_enhanced_action(
                    IA_JUMP,
                    COMPLETED,
                    self._on_jump_completed,
                )
            )
            handles.append(
                self.bind_enhanced_action(IA_MOVE, TRIGGERED, self._on_move)
            )
            handles.append(
                self.bind_enhanced_action(
                    IA_MOUSE_LOOK,
                    TRIGGERED,
                    self._on_look,
                )
            )
            handles.append(
                self.bind_enhanced_action(IA_LOOK, TRIGGERED, self._on_look)
            )
        except Exception:
            for handle in handles:
                try:
                    self.remove_enhanced_action_binding(handle)
                except Exception:
                    pass
            # Possession creates the pawn InputComponent after construction and
            # can race BeginPlay, so retry from a later character tick.
            return False

        (
            self.PythonJumpStartHandle,
            self.PythonJumpCompleteHandle,
            self.PythonMoveHandle,
            self.PythonMouseLookHandle,
            self.PythonGamepadLookHandle,
        ) = handles
        self.PythonInputBound = True
        ue.log("UEP_PYTHON_THIRD_PERSON_INPUT_BOUND")
        return True

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
            if is_valid(animation_instance):
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

    PythonTick.override = "ReceiveTick"
