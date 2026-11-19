#!/usr/bin/env python3
"""Run the default texture level checks (TextureMemoryClass.java) on mocked memory sizes."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-texture-memory-class-') as d:
    source = app / 'android/game/src/org/stormengine/port/TextureMemoryClass.java'
    check = Path(__file__).with_name('TextureMemoryClassCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.TextureMemoryClassCheck'], check=True)
