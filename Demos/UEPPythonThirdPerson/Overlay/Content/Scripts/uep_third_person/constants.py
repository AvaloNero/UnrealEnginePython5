"""Shared constants for the Python-first Third Person sample."""

GAME_WORLD_TYPES = (1, 3, 5)  # Game, PIE, GamePreview
GAME_MODE_URL = "/Engine/Transient.UEPThirdPersonGameMode"
MAP_URL = "/Game/ThirdPerson/Lvl_ThirdPerson?game=" + GAME_MODE_URL
SMOKE_RESULT_PREFIX = "-UEPPythonThirdPersonSmokeResult="

TRIGGERED = 1
STARTED = 2
COMPLETED = 16

PICKUP_COUNT = 6
PICKUP_RADIUS = 420.0
PICKUP_HEIGHT = 115.0
COLLECTION_DISTANCE = 135.0
ROUND_COMPLETE_SECONDS = 3.0
