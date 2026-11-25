#!/usr/bin/env python3
"""Compile the engine -> host OS hook checks (src/libs/util/include/platform/host_hooks.hpp) for Android with $CXX
(tools/android/ci_dependencies.sh passes the NDK's clang++), which also compiles the JNI frame work hint (report and
target). Their host run is the host check host_hooks (tests/CMakeLists.txt)."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdl-source', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-host-hooks-') as directory:
    build = Path(directory)
    (build / 'SDL2').symlink_to(a.sdl_source.resolve() / 'include', target_is_directory=True)
    contract = root / 'cmake/StormAppContract.cmake'
    subprocess.run(['cmake', f'-DSTORM_APP_CONTRACT_INCLUDE={build}', '-P', str(contract)], check=True)
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++20',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-I' + str(root / 'src/libs/util/include'),
            '-I' + str(build),
            '-c',
            str(Path(__file__).with_name('check.cpp')),
            '-o',
            str(build / 'check.o'),
        ],
        check=True,
    )
    print('PASS: host hooks compiled for the target')
