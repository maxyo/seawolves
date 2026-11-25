#!/usr/bin/env python3
"""The FF vertex programs compiled to SPIR-V by the pinned shaderc export and write the PointSize built-in (our
shaderc patch bgfx-shaderc-point-size.patch), without a GPU. The same source with a bare PSIZE output must compile
without it: the built-in comes from the patched field.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--shaderc', type=Path, required=True)
p.add_argument('--bgfx-include', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
renderer = root / 'src/libs/renderer_bgfx'
sys.path.insert(0, str(renderer / 'cmake'))
import spirv_blob  # noqa: E402
field = r'(?:\[\[vk::builtin\("PointSize"\)\]\] )?float gl_PointSize : PSIZE;'


def run(*args):
    subprocess.run(
        [
            str(a.shaderc),
            '--type',
            'vertex',
            '--platform',
            'android',
            '-p',
            'spirv',
            '--varyingdef',
            str(renderer / 'shaders/varying.def.sc'),
            *map(str, args),
        ],
        check=True,
    )


def point_size_stored(path):
    builtins, outputs, stores = set(), set(), set()
    for opcode, args in spirv_blob.instructions(spirv_blob.module(path.read_bytes())):
        if opcode == 71 and len(args) == 3 and args[1:] == (11, 1):
            builtins.add(args[0])  # OpDecorate BuiltIn PointSize
        if opcode == 59 and args[2] == 3:
            outputs.add(args[1])  # OpVariable Output
        if opcode == 62:
            stores.add(args[0])  # OpStore
    return bool(builtins & outputs & stores)


with tempfile.TemporaryDirectory(prefix='storm-pointsize-') as directory:
    work = Path(directory)
    for variant in ('vs_ff', 'vs_ff_unlit'):
        hlsl = work / (variant + '.hlsl')
        run(
            '--preprocess',
            '-f',
            renderer / f'shaders/{variant}.sc',
            '-o',
            hlsl,
            '-i',
            a.bgfx_include,
            '-i',
            renderer / 'shaders/include',
            '-i',
            renderer / 'fx/shaders/include',
        )
        source = hlsl.read_text()
        assert len(re.findall(field, source)) == 1, 'production FF shader PointSize field missing or duplicated'
        for control, text, expected in [
            ('production', source, True),
            ('bare-PSIZE', re.sub(field, 'float gl_PointSize : PSIZE;', source), False),
        ]:
            hlsl.write_text(text)
            output = work / (variant + '-' + control + '.bin')
            run('--raw', '-f', hlsl, '-o', output)
            actual = point_size_stored(output)
            assert actual == expected, f'{variant}/{control}: stored PointSize built-in={actual}, expected {expected}'
            print(f'PASS {variant}/{control}: stored PointSize built-in={actual}', flush=True)
print('PointSize SPIR-V check PASS')
