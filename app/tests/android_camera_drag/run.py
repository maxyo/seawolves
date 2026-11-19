#!/usr/bin/env python3
"""Run the production CameraDrag on the host (no Android classes needed)."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-camera-drag-') as temporary:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary,
                    str(java / 'CameraDrag.java'), str(here / 'CameraDragCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary, 'org.stormengine.port.CameraDragCheck'], check=True)
