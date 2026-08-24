"""Small Unreal helpers shared by the runtime and its smoke test."""

import sys

import unreal_engine as ue

from .constants import GAME_WORLD_TYPES


def command_line_value(prefix):
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):].strip('"')
    return None


def is_valid(obj):
    try:
        return obj is not None and obj.is_valid()
    except Exception:
        return False


def gameplay_objects():
    """Return the active local game world and its Python gameplay objects."""
    for world in ue.all_worlds():
        try:
            if world.get_world_type() not in GAME_WORLD_TYPES:
                continue
            controller = world.get_player_controller()
            pawn = world.get_player_pawn()
            game_mode = world.get_auth_game_mode()
            if all(is_valid(value) for value in (controller, pawn, game_mode)):
                return world, game_mode, controller, pawn
        except Exception:
            continue
    return None, None, None, None


def request_exit():
    """Request an orderly exit without assuming a local PlayerController."""
    ue.request_exit()
