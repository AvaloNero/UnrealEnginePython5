"""Read-only Lyra lifecycle probe used by automated and interactive runs."""

import json
from pathlib import Path
import sys
import traceback

import unreal_engine as ue


GAME_WORLD_TYPES = (1, 3, 5)  # Game, PIE, GamePreview
RESULT_PREFIX = "-UEPLyraSmokeResult="
MODE_PREFIX = "-UEPLyraMode="
REQUIRE_EXPERIENCE = "-UEPLyraRequireExperience"


def _command_line_value(prefix, default=None):
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):].strip('"')
    return default


def _has_argument(name):
    return any(argument.lower() == name.lower() for argument in sys.argv)


def _is_valid(obj):
    try:
        return obj is not None and obj.is_valid()
    except Exception:
        return False


def _field(value, name, default=None):
    try:
        return getattr(value, name)
    except Exception:
        return default


class LyraRuntimeProbe:
    def __init__(self):
        self.elapsed = 0.0
        self.world_ticks = 0
        self.result_path = _command_line_value(RESULT_PREFIX)
        self.mode = _command_line_value(MODE_PREFIX, "source")
        self.require_experience = _has_argument(REQUIRE_EXPERIENCE)
        self.bridge_class = ue.find_class("UEPLyraWorldSubsystem")
        self.library = ue.find_class("UEPLyraBridgeLibrary").get_cdo()
        self.world = None
        self.bridge = None
        self.bound_callback = None
        self.experience_events = 0
        self.finished = False

    def _game_world(self):
        for world in ue.all_worlds():
            try:
                if world.get_world_type() in GAME_WORLD_TYPES:
                    return world
            except Exception:
                continue
        return None

    def _attach(self, world):
        bridge = self.library.call_function("GetBridgeForWorld", world)
        if not _is_valid(bridge):
            return False
        if _is_valid(self.bridge) and self.bridge.get_path_name() == bridge.get_path_name():
            return True
        self.shutdown_binding()
        self.world = world
        self.bridge = bridge
        self.bound_callback = self._on_experience_ready
        bridge.bind_event("OnExperienceReady", self.bound_callback)
        ue.log("UEP_LYRA_BRIDGE_ATTACHED")
        return True

    def _on_experience_ready(self, experience):
        self.experience_events += 1
        ue.log("UEP_LYRA_EXPERIENCE_READY")

    def shutdown_binding(self):
        if _is_valid(self.bridge) and self.bound_callback is not None:
            try:
                self.bridge.unbind_event("OnExperienceReady", self.bound_callback)
            except Exception:
                ue.log_error(traceback.format_exc())
        self.bound_callback = None
        self.bridge = None

    def _feature_states(self, snapshot):
        states = []
        for item in _field(snapshot, "GameFeatures", []) or []:
            states.append(
                {
                    "name": _field(item, "Name", ""),
                    "state": _field(item, "State", ""),
                    "active": bool(_field(item, "bActive", False)),
                }
            )
        return states

    def _snapshot_report(self, snapshot):
        current_experience = _field(snapshot, "CurrentExperience")
        return {
            "game_thread": bool(_field(snapshot, "bIsGameThread", False)),
            "game_world": bool(_field(snapshot, "bIsGameWorld", False)),
            "world": _field(snapshot, "WorldName", ""),
            "net_mode": _field(snapshot, "NetMode", ""),
            "server_authority": bool(_field(snapshot, "bHasServerAuthority", False)),
            "experience_manager": bool(_field(snapshot, "bHasExperienceManager", False)),
            "experience_loaded": bool(_field(snapshot, "bExperienceLoaded", False)),
            "experience": current_experience.get_path_name() if _is_valid(current_experience) else None,
            "player_pawn": bool(_field(snapshot, "bHasPlayerPawn", False)),
            "hero_input_ready": bool(_field(snapshot, "bHeroInputReady", False)),
            "ability_system_ready": bool(_field(snapshot, "bAbilitySystemReady", False)),
            "game_features": self._feature_states(snapshot),
        }

    def _assert_ready(self, report):
        if sys.version_info[:2] != (3, 11):
            raise RuntimeError("expected CPython 3.11, got {0}".format(sys.version))
        if not report["game_thread"] or not report["game_world"]:
            raise RuntimeError("Lyra bridge snapshot was not captured on a game world/game thread")
        if self.mode == "source":
            if report["net_mode"] != "Standalone":
                raise RuntimeError("source smoke expected Standalone net mode")
            return
        if self.require_experience and not report["experience_loaded"]:
            raise RuntimeError("Lyra experience did not finish loading")
        if self.mode in ("standalone", "client"):
            if not report["player_pawn"]:
                raise RuntimeError("Lyra player pawn was not ready")
            if not report["hero_input_ready"]:
                raise RuntimeError("Lyra Enhanced Input bindings were not ready")
            if not report["ability_system_ready"]:
                raise RuntimeError("Lyra Ability System was not ready")
        if self.mode == "client" and report["net_mode"] != "Client":
            raise RuntimeError("multiplayer client did not enter Client net mode")
        if self.mode == "server" and not report["server_authority"]:
            raise RuntimeError("server probe did not retain authority")

    def _write_result(self, status, snapshot=None, error=None):
        if not self.result_path or self.finished:
            return
        self.finished = True
        report = {
            "schema_version": 1,
            "status": status,
            "mode": self.mode,
            "python_version": ".".join(str(value) for value in sys.version_info[:3]),
            "world_ticks": self.world_ticks,
            "experience_events": self.experience_events,
            "snapshot": self._snapshot_report(snapshot) if snapshot is not None else None,
            "error": error,
        }
        path = Path(self.result_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        (ue.log if status == "passed" else ue.log_error)(
            "UEP_LYRA_SMOKE_PASSED" if status == "passed" else "UEP_LYRA_SMOKE_FAILED"
        )

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

    def tick(self, delta_seconds):
        try:
            self.elapsed += delta_seconds
            world = self._game_world()
            if not _is_valid(world) or not self._attach(world):
                if self.result_path and self.elapsed > 120.0:
                    raise RuntimeError("Lyra game world/bridge was not ready before timeout")
                return True

            self.world_ticks += 1
            snapshot = self.bridge.call_function("CaptureSnapshot")
            snapshot_report = self._snapshot_report(snapshot)
            minimum_ticks = 12 if self.mode == "source" else 30
            if self.world_ticks < minimum_ticks:
                return True
            if self.require_experience and not snapshot_report["experience_loaded"]:
                if self.elapsed > 120.0:
                    raise RuntimeError("Lyra experience was not loaded before timeout")
                return True

            self._assert_ready(snapshot_report)
            self._write_result("passed", snapshot)
            self.shutdown_binding()
            self._request_quit()
            return False
        except Exception:
            details = traceback.format_exc()
            ue.log_error(details)
            self._write_result("failed", error=details)
            self.shutdown_binding()
            self._request_quit()
            return False


try:
    _previous = globals().get("runtime")
    if _previous is not None:
        _previous.shutdown_binding()
except Exception:
    ue.log_error(traceback.format_exc())

runtime = LyraRuntimeProbe()
ticker = ue.add_ticker(runtime.tick)
