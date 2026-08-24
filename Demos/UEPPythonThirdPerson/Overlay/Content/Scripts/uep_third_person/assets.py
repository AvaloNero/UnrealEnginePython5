"""The unchanged UE 5.8 template assets used by the Python runtime."""

import unreal_engine as ue
from unreal_engine.classes import (
    AnimSequence,
    BlendSpace,
    InputAction,
    InputMappingContext,
    SkeletalMesh,
    StaticMesh,
)

from .utils import is_valid


IA_JUMP = ue.load_object(InputAction, "/Game/Input/Actions/IA_Jump")
IA_MOVE = ue.load_object(InputAction, "/Game/Input/Actions/IA_Move")
IA_LOOK = ue.load_object(InputAction, "/Game/Input/Actions/IA_Look")
IA_MOUSE_LOOK = ue.load_object(InputAction, "/Game/Input/Actions/IA_MouseLook")
IMC_DEFAULT = ue.load_object(InputMappingContext, "/Game/Input/IMC_Default")
IMC_MOUSE_LOOK = ue.load_object(InputMappingContext, "/Game/Input/IMC_MouseLook")

MANNY_MESH = ue.load_object(
    SkeletalMesh,
    "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
)
LOCOMOTION_BLEND_SPACE = ue.load_object(
    BlendSpace,
    "/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run",
)
JUMP_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump",
)
FALL_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop",
)
LAND_ANIMATION = ue.load_object(
    AnimSequence,
    "/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land",
)

COMPANION_MESH = ue.load_object(
    StaticMesh,
    "/Game/LevelPrototyping/Meshes/SM_ChamferCube",
)
PICKUP_MESH = ue.load_object(StaticMesh, "/Engine/BasicShapes/Sphere")


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
    COMPANION_MESH,
    PICKUP_MESH,
)

for _asset in ROOTED_ASSETS:
    if not is_valid(_asset):
        raise RuntimeError("required Third Person asset failed to load")
    _asset.add_to_root()
