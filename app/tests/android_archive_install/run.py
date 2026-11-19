#!/usr/bin/env python3
"""Run real shared transaction + archive adapter with explicit JNI/JSON recording boundaries."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--android-jar', type=Path, help='Also compile the real wrapper/adapter against this SDK API')
args = parser.parse_args()
here = Path(__file__).resolve().parent
app = here.parents[1]
java = app / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-archive-install-') as tmp:
    out = Path(tmp)
    subprocess.run(['javac', '-source', '8', '-target', '8', '-d', str(out),
                    str(java / 'ResourceImporter.java'), str(java / 'ArchiveInstaller.java'),
                    str(here / 'ArchiveInstallerCheck.java'),
                    *[str(path) for path in sorted((here / 'stubs').rglob('*.java'))]], check=True)
    subprocess.run(['java', '-ea', '-cp', str(out), 'org.stormengine.port.ArchiveInstallerCheck', str(out / 'data')], check=True)
    if args.android_jar:
        sdk = out / 'sdk'
        sdk.mkdir()
        subprocess.run(['javac', '-source', '8', '-target', '8', '-cp', str(args.android_jar), '-d', str(sdk),
                        *[str(java / name) for name in ('ResourceImporter.java', 'NativeArchives.java', 'ArchiveInstaller.java')]], check=True)
        print('PASS real NativeArchives/ArchiveInstaller Android API compile', flush=True)
