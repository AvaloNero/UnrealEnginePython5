"""Project startup hook imported automatically by UnrealEnginePython."""

import unreal_engine as ue


ue.log("UEP_DEMO_SCRIPT_LOADED")

# Importing the module keeps its runtime and ticker alive for the process.
import uep_third_person_demo  # noqa: E402,F401
