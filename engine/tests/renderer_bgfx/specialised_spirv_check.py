#!/usr/bin/env python3
"""The specialised fixed-function programs (shaders/ff_programs.cmake) as built, without a GPU.

- None reads the stage uniform. A specialised program has its setup's rows as constants (ff_stages.sh FF_SETUP); a row
  left as a uniform would draw the same pixels, so only the blobs show it: neither the SPIR-V module nor the ESSL text
  names u_ffStage, while the general fs_ff does.
- Each takes the inputs of a general program of its entry (the hash bgfx links a vertex shader by), in both blobs: the
  draws that take it keep the vertex shaders, and so the positions, of the general program.
Reads the build's blobs and the list the table generated (a specialised program and a general one of its entry).
"""

import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src/libs/renderer_bgfx/cmake'))
import spirv_blob  # noqa: E402

OP_MEMBER_NAME = 6
UNIFORM = 'u_ffStage'


def names_uniform(stem, shaders):
    """Whether the SPIR-V module and the ESSL text of a program name the stage uniform."""
    words = spirv_blob.module((shaders / f'{stem}.spv.bin').read_bytes())
    names = set(spirv_blob.names(words).values())
    names |= {spirv_blob.string(args[2:]) for op, args in spirv_blob.instructions(words) if op == OP_MEMBER_NAME}
    essl = (shaders / f'{stem}.bin').read_bytes()
    return UNIFORM in names, re.search(rb'\b' + UNIFORM.encode() + rb'\b', essl) is not None


def input_hashes(stem, shaders):
    """The input hash of a fragment blob (bgfx's header: magic 'FSH', then the hash), ESSL and SPIR-V."""
    hashes = []
    for suffix in ('.bin', '.spv.bin'):
        blob = (shaders / f'{stem}{suffix}').read_bytes()
        if blob[:3] != b'FSH':
            sys.exit(f'FAIL: {stem}{suffix} is no fragment shader blob')
        hashes.append(struct.unpack_from('<I', blob, 4)[0])
    return hashes


def main():
    if len(sys.argv) != 3:
        sys.exit(f'usage: {sys.argv[0]} <shaders-gen/storm_shaders> <generated/shaders/ff_specialised.txt>')
    shaders = Path(sys.argv[1])
    programs = [line.split() for line in Path(sys.argv[2]).read_text().splitlines() if line]
    if not programs:
        sys.exit('FAIL: the table lists no specialised program')
    if names_uniform('fs_ff', shaders) != (True, True):
        sys.exit(f'FAIL: the general fs_ff does not name {UNIFORM}: this check reads the blobs wrongly')
    failures = []
    for program, general in programs:
        for stem in (program, f'{program}_opaque'):
            spirv, essl = names_uniform(stem, shaders)
            failures += [f'{stem}: the {kind} blob names {UNIFORM}' for kind, found in (('SPIR-V', spirv), ('ESSL', essl))
                         if found]
            if input_hashes(stem, shaders) != input_hashes(general, shaders):
                failures.append(f'{stem}: its inputs are not those of {general}, a general program of its entry')
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print(f'PASS: no specialised program reads the stage uniform, each takes its entry\'s inputs '
          f'({2 * len(programs)} checked)')


main()
