#!/usr/bin/env python3
"""Run real shortcut protocol checks and optionally compile all app/SDL Java.

This does not run Android, JNI, the SDL lifecycle, native game handlers, or audio.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--android-jar', type=Path)
p.add_argument('--sdl-source', type=Path)
a = p.parse_args()
if bool(a.android_jar) != bool(a.sdl_source):
    p.error('Pass both --android-jar and --sdl-source, or neither')
app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-java-shortcuts-') as d:
    helper = app/'android/game/src/org/stormengine/port/GameplayShortcuts.java'
    check = Path(__file__).with_name('GameplayShortcutsCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-d', d, str(helper), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.GameplayShortcutsCheck'], check=True)
    if a.android_jar:
        sources = list((a.sdl_source.resolve()/'android-project/app/src/main/java').rglob('*.java'))
        sources += list((app/'android/game/src').rglob('*.java'))
        subprocess.run(['javac', '-source', '8', '-target', '8', '-classpath', str(a.android_jar.resolve()),
                        '-d', d, *map(str, sources)], check=True)
        print('PASS: complete real app + SDL Java sources compile against SDK')
