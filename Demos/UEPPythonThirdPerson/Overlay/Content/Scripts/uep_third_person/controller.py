"""Python-generated PlayerController for the Third Person sample."""

import unreal_engine as ue
from unreal_engine.classes import BoolProperty, PlayerController

from .assets import IMC_DEFAULT, IMC_MOUSE_LOOK


class UEPThirdPersonPlayerController(PlayerController):
    PythonMappingReady = BoolProperty

    def __init__(self):
        self.PythonMappingReady = False

    def _remove_mapping_contexts(self):
        for context in (IMC_DEFAULT, IMC_MOUSE_LOOK):
            try:
                if self.has_enhanced_input_mapping_context(context):
                    self.remove_enhanced_input_mapping_context(context)
            except Exception:
                pass
        self.PythonMappingReady = False

    def _ensure_mapping_contexts(self):
        try:
            if (
                self.PythonMappingReady
                and self.has_enhanced_input_mapping_context(IMC_DEFAULT)
                and self.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK)
            ):
                return True
            for context in (IMC_DEFAULT, IMC_MOUSE_LOOK):
                if not self.has_enhanced_input_mapping_context(context):
                    self.add_enhanced_input_mapping_context(context, 0)
            if not (
                self.has_enhanced_input_mapping_context(IMC_DEFAULT)
                and self.has_enhanced_input_mapping_context(IMC_MOUSE_LOOK)
            ):
                self.PythonMappingReady = False
                return False
        except Exception:
            self._remove_mapping_contexts()
            return False

        self.PythonMappingReady = True
        ue.log("UEP_PYTHON_THIRD_PERSON_MAPPING_READY")
        return True

    def PythonBeginPlay(self):
        self._ensure_mapping_contexts()
        ue.log("UEP_PYTHON_THIRD_PERSON_CONTROLLER_READY")

    PythonBeginPlay.override = "ReceiveBeginPlay"

    def PythonTick(self, _DeltaSeconds: float):
        # The local-player Enhanced Input subsystem can become available after
        # ReceiveBeginPlay during startup/possession, so keep the setup retryable.
        self._ensure_mapping_contexts()

    PythonTick.override = "ReceiveTick"
