#!/usr/bin/env python3
"""Compile the storage/lifecycle check for Android with $CXX (tools/android/ci_dependencies.sh passes the NDK's
clang++). Its host run is the host check android_runtime (tests/CMakeLists.txt)."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
a = p.parse_args()
r = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(r / 'tools'))
import pins  # noqa: E402

with tempfile.TemporaryDirectory(prefix='storm-runtime-') as d:
    b = Path(d)
    (b / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    contract = r / 'cmake/StormAppContract.cmake'
    subprocess.run(['cmake', f'-DSTORM_APP_CONTRACT_INCLUDE={b}', '-P', str(contract)], check=True)
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++20',
            '-I' + str(b),
            '-I' + str(r / 'src/libs/util/include'),
            '-I' + str(pins.json_headers()),
            '-c',
            str(Path(__file__).with_name('check.cpp')),
            '-o',
            str(b / 'check.o'),
        ],
        check=True,
    )
    print('PASS: storage/lifecycle compiled')
