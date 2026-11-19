#!/usr/bin/env python3
"""Run the save export checks: which files of SaveData are the player's saves, their order, which
picked keys are refused, and that the archive holds that one save (or every save) and nothing else.
The way back (the archive restored) is tests/android_save_restore.

This does not run Android: the file provider and the share sheet are checked on a device.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--android-jar', type=Path, help='also compile/check the real ShareSheet provider path boundary against the SDK')
args = parser.parse_args()
app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-java-save-export-') as d:
    port = app/'android/game/src/org/stormengine/port'
    check = Path(__file__).with_name('SaveExportCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(port/'SaveExport.java'),
                    str(port/'SaveArchive.java'), str(check)], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', d, 'org.stormengine.port.SaveExportCheck'], check=True)
    if args.android_jar:
        stub = Path(d)/'StormUiView.java'
        stub.write_text('package org.stormengine.port; class StormUiView { void toast(String text, String tone) {} }')
        share = app/'android/game/src/org/stormengine/port/ShareSheet.java'
        path_check = Path(__file__).with_name('ShareSheetPathCheck.java')
        subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-cp', str(args.android_jar),
                        '-d', d, str(stub), str(share), str(path_check)], check=True)
        import os
        subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', os.pathsep.join((d, str(args.android_jar))),
                        'org.stormengine.port.ShareSheetPathCheck'], check=True)
