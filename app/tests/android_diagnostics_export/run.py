#!/usr/bin/env python3
"""Run the diagnostics export checks: which log lines go into the shared file, the renderer
facts taken from the engine's logged lines, engine log tails, the section order and the native crash backtrace
read from a tombstone (Tombstone.java).

This does not run Android: logcat, the EGL probe, the file provider and the share sheet are
checked on a device.
"""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-java-diagnostics-') as d:
    port = app/'android/game/src/org/stormengine/port'
    here = Path(__file__).parent
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(port/'DiagnosticsReport.java'),
                    str(port/'Tombstone.java'), str(here/'DiagnosticsReportCheck.java'), str(here/'TombstoneCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', d, 'org.stormengine.port.DiagnosticsReportCheck'], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', d, 'org.stormengine.port.TombstoneCheck'], check=True)
