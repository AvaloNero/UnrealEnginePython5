"""Read-only Lyra lifecycle probe used by automated and interactive runs."""

import json
from pathlib import Path
import re
import sys
import traceback

import unreal_engine as ue


GAME_WORLD_TYPES = (1, 3, 5)  # Game, PIE, GamePreview
RESULT_PREFIX = "-UEPLyraSmokeResult="
MODE_PREFIX = "-UEPLyraMode="
REQUIRE_EXPERIENCE = "-UEPLyraRequireExperience"
TIMEOUT_PREFIX = "-UEPLyraTimeoutSeconds="
EXPECTED_CONTROLLERS_PREFIX = "-UEPLyraExpectedPlayerControllers="
REQUIRED_FEATURES_PREFIX = "-UEPLyraRequiredFeatures="
EXPECTED_EXPERIENCE_PREFIX = "-UEPLyraExpectedExperienceContains="
HOLD_READY_PREFIX = "-UEPLyraHoldReadySeconds="
READY_RELEASE_PREFIX = "-UEPLyraReadyReleaseFile="


def _command_line_value(prefix, default=None):
    for argument in sys.argv:
        if argument.startswith(prefix):
            return argument[len(prefix):].strip('"')
    return default


def _has_argument(name):
    return any(argument.lower() == name.lower() for argument in sys.argv)


def _command_line_float(prefix, default):
    value = _command_line_value(prefix)
    try:
        return float(value) if value is not None else float(default)
    except (TypeError, ValueError):
        return float(default)


def _command_line_int(prefix, default):
    value = _command_line_value(prefix)
    try:
        return int(value) if value is not None else int(default)
    except (TypeError, ValueError):
        return int(default)


