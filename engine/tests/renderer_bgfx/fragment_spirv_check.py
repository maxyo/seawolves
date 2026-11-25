#!/usr/bin/env python3
"""No compiled Vulkan fragment shader takes a texel of a stage or of the material texture through OpSelect, without a
GPU.

The SPIR-V build compiles the shaders as HLSL, where `c ? a : b` evaluates both sides: `c ? texture(...) : x` reads
the texture for every pixel, whichever side is taken. Such a choice is written as if / else
(shaders/include/ff_stages.sh).
Reads the build's blobs (shaders-gen/storm_shaders/fs_*.spv.bin, bgfx's shader binary format).
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src/libs/renderer_bgfx/cmake'))
import spirv_blob  # noqa: E402

OP_LOAD, OP_VECTOR_SHUFFLE, OP_COMPOSITE_EXTRACT, OP_COPY_OBJECT, OP_SAMPLED_IMAGE, OP_SELECT = 61, 79, 81, 83, 86, 169
IMAGE_READS = set(range(87, 98))  # OpImageSample*, OpImageFetch, OpImageGather, OpImageDrefGather
WATCHED = re.compile(r'^(s_tex\d|s_fxMaterial)')


def selected_reads(words):
    """The samplers of the watched texels that OpSelect instructions take as a value, one entry per value."""
    defs, selects = {}, []
    for op, args in spirv_blob.instructions(words):
        if op in IMAGE_READS or op in (OP_LOAD, OP_VECTOR_SHUFFLE, OP_COMPOSITE_EXTRACT, OP_COPY_OBJECT,
                                       OP_SAMPLED_IMAGE):
            defs[args[1]] = (op, args)
        elif op == OP_SELECT:
            selects.append(args)
    names = spirv_blob.names(words)

    def sampler(value):
        """The sampler variable a value was read from (through extracts and shuffles of one read), or None."""
        op, args = defs.get(value, (None, ()))
        if op in (OP_COMPOSITE_EXTRACT, OP_COPY_OBJECT):
            return sampler(args[2])
        if op == OP_VECTOR_SHUFFLE and args[2] == args[3]:
            return sampler(args[2])
        if op in IMAGE_READS:
            image = defs.get(args[2], (None, ()))
            if image[0] == OP_SAMPLED_IMAGE:
                image = defs.get(image[1][2], (None, ()))
            if image[0] == OP_LOAD:
                return names.get(image[1][2])
        return None

    found = []
    for args in selects:
        found += [s for s in (sampler(args[3]), sampler(args[4])) if s and WATCHED.match(s)]
    return found


def main():
    if len(sys.argv) != 2:
        sys.exit(f'usage: {sys.argv[0]} <build>/renderer_bgfx/shaders-gen/storm_shaders')
    shaders = sorted(Path(sys.argv[1]).glob('fs_*.spv.bin'))
    if not shaders:
        sys.exit(f'FAIL: no compiled fragment shaders (fs_*.spv.bin) in {sys.argv[1]}')
    failures = []
    for path in shaders:
        for read in selected_reads(spirv_blob.module(path.read_bytes())):
            failures.append(f'{path.name}: OpSelect takes a texel of {read}: the read runs whichever side is taken')
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print(f'PASS: no fragment shader selects a stage or material texel ({len(shaders)} checked)')


main()
