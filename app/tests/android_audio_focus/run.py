#!/usr/bin/env python3
"""Run the audio focus checks (AudioFocusState.java): each focus change maps to one engine action, no duplicates."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-audio-focus-') as d:
    source = app / 'android/game/src/org/stormengine/port/AudioFocusState.java'
    check = Path(__file__).with_name('AudioFocusStateTest.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.AudioFocusStateTest'], check=True)
