#!/usr/bin/env python3
"""Run the minimum requirements checks (MinimumRequirements.java): the launcher's below-minimum notice is decided from
RAM, the graphics API and the engine's «Низкая» measurement. org.json is the host stand-in of tests/android_archive_install."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
app = here.parents[1]
stubs = here.parent / 'android_archive_install/stubs'
with tempfile.TemporaryDirectory(prefix='storm-minimum-requirements-') as d:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d,
                    str(app / 'android/game/src/org/stormengine/port/MinimumRequirements.java'), str(here / 'MinimumRequirementsCheck.java'),
                    *[str(path) for path in sorted(stubs.rglob('*.java'))]], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.MinimumRequirementsCheck', str(Path(d) / 'files')], check=True)
