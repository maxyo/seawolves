#!/usr/bin/env python3
"""Host checks of the archive reader (android/archive): synthetic archives and adversarial fixtures, source-set
discovery and nested extraction, entry names that are not UTF-8, allocation failures at every position, and the JNI bridge under the JDK checker.
No game data.

Builds the reader for the host with android/archive/build.py --host-testing (fault injection and the host JNI
bridge; sources and builds stay in --build-dir, so later runs are incremental), then runs android/archive/tests.
Needs 7z (fixtures) and JDK 17.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
tests = root / 'android/archive/tests'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
build = args.build_dir.resolve()
missing = [tool for tool in ('7z', 'javac', 'java') if not shutil.which(tool)]
if missing:
    sys.exit(f'archive checks need {", ".join(missing)} (7z generates fixtures; the JNI check runs under the JDK)')
jobs = os.environ.get('STORM_CHECK_JOBS', str(max(2, (os.cpu_count() or 4) // 2)))
subprocess.run(
    [
        sys.executable,
        str(root / 'android/archive/build.py'),
        '--build-dir',
        str(build),
        '--host-testing',
        '--jobs',
        jobs,
    ],
    check=True,
)
library = build / 'output/libstorm_archive.so'
subprocess.run([sys.executable, str(tests / 'test_archives.py'), '--library', str(library)], check=True)
# libarchive's own RAR fixtures (in the pinned sources build.py fetched): multi-volume sets, Unicode names.
libarchive = json.loads((build / 'pinned/sources.json').read_text())['libarchive']
subprocess.run(
    [
        sys.executable,
        str(tests / 'test_discovery.py'),
        '--library',
        str(library),
        '--libarchive-tests',
        str(Path(libarchive) / 'libarchive/test'),
    ],
    check=True,
)
subprocess.run(
    [
        sys.executable,
        str(tests / 'test_names.py'),
        '--library',
        str(library),
        '--libarchive-tests',
        str(Path(libarchive) / 'libarchive/test'),
    ],
    check=True,
)
subprocess.run(
    [sys.executable, str(tests / 'test_oom.py'), '--library', str(library), '--report', str(build / 'oom.json')],
    check=True,
)
classes = build / 'jni-test'
subprocess.run(['javac', '-d', str(classes), str(tests / 'NativeArchives.java')], check=True)
subprocess.run(
    [
        'java',
        '-Xcheck:jni',
        f'-Djava.library.path={build / "reader"}',
        '-cp',
        str(classes),
        'org.stormengine.port.NativeArchives',
    ],
    check=True,
)
print('PASS: archive reader host checks')
