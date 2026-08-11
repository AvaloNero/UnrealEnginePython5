"""Project startup hook for the staged UEP Lyra validation project."""

import sys

sys.dont_write_bytecode = True

import unreal_engine as ue


ue.log("UEP_LYRA_SCRIPT_LOADED")

import uep_lyra_probe  # noqa: E402,F401
