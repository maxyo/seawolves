#!/usr/bin/env python3
"""Run the engine exit checks (EngineExit.java): the wait for the engine thread at Activity destruction is bounded."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-engine-exit-') as d:
    source = app / 'android/game/src/org/stormengine/port/EngineExit.java'
    check = Path(__file__).with_name('EngineExitTest.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.EngineExitTest'], check=True)
