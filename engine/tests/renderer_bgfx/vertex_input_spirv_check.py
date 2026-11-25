#!/usr/bin/env python3
"""Every compiled vertex shader, without a GPU: its bone indices (a_indices) are an unsigned integer input on Vulkan,
and its position is invariant on Vulkan and on OpenGL ES.

bgfx binds the non-normalised Uint8 indices as VK_FORMAT_R8G8B8A8_UINT (and GL_UNSIGNED_BYTE through
glVertexAttribIPointer). A signed `ivec4` input does not match the format's numeric type: undefined input
(VUID-VkGraphicsPipelineCreateInfo-Input-08733); PowerVR's Vulkan driver drew characters without legs. The depth
pre-pass draws a scene draw with its vertex shader in two programs that must compute the same depth
(patches/bgfx-shaderc-invariant-position.patch). Reads the build's shader blobs (shaders-gen/storm_shaders/vs_*.bin,
bgfx's shader binary format: SPIR-V in the .spv.bin ones, ESSL text in the others).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src/libs/renderer_bgfx/cmake'))
import spirv_blob  # noqa: E402

OP_TYPE_INT, OP_TYPE_VECTOR, OP_TYPE_POINTER, OP_VARIABLE = 21, 23, 32, 59
OP_DECORATE, OP_MEMBER_DECORATE = 71, 72
STORAGE_INPUT = 1
DECORATION_BUILTIN, DECORATION_INVARIANT, BUILTIN_POSITION = 11, 18, 0


def input_types(words):
    """{input variable name: (component type, signed)} of a SPIR-V module."""
    ints, vectors, pointers, inputs = {}, {}, {}, {}
    for op, args in spirv_blob.instructions(words):
        if op == OP_TYPE_INT:
            ints[args[0]] = (args[1], args[2])
        elif op == OP_TYPE_VECTOR:
            vectors[args[0]] = args[1]
        elif op == OP_TYPE_POINTER:
            pointers[args[0]] = (args[1], args[2])
        elif op == OP_VARIABLE and args[2] == STORAGE_INPUT:
            inputs[args[1]] = args[0]
    names = spirv_blob.names(words)
    result = {}
    for variable, pointer in inputs.items():
        _, pointee = pointers[pointer]
        component = vectors.get(pointee, pointee)
        result[names.get(variable, f'%{variable}')] = ints.get(component)
    return result


def position_invariant(words):
    """Whether the module's BuiltIn Position (a variable or a block member) is decorated Invariant; None: no position."""
    decorations = {}
    for op, args in spirv_blob.instructions(words):
        if op == OP_DECORATE:
            decorations.setdefault((args[0],), []).append(tuple(args[1:]))
        elif op == OP_MEMBER_DECORATE:
            decorations.setdefault((args[0], args[1]), []).append(tuple(args[2:]))
    for decorated in decorations.values():
        if (DECORATION_BUILTIN, BUILTIN_POSITION) in decorated:
            return (DECORATION_INVARIANT,) in decorated
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(f'usage: {sys.argv[0]} <build>/renderer_bgfx/shaders-gen/storm_shaders')
    shaders = sorted(Path(sys.argv[1]).glob('vs_*.spv.bin'))
    if not shaders:
        sys.exit(f'FAIL: no compiled vertex shaders (vs_*.spv.bin) in {sys.argv[1]}')
    failures, checked = [], 0
    for path in shaders:
        words = spirv_blob.module(path.read_bytes())
        if position_invariant(words) is False:
            failures.append(f'{path.name}: its position is not Invariant')
        types = input_types(words)
        if 'a_indices' not in types:
            continue
        checked += 1
        integer = types['a_indices']
        if integer is None or integer[1] != 0:
            failures.append(
                f'{path.name}: a_indices is {"signed int" if integer else "not an integer"}, the format '
                'is VK_FORMAT_R8G8B8A8_UINT (varying.def.sc: uvec4)'
            )
    for path in sorted(Path(sys.argv[1]).glob('vs_*.bin')):
        text = path.read_bytes()
        if not path.name.endswith('.spv.bin') and b'gl_Position' in text and b'invariant gl_Position;' not in text:
            failures.append(f'{path.name}: no invariant gl_Position')
    if checked == 0:
        failures.append('no vertex shader declares a_indices: the skinned shaders were not found')
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print(f'PASS: a_indices is unsigned in all {checked} skinned vertex shaders; every position is Invariant')


main()
