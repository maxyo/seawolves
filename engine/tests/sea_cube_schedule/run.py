#!/usr/bin/env python3
"""The sea's cube faces (src/libs/sea/src/cube_schedule.hpp) while the camera is dragged round a ship, follows it
sailing, stands, and turns in place: how far off the reflections are in each frame, and how many faces a frame
costs, against the previous schedule (all faces in turn by the clock) built as the reference."""

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-sea-cube-') as temporary:
    binary = Path(temporary) / 'check'
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++20',
            '-O2',
            '-g',
            '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer',
            '-I' + str(root / 'src/libs/sea/src'),
            str(Path(__file__).with_name('check.cpp')),
            '-o',
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
