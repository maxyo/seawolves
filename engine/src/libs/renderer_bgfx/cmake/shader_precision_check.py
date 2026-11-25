#!/usr/bin/env python3
"""Fails when an embedded shader blob (shaderc output, cmake/shader_blobs.py) is not highp everywhere.

GPUs differ in what mediump means: Adreno and Mali run it as fp16, others as fp32. Positions, depth, time and the
HDR / tone / occlusion maths overflow or band in fp16, so every shader of the engine is highp, without exceptions.

ESSL blobs (OpenGL ES, <name>.bin): no `mediump` or `lowp` anywhere. shaderc's spirv-cross output declares most
variables with an explicit qualifier, but not all (OpUndef placeholders, for example), so its default precision is
highp too (patches/bgfx-essl-highp-default.patch) and the default line `precision mediump float;` is an error like
any other mediump.

SPIR-V blobs (Vulkan, <name>.spv.bin): no RelaxedPrecision decoration, which is what mediump / lowp and HLSL's
min16float become there and what lets a driver compute in fp16.
  shader_precision_check.py <stamp> <blob.bin>...
"""

import pathlib
import re
import sys

import spirv_blob

OP_DECORATE, OP_MEMBER_DECORATE, RELAXED_PRECISION = 71, 72, 0


def relaxed_precision(data, name):
    """The RelaxedPrecision decorations of the SPIR-V module inside a bgfx shader blob."""
    try:
        code = spirv_blob.instructions(spirv_blob.module(data))
        found = sum(
            1
            for op, args in code
            if (op == OP_DECORATE and args[1] == RELAXED_PRECISION)
            or (op == OP_MEMBER_DECORATE and args[2] == RELAXED_PRECISION)
        )
    except ValueError as error:
        return [f'{name}: {error}']
    return [f'{name}: {found} RelaxedPrecision decorations'] if found else []


stamp, *blobs = sys.argv[1:]
problems = []
for blob in blobs:
    path = pathlib.Path(blob)
    data = path.read_bytes()
    if path.name.endswith('.spv.bin'):
        problems += relaxed_precision(data, path.name)
        continue
    text = data.decode('latin1')
    start = text.find('precision ')
    if start < 0:
        continue  # no precision statement: a vertex shader, highp by default
    for line in text[start:].split('\n'):
        if re.search(r'\b(mediump|lowp)\b', line):
            problems.append(f'{path.name}: {line.strip()[:120]}')
if problems:
    sys.exit('Shader precision check failed (only highp is allowed, see this script):\n  ' + '\n  '.join(problems))
pathlib.Path(stamp).write_text(f'{len(blobs)} shader blobs checked\n')
