"""Collectible round gameplay layered onto the Python-generated character."""

import math

import unreal_engine as ue
from unreal_engine import FVector
from unreal_engine.classes import StaticMeshActor

from .assets import COMPANION_MESH, PICKUP_MESH
from .constants import (
    COLLECTION_DISTANCE,
    PICKUP_COUNT,
    PICKUP_HEIGHT,
    PICKUP_RADIUS,
    ROUND_COMPLETE_SECONDS,
)
from .state import DemoProgress
from .utils import is_valid


class ThirdPersonGameplay:
    """Own the visible actors and one deterministic collectible game loop."""

    def __init__(self, world, pawn):
        self.world = world
        self.pawn = pawn
        self.progress = DemoProgress(PICKUP_COUNT, ROUND_COMPLETE_SECONDS)
        self.pickups = []
        self.companion = None
        self.companion_initial_location = None
        self.elapsed = 0.0
        self.spawn_generation = 0

        pawn_location = pawn.get_actor_location()
        companion_location = pawn_location + FVector(160.0, 0.0, 105.0)
        self.companion = self._spawn_mesh_actor(
            COMPANION_MESH,
            companion_location,
            0.28,
        )
        self.companion_initial_location = companion_location
        self._spawn_round(pawn_location)
        ue.log("UEP_PYTHON_THIRD_PERSON_GAMEPLAY_READY")

    def _spawn_mesh_actor(self, mesh, location, scale):
        actor = self.world.actor_spawn(StaticMeshActor, location)
        component = actor.StaticMeshComponent
        component.StaticMesh = mesh
        component.Mobility = 2
        actor.set_actor_scale(scale, scale, scale)
        return actor

    def _spawn_round(self, center):
        self.spawn_generation += 1
        self.pickups = []
        for index in range(PICKUP_COUNT):
            angle = (math.tau * index / PICKUP_COUNT) + (
                self.spawn_generation * 0.35
            )
            location = FVector(
                center.x + math.cos(angle) * PICKUP_RADIUS,
                center.y + math.sin(angle) * PICKUP_RADIUS,
                center.z + PICKUP_HEIGHT,
            )
            self.pickups.append(
                {
                    "actor": self._spawn_mesh_actor(PICKUP_MESH, location, 0.32),
                    "base": location,
                    "phase": angle,
                }
            )
        ue.log(
            "UEP_PYTHON_THIRD_PERSON_ROUND_READY {0}".format(
                self.progress.round,
            )
        )

    def _update_companion(self, pawn_location):
        if not is_valid(self.companion):
            return
        angle = self.elapsed * 1.8
        target = FVector(
            pawn_location.x + math.cos(angle) * 165.0,
            pawn_location.y + math.sin(angle) * 165.0,
            pawn_location.z + 105.0 + math.sin(angle * 2.0) * 18.0,
        )
        self.companion.set_actor_location(target)
        self.companion.set_actor_rotation(20.0, math.degrees(angle), 35.0)

    def _update_pickups(self, pawn_location):
        if self.progress.phase != "playing":
            return

        remaining = []
        for index, pickup in enumerate(self.pickups):
            actor = pickup["actor"]
            if not is_valid(actor):
                continue

            base = pickup["base"]
            bob = math.sin(self.elapsed * 2.4 + pickup["phase"]) * 28.0
            location = FVector(base.x, base.y, base.z + bob)
            actor.set_actor_location(location)
            actor.set_actor_rotation(
                0.0,
                self.elapsed * 85.0 + index * 30.0,
                0.0,
            )

            if (location - pawn_location).length() <= COLLECTION_DISTANCE:
                actor.actor_destroy()
                completed = self.progress.collect()
                ue.log(
                    "UEP_PYTHON_THIRD_PERSON_PICKUP {0}/{1}".format(
                        self.progress.round_score,
                        self.progress.target,
                    )
                )
                if completed:
                    ue.log("UEP_PYTHON_THIRD_PERSON_ROUND_COMPLETE")
            else:
                remaining.append(pickup)
        self.pickups = remaining

    def tick(self, delta_seconds, pawn):
        self.pawn = pawn
        self.elapsed += delta_seconds
        pawn_location = pawn.get_actor_location()
        self._update_companion(pawn_location)
        self._update_pickups(pawn_location)

        if self.progress.tick(delta_seconds) and self.progress.begin_next_round():
            self._spawn_round(pawn_location)

    def companion_movement(self):
        if not is_valid(self.companion) or self.companion_initial_location is None:
            return 0.0
        return (
            self.companion.get_actor_location() - self.companion_initial_location
        ).length()

    def pickup_locations(self):
        return [
            item["actor"].get_actor_location()
            for item in self.pickups
            if is_valid(item["actor"])
        ]

    def close(self):
        for pickup in self.pickups:
            actor = pickup.get("actor")
            if is_valid(actor):
                try:
                    actor.actor_destroy()
                except Exception:
                    pass
        self.pickups = []
        if is_valid(self.companion):
            try:
                self.companion.actor_destroy()
            except Exception:
                pass
        self.companion = None
