#!/usr/bin/env python3
"""Check weighted label layout; optionally exercise the exact render projection with pinned GLM."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Enable real projection checks with extracted pinned GLM 1.0.3')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]


def source(path):
    return (root / path).read_text()


with tempfile.TemporaryDirectory(prefix='storm-worldmap-labels-') as d:
    out = Path(d)
    binary = out / 'check'
    flags = []
    if args.glm_source:
        islands = source('src/libs/worldmap/src/wdm_islands.cpp')
        start = islands.index('\n', islands.index('    const float outline =')) + 1
        end = islands.index('    // Draw icons and write text', start)
        (out / 'label_projection.inc').write_text(islands[start:end])
        (out / 'wdm_label_layout.hpp').write_text(source('src/libs/worldmap/src/wdm_label_layout.hpp'))
        flags = [
            '-DSTORM_LABEL_PROJECTION_CHECK',
            '-D__ANDROID__',
            '-I' + str(args.glm_source.resolve()),
            '-I' + str(root / 'src/libs/math/include'),
            '-I' + str(root / 'src/libs/util/include'),
        ]
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++20',
            '-fsanitize=address,undefined',
            '-I' + d,
            '-I' + str(root / 'src/libs/worldmap/src'),
            *flags,
            str(Path(__file__).with_name('check.cpp')),
            '-o',
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print(
    'PASS: weighted collisions, stable gap hysteresis, deterministic order'
    + (
        '; world anchor independent of HUD; pan/zoom/rotation; natural clipping and camera-plane transitions'
        if args.glm_source
        else '; projection checks require --glm-source'
    )
)
