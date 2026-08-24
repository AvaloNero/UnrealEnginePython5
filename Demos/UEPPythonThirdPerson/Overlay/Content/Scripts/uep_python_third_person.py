"""Compatibility entry point for the modular 0.6 Third Person runtime.

New code lives under :mod:`uep_third_person`. Keeping this module means existing
staged projects and documentation can import the old 0.2 entry point without
registering a second set of transient Unreal classes.
"""

from uep_third_person.bootstrap import (  # noqa: F401
    UEPThirdPersonCharacter,
    UEPThirdPersonGameMode,
    UEPThirdPersonPlayerController,
    runtime,
    runtime_ticker,
    smoke,
    smoke_ticker,
)
