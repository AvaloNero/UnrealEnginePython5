"""Small visible gameplay sample using only UnrealEnginePython runtime APIs.

The official Third Person template remains unchanged. When a Game or PIE world
appears, this module creates collectible floating orbs and a cube companion.
Everything they do after spawning is driven by this Python ticker.
"""

import json
import math
from pathlib import Path
import sys
import traceback

import unreal_engine as ue
from unreal_engine import FVector
from unreal_engine.classes import StaticMesh, StaticMeshActor


GAME_WORLD_TYPES = (1, 3, 5)  # Game, PIE, GamePreview
PICKUP_COUNT = 6
PICKUP_RADIUS = 420.0
COLLECTION_DISTANCE = 135.0
SMOKE_RESULT_PREFIX = "-UEPDemoSmokeResult="


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


def _game_world_and_pawn():
    """Return an initialized gameplay world, ignoring the editor preview world."""
    for world in ue.all_worlds():
        try:
            if world.get_world_type() not in GAME_WORLD_TYPES:
                continue
            pawn = world.get_player_pawn()
            if _is_valid(pawn):
                return world, pawn
        except Exception:
            continue
    return None, None


class ThirdPersonPythonDemo:
    def __init__(self):
        self.world = None
        self.pawn = None
        self.companion = None
        self.pickups = []
        self.score = 0
        self.wave = 0
        self.elapsed = 0.0
        self.world_ticks = 0
        self.companion_initial_location = None
        self.smoke_result = _command_line_value(SMOKE_RESULT_PREFIX)
        self.smoke_finished = False
        self.cube_mesh = None
        self.sphere_mesh = None

    def _spawn_mesh_actor(self, mesh, location, scale):
        actor = self.world.actor_spawn(StaticMeshActor, location)
        component = actor.StaticMeshComponent
        component.StaticMesh = mesh

        # StaticMeshActor defaults to Static mobility. Python moves these actors
        # every frame, so make the root component movable before the first tick.
        component.Mobility = 2
        actor.set_actor_scale(scale, scale, scale)
        return actor

    def _spawn_wave(self, center):
        self.wave += 1
        self.pickups = []
        for index in range(PICKUP_COUNT):
            angle = (math.tau * index / PICKUP_COUNT) + (self.wave * 0.35)
            location = FVector(
                center.x + math.cos(angle) * PICKUP_RADIUS,
                center.y + math.sin(angle) * PICKUP_RADIUS,
                center.z + 115.0,
            )
            actor = self._spawn_mesh_actor(self.sphere_mesh, location, 0.32)
            self.pickups.append(
                {
                    "actor": actor,
                    "base": location,
                    "phase": angle,
                }
            )

    def _start_for_world(self, world, pawn):
        self.world = world
        self.pawn = pawn
        self.score = 0
        self.wave = 0
        self.elapsed = 0.0
        self.world_ticks = 0

        self.cube_mesh = ue.load_object(StaticMesh, "/Game/LevelPrototyping/Meshes/SM_ChamferCube")
        self.sphere_mesh = ue.load_object(StaticMesh, "/Engine/BasicShapes/Sphere")

        pawn_location = pawn.get_actor_location()
        companion_location = pawn_location + FVector(160.0, 0.0, 105.0)
        self.companion = self._spawn_mesh_actor(self.cube_mesh, companion_location, 0.28)
        self.companion_initial_location = companion_location
        self._spawn_wave(pawn_location)

        ue.log("UEP_DEMO_WORLD_READY")
        ue.print_string("UEP Python gameplay is running", 5.0)

    def _reset_world(self):
        self.world = None
        self.pawn = None
        self.companion = None
        self.pickups = []
        self.companion_initial_location = None

    def _update_companion(self, pawn_location):
        angle = self.elapsed * 1.8
        target = FVector(
            pawn_location.x + math.cos(angle) * 165.0,
            pawn_location.y + math.sin(angle) * 165.0,
            pawn_location.z + 105.0 + math.sin(angle * 2.0) * 18.0,
        )
        self.companion.set_actor_location(target)
        self.companion.set_actor_rotation(20.0, math.degrees(angle), 35.0)

    def _update_pickups(self, pawn_location):
        remaining = []
        for index, pickup in enumerate(self.pickups):
            actor = pickup["actor"]
            if not _is_valid(actor):
                continue

            base = pickup["base"]
            bob = math.sin(self.elapsed * 2.4 + pickup["phase"]) * 28.0
            location = FVector(base.x, base.y, base.z + bob)
            actor.set_actor_location(location)
            actor.set_actor_rotation(0.0, self.elapsed * 85.0 + index * 30.0, 0.0)

            if (location - pawn_location).length() <= COLLECTION_DISTANCE:
                actor.actor_destroy()
                self.score += 1
                ue.print_string("Python pickup collected: {0}".format(self.score), 1.5)
            else:
                remaining.append(pickup)

        self.pickups = remaining
        if not self.pickups:
            self._spawn_wave(pawn_location)
            ue.print_string("Wave complete - Python spawned a new ring", 2.5)

    def _show_hud(self):
        ue.add_on_screen_debug_message(
            58001,
            0.2,
            "UEP PYTHON DEMO  |  Pickups: {0}  |  Wave: {1}".format(self.score, self.wave),
        )
        ue.add_on_screen_debug_message(
            58002,
            0.2,
            "WASD + mouse to move. Touch the floating spheres.",
        )

    def _write_smoke_result(self, status, error=None):
        if not self.smoke_result or self.smoke_finished:
            return
        self.smoke_finished = True
        report = {
            "schema_version": 1,
            "status": status,
            "python_version": ".".join(str(value) for value in sys.version_info[:3]),
            "world_type": self.world.get_world_type() if _is_valid(self.world) else None,
            "pickup_count": len(self.pickups),
            "companion_valid": _is_valid(self.companion),
            "world_ticks": self.world_ticks,
            "error": error,
        }
        result_path = Path(self.smoke_result)
        result_path.parent.mkdir(parents=True, exist_ok=True)
        result_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        marker = "UEP_DEMO_SMOKE_PASSED" if status == "passed" else "UEP_DEMO_SMOKE_FAILED"
        (ue.log if status == "passed" else ue.log_error)(marker)

    def _request_quit(self):
        candidates = [self.world] if _is_valid(self.world) else []
        candidates.extend(ue.all_worlds())
        for world in candidates:
            try:
                world.quit_game()
                return
            except Exception:
                try:
                    world.world_exec("QUIT")
                    return
                except Exception:
                    continue

    def _finish_smoke_if_ready(self):
        if not self.smoke_result or self.smoke_finished or self.world_ticks < 12:
            return False

        moved = (
            self.companion.get_actor_location() - self.companion_initial_location
        ).length()
        if sys.version_info[:2] != (3, 11):
            raise RuntimeError("expected CPython 3.11, got {0}".format(sys.version))
        if not _is_valid(self.companion) or moved <= 1.0:
            raise RuntimeError("Python companion did not move")
        if len(self.pickups) != PICKUP_COUNT or not all(
            _is_valid(item["actor"]) for item in self.pickups
        ):
            raise RuntimeError("Python pickup wave is incomplete")

        self._write_smoke_result("passed")
        self._request_quit()
        return True

    def tick(self, delta_seconds):
        try:
            self.elapsed += delta_seconds
            world, pawn = _game_world_and_pawn()
            if not _is_valid(world) or not _is_valid(pawn):
                if self.world is not None:
                    self._reset_world()
                if self.smoke_result and self.elapsed > 45.0:
                    raise RuntimeError("game world and player pawn were not ready before timeout")
                return True

            if not _is_valid(self.world) or world.get_path_name() != self.world.get_path_name():
                self._start_for_world(world, pawn)

            self.pawn = pawn
            self.world_ticks += 1
            pawn_location = pawn.get_actor_location()
            self._update_companion(pawn_location)
            self._update_pickups(pawn_location)
            self._show_hud()

            if self._finish_smoke_if_ready():
                return False
            return True
        except Exception:
            details = traceback.format_exc()
            ue.log_error(details)
            self._write_smoke_result("failed", details)
            self._request_quit()
            return False


runtime = ThirdPersonPythonDemo()
ticker = ue.add_ticker(runtime.tick)
