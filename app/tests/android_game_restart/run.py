#!/usr/bin/env python3
"""Run the restart checks (GameRestart.java): the game starts again only after its process is gone, once, bounded."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-game-restart-') as d:
    source = app / 'android/game/src/org/stormengine/port/GameRestart.java'
    check = Path(__file__).with_name('GameRestartTest.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.GameRestartTest'], check=True)
