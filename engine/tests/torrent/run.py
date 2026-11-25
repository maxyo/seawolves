#!/usr/bin/env python3
"""Host checks of the torrent library (android/torrent): metadata, a local transfer, JNI under the JDK checker,
allocation failures. No network peers or game data.

Builds the pinned libtorrent, Boost headers and OpenSSL for the host with tools/android/build_torrent.py (sources
and builds stay in --build-dir, so later runs are incremental) and runs its ctest. Needs Perl, make and JDK 17.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
jobs = os.environ.get('STORM_CHECK_JOBS', str(max(2, (os.cpu_count() or 4) // 2)))
subprocess.run(
    [
        sys.executable,
        str(root / 'tools/android/build_torrent.py'),
        '--work',
        str(args.build_dir.resolve()),
        '--jobs',
        jobs,
    ],
    check=True,
)
