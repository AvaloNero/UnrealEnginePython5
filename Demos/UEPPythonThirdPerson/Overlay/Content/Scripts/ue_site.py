"""Project startup hook imported automatically by UnrealEnginePython."""

import sys

# Scripts are staged as source. Do not let editor/cook startup add host-specific
# bytecode caches to the NonUFS package manifest.
sys.dont_write_bytecode = True

import unreal_engine as ue


ue.log("UEP_PYTHON_THIRD_PERSON_SCRIPT_LOADED")

# Importing the module creates the transient gameplay UClasses and retains the
# process-wide smoke/runtime ticker across map travel.
import uep_python_third_person  # noqa: E402,F401
