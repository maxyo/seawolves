#!/usr/bin/env python3
"""Host checks of the engine cache root and the removal of the unused cache volume's root (GameCache.java).

Clearing the cache on a device (pm clear --cache-only) is checked on the emulator, not here.
"""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-java-cache-') as d:
    source = app/'android/game/src/org/stormengine/port/GameCache.java'
    check = Path(__file__).with_name('GameCacheCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    work = Path(d)/'work'
    work.mkdir()
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.GameCacheCheck', str(work)], check=True)
