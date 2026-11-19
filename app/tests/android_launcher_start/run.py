#!/usr/bin/env python3
"""The production PointerQueue: a tap made while the launcher's UI runtime starts is kept for the runtime instead of
being dropped."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-launcher-start-') as temporary:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary,
                    str(java / 'PointerQueue.java'), str(here / 'LauncherStartCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + temporary, '-cp', temporary, 'org.stormengine.port.LauncherStartCheck'], check=True)