def _command_line_list(prefix):
    value = _command_line_value(prefix, "")
    return [item.strip() for item in re.split(r"[,+;]", value) if item.strip()]


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
        self.timeout_seconds = max(10.0, _command_line_float(TIMEOUT_PREFIX, 180.0))
        self.expected_player_controllers = max(0, _command_line_int(EXPECTED_CONTROLLERS_PREFIX, 1))
        self.required_features = _command_line_list(REQUIRED_FEATURES_PREFIX)
        self.expected_experience = _command_line_value(EXPECTED_EXPERIENCE_PREFIX, "")
        self.hold_ready_seconds = max(0.0, _command_line_float(HOLD_READY_PREFIX, 0.0))
        self.ready_release_path = _command_line_value(READY_RELEASE_PREFIX)
        self.ready_since = None
        self.ready_announced = False
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
            "player_controller_count": int(_field(snapshot, "PlayerControllerCount", 0)),
            "local_player_controller_count": int(_field(snapshot, "LocalPlayerControllerCount", 0)),
            "remote_player_controller_count": int(_field(snapshot, "RemotePlayerControllerCount", 0)),
            "player_state_count": int(_field(snapshot, "PlayerStateCount", 0)),
            "experience_manager": bool(_field(snapshot, "bHasExperienceManager", False)),
            "experience_loaded": bool(_field(snapshot, "bExperienceLoaded", False)),
            "experience": current_experience.get_path_name() if _is_valid(current_experience) else None,
            "player_pawn": bool(_field(snapshot, "bHasPlayerPawn", False)),
            "pawn_locally_controlled": bool(_field(snapshot, "bPawnLocallyControlled", False)),
            "player_state_ready": bool(_field(snapshot, "bPlayerStateReady", False)),
            "pawn_local_role": _field(snapshot, "PawnLocalRole", ""),
            "pawn_remote_role": _field(snapshot, "PawnRemoteRole", ""),
            "hero_input_ready": bool(_field(snapshot, "bHeroInputReady", False)),
            "ability_system_ready": bool(_field(snapshot, "bAbilitySystemReady", False)),
            "game_features": self._feature_states(snapshot),
        }

    def _pending_requirements(self, report):
        pending = []
        if not report["game_thread"]:
            pending.append("game thread")
        if not report["game_world"]:
            pending.append("game world")
        if self.require_experience and not report["experience_loaded"]:
            pending.append("Experience load")
        if self.expected_experience:
            experience = report["experience"] or ""
            if self.expected_experience.lower() not in experience.lower():
                pending.append("Experience containing {0}".format(self.expected_experience))

        feature_states = {
            item["name"].lower(): item for item in report["game_features"] if item["name"]
        }
        for feature in self.required_features:
            state = feature_states.get(feature.lower())
            if state is None or not state["active"]:
                pending.append("active Game Feature {0}".format(feature))

        if self.mode == "source":
            if report["net_mode"] != "Standalone":
                pending.append("Standalone net mode")
            return pending

        if self.mode == "server_exit":
            if report["net_mode"] not in ("DedicatedServer", "ListenServer"):
                pending.append("server net mode")
            if not report["server_authority"]:
                pending.append("server authority")
            return pending

        if self.mode in ("standalone", "packaged", "client"):
            expected_net_mode = "Client" if self.mode == "client" else "Standalone"
            if report["net_mode"] != expected_net_mode:
                pending.append("{0} net mode".format(expected_net_mode))
            if report["local_player_controller_count"] < self.expected_player_controllers:
                pending.append("{0} local player controller(s)".format(self.expected_player_controllers))
            if not report["player_pawn"]:
                pending.append("local player pawn")
            if not report["pawn_locally_controlled"]:
                pending.append("locally controlled pawn")
            if not report["player_state_ready"]:
                pending.append("replicated PlayerState")
            if not report["hero_input_ready"]:
                pending.append("Lyra Enhanced Input bindings")
            if not report["ability_system_ready"]:
                pending.append("Lyra Ability System")
            if self.mode == "client" and report["pawn_local_role"] != "AutonomousProxy":
                pending.append("AutonomousProxy pawn role")
            if self.mode in ("standalone", "packaged") and report["pawn_local_role"] != "Authority":
                pending.append("Authority pawn role")
        elif self.mode == "server":
            if report["net_mode"] not in ("DedicatedServer", "ListenServer"):
                pending.append("server net mode")
            if not report["server_authority"]:
                pending.append("server authority")
            if report["remote_player_controller_count"] < self.expected_player_controllers:
                pending.append("{0} remote player controller(s)".format(self.expected_player_controllers))
            if not report["player_pawn"]:
                pending.append("server-side player pawn")
            if not report["player_state_ready"]:
                pending.append("server-side PlayerState")
            if not report["ability_system_ready"]:
                pending.append("server-side Ability System")
            if report["player_pawn"] and report["pawn_local_role"] != "Authority":
                pending.append("server Authority pawn role")
        else:
            pending.append("known validation mode")
        return pending

    def _assert_ready(self, report):
        if sys.version_info[:2] != (3, 11):
            raise RuntimeError("expected CPython 3.11, got {0}".format(sys.version))
        pending = self._pending_requirements(report)
        if pending:
            raise RuntimeError("Lyra readiness timed out waiting for: {0}".format(", ".join(pending)))

    def _write_result(self, status, snapshot=None, error=None):
        if not self.result_path or self.finished:
            return
        self.finished = True
        report = {
            "schema_version": 1,
            "status": status,
            "mode": self.mode,
            "python_version": ".".join(str(value) for value in sys.version_info[:3]),
            "elapsed_seconds": round(self.elapsed, 3),
            "world_ticks": self.world_ticks,
            "experience_events": self.experience_events,
            "required_features": self.required_features,
            "expected_experience_contains": self.expected_experience,
            "expected_player_controllers": self.expected_player_controllers,
            "hold_ready_seconds": self.hold_ready_seconds,
            "ready_release_gate": bool(self.ready_release_path),
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
        try:
            ue.request_exit()
            return
        except Exception:
            ue.log_error(traceback.format_exc())

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
                if self.result_path and self.elapsed > self.timeout_seconds:
                    raise RuntimeError("Lyra game world/bridge was not ready before timeout")
                return True

            self.world_ticks += 1
            snapshot = self.bridge.call_function("CaptureSnapshot")
            snapshot_report = self._snapshot_report(snapshot)
            minimum_ticks = 12 if self.mode == "source" else 30
            if self.world_ticks < minimum_ticks:
                return True
            pending = self._pending_requirements(snapshot_report)
            if pending:
                self.ready_since = None
                self.ready_announced = False
                if self.elapsed > self.timeout_seconds:
                    self._assert_ready(snapshot_report)
                return True

            if self.ready_since is None:
                self.ready_since = self.elapsed
            if self.elapsed - self.ready_since < self.hold_ready_seconds:
                return True

            if self.ready_release_path:
                if not self.ready_announced:
                    ue.log("UEP_LYRA_REQUIREMENTS_READY {0}".format(self.mode))
                    self.ready_announced = True
                if not Path(self.ready_release_path).is_file():
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
