"""Project startup hook imported automatically by UnrealEnginePython."""

import sys

# Scripts are staged as source. Do not let editor/cook startup add host-specific
# bytecode caches to the NonUFS package manifest.
sys.dont_write_bytecode = True

import unreal_engine as ue


ue.log("UEP_PYTHON_THIRD_PERSON_SCRIPT_LOADED")

# Importing the bootstrap creates the transient gameplay UClasses and starts a
# process-wide playable runtime. The automated smoke ticker is added only when
# its explicit command-line result argument is present.
from uep_third_person import bootstrap  # noqa: E402,F401
