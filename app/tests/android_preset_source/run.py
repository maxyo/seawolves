#!/usr/bin/env python3
"""Run the graphics preset source checks (PresetSource.java): the player's choice is never measured over."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-preset-source-') as d:
    source = app / 'android/game/src/org/stormengine/port/PresetSource.java'
    check = Path(__file__).with_name('PresetSourceCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.PresetSourceCheck'], check=True)
