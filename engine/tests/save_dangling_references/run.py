#!/usr/bin/env python3
"""Save script globals whose ref/aref targets were freed, with the production writer (ASan).

Guards the crash in COMPILER::SaveVariable that a save met at a dangling aref (README.md).
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependency-sources', type=Path, required=True)
p.add_argument('--glm-source', type=Path, required=True)
a = p.parse_args()
deps = a.dependency_sources.resolve()
test_dir = Path(__file__).resolve().parent

WRITER = [
    'void COMPILER::SaveData(',
    'void COMPILER::SaveString(',
    'void COMPILER::WriteVDword(',
    'void COMPILER::SaveVariable(',
    'void COMPILER::SaveAttributesData(',
]


def build(root, directory):
    source = (root / 'src/libs/core/src/compiler.cpp').read_text()
    parts = []
    # Complete, unchanged production methods of COMPILER; check.cpp declares that class around them.
    # Reading goes through save_state.hpp.
    for start in WRITER:
        offset = source.index(start)
        parts.append(source[offset : source.index('\n}', offset) + 2])
    b = Path(directory)
    b.mkdir()
    (b / 'methods.inc').write_text('\n'.join(parts))
    # data.cpp does not use CoreImpl; its include would instantiate the full-engine singleton.
    (b / 'data.cpp').write_text(
        (root / 'src/libs/core/src/data.cpp')
        .read_text()
        .replace('#include "core_impl.h"', '#include "platform/platform.hpp"\n#include <cmath>\nusing std::isnan;')
    )
    (b / 'SDL2').symlink_to(deps / 'SDL/include', target_is_directory=True)
    includes = list((root / 'src/libs').glob('*/include')) + [
        root / 'src/libs/core/src',
        b,
        deps / 'fmt/include',
        deps / 'spdlog/include',
        deps / 'mimalloc/include',
        deps / 'fast_float/include',
        root / 'src/libs/renderer_bgfx/include',
        a.glm_source.resolve(),
    ]
    flags = [
        '-std=c++20',
        '-DFMT_HEADER_ONLY',
        '-DSPDLOG_FMT_EXTERNAL',
        '-g',
        '-fno-omit-frame-pointer',
        '-fno-sanitize-recover=all',
    ] + ['-I' + str(i) for i in includes]
    flags += ['-fsanitize=address,undefined']
    files = [test_dir / 'check.cpp', b / 'data.cpp'] + [
        root / 'src/libs/core/src' / name for name in ('attributes.cpp', 's_vartab.cpp')
    ]
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *map(str, files), '-o', str(b / 'check')], check=True)
    return b / 'check'


with tempfile.TemporaryDirectory(prefix='storm-save-references-') as directory:
    subprocess.run([str(build(Path(__file__).resolve().parents[2], Path(directory) / 'check'))], check=True)
