#!/usr/bin/env python3
"""Check which engine log lines end the test loop's wait for the automatic graphics preset (AutoPresetOutcome.java)."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-testloop-outcome-') as d:
    source = app / 'android/game/testloop/on/org/stormengine/port/AutoPresetOutcome.java'
    check = Path(__file__).with_name('AutoPresetOutcomeCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.AutoPresetOutcomeCheck'], check=True)
