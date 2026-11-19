#!/usr/bin/env python3
"""The production LauncherEntry rule: when a launcher start only brings the app's task back."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-launcher-entry-') as temporary:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary,
                    str(java / 'LauncherEntry.java'), str(here / 'LauncherEntryCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary, 'org.stormengine.port.LauncherEntryCheck'], check=True)
