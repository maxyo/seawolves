#!/usr/bin/env python3
"""Build and run the math contracts (GLM and the storm::math helpers); --bench adds the micro-benchmark."""

import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--aligned-defaults', action='store_true')
parser.add_argument(
    '--cxx-flags', default='', help='Extra compiler flags for the harnesses (e.g. -fno-strict-aliasing)'
)
parser.add_argument(
    '--bench', action='store_true', help='Also run the micro-benchmark (never combined with --sanitize)'
)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
build = (args.build_dir or root / 'build/glm-tests').resolve()
build.mkdir(parents=True, exist_ok=True)

HARNESSES = ('check', 'builders_check', 'helpers_check')


def run_bench(target):
    rows = {}
    output = subprocess.check_output([str(target / 'bench')], text=True)
    for line in output.splitlines():
        fields = line.split()
        position = fields.index('ns/op')
        rows[' '.join(fields[: position - 1])] = (float(fields[position - 1]), fields[-1])
    return rows


def run_variant(name):
    target = build / name
    command = [
        'cmake',
        '-S',
        str(Path(__file__).parent),
        '-B',
        str(target),
        '-DCMAKE_BUILD_TYPE=Release',
        '-DSTORM_MATH_SANITIZE=' + ('ON' if args.sanitize else 'OFF'),
    ]
    if args.glm_source:
        command.append('-DFETCHCONTENT_SOURCE_DIR_STORM_GLM=' + str(args.glm_source.resolve()))
    flags = args.cxx_flags.split()
    if args.aligned_defaults:
        flags += ['-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES', '-DGLM_FORCE_INTRINSICS']
    # Always passed, so a later run without extra flags resets the cached value.
    command.append('-DCMAKE_CXX_FLAGS=' + ' '.join(flags))
    subprocess.run(command, check=True)
    subprocess.run(['cmake', '--build', str(target), '--parallel', '2'], check=True)
    subprocess.run([str(target / 'packed_vector_include')], check=True)
    for executable in HARNESSES:
        subprocess.run([str(target / executable)], check=True)


run_variant('current')
if args.bench and not args.sanitize:
    for name, (value, checksum) in run_bench(build / 'current').items():
        print(f'{name:30s} {value:10.3f} ns/op  {checksum}')
