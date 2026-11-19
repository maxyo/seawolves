#!/usr/bin/env python3
"""Resolve the native crash backtrace of a diagnostics export («Поделиться отчётом») to functions and source lines.

    tools/android/symbolize.py EXPORT.txt [--symbols PATH ...] [--llvm-symbolizer PATH]

The export's exit history carries each native crash's backtrace from the platform's tombstone (Tombstone.java):
frames "#NN pc <relative pc>  <library> ... (BuildId: <id>)". This finds the unstripped library of each build id
and runs llvm-symbolizer on the frame's relative pc; every frame line is printed with the function, file and line
(inlined calls first) under it. A frame whose build id has no symbols here is printed as it is, marked.

--symbols: build-ids.json files (written beside every APK by build_apk.py), directories (searched for build-ids.json
and *.so files, whose build ids are read from their ELF notes) or native-debug-symbols.zip files. Default: build/
of this checkout (every APK built here). Symbols stay on this machine: release APKs ship stripped libraries.
--llvm-symbolizer: default the NDK's of $STORM_CI_WORK or of engine/build/android-ci, else PATH.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
FRAME = re.compile(r'^\s*#\d+ pc ([0-9a-f]+)\s+(\S+)(?:.*\(BuildId: ([0-9a-f]+)\))?\s*$')


def build_id(path):
    """The GNU build id of an ELF file (its PT_NOTE segments), or None."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return None
    if data[:4] != b'\x7fELF' or len(data) < 64:
        return None
    wide, order = data[4] == 2, '<' if data[5] == 1 else '>'
    if wide:
        phoff, = struct.unpack_from(order + 'Q', data, 32)
        phentsize, phnum = struct.unpack_from(order + 'HH', data, 54)
    else:
        phoff, = struct.unpack_from(order + 'I', data, 28)
        phentsize, phnum = struct.unpack_from(order + 'HH', data, 42)
    for i in range(phnum):
        at = phoff + i * phentsize
        kind, = struct.unpack_from(order + 'I', data, at)
        if kind != 4:  # PT_NOTE
            continue
        offset, size = (struct.unpack_from(order + 'QQ', data, at + 8)[0], struct.unpack_from(order + 'Q', data, at + 32)[0]) if wide \
            else (struct.unpack_from(order + 'I', data, at + 4)[0], struct.unpack_from(order + 'I', data, at + 16)[0])
        end = offset + size
        while offset + 12 <= end:
            namesz, descsz, note = struct.unpack_from(order + 'III', data, offset)
            name = data[offset + 12:offset + 12 + namesz]
            desc_at = offset + 12 + (namesz + 3) // 4 * 4
            if note == 3 and name.rstrip(b'\0') == b'GNU':  # NT_GNU_BUILD_ID
                return data[desc_at:desc_at + descsz].hex()
            offset = desc_at + (descsz + 3) // 4 * 4
    return None


def collect(paths, scratch):
    """Unstripped libraries by key: their build id, or "name:<file name>" for one built without a build id
    (build-ids.json without_build_id), from index files, directories and symbol zips."""
    found = {}

    def library(path):
        identity = build_id(path)
        found.setdefault(identity or 'name:' + Path(path).name, Path(path))

    for path in paths:
        path = Path(path)
        if path.is_file() and path.name.endswith('.json'):
            index = json.loads(path.read_text())
            entries = [(k, v['symbols']) for k, v in index['libraries'].items()]
            entries += [('name:' + k, v) for k, v in index.get('without_build_id', {}).items()]
            for key, symbols in entries:
                if Path(symbols).is_file():
                    found.setdefault(key, Path(symbols))
        elif path.is_file() and path.suffix == '.zip':
            target = Path(tempfile.mkdtemp(prefix='symbols-', dir=scratch))
            with zipfile.ZipFile(path) as archive:
                archive.extractall(target)
            for so in target.rglob('*.so'):
                library(so)
        elif path.is_dir():
            for index in path.rglob('build-ids.json'):
                found.update({k: v for k, v in collect([index], scratch).items() if k not in found})
            for so in path.rglob('*.so'):
                library(so)
        else:
            sys.exit(f'symbolize: {path} is no build-ids.json, directory or symbol zip')
    return found


def symbolizer(given):
    if given:
        return given
    # The NDK of the engine's Android build: $STORM_CI_WORK, else engine/build/android-ci (build_debug.sh's default).
    for work in ([Path(os.environ['STORM_CI_WORK'])] if 'STORM_CI_WORK' in os.environ else []) + [ROOT.parent / 'engine/build/android-ci']:
        for candidate in sorted(work.glob('tools/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-symbolizer'), reverse=True):
            return str(candidate)
    found = shutil.which('llvm-symbolizer')
    if not found:
        sys.exit('symbolize: no llvm-symbolizer (pass --llvm-symbolizer, set STORM_CI_WORK to the engine build/android-ci, or install LLVM)')
    return found


def resolve(tool, library, addresses):
    """[(function, location), ...] per address, inlined calls first."""
    out = subprocess.run([tool, '--obj', str(library), '--functions=linkage', '--inlining', '--demangle', '--output-style=LLVM',
                          *[f'0x{a}' for a in addresses]], capture_output=True, text=True, check=True).stdout
    blocks = out.strip('\n').split('\n\n')
    result = []
    for text in blocks:
        lines = text.splitlines()
        result.append([(lines[i], lines[i + 1] if i + 1 < len(lines) else '??') for i in range(0, len(lines), 2)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('export', type=Path, nargs='+')
    parser.add_argument('--symbols', type=Path, nargs='*', default=[ROOT / 'build'])
    parser.add_argument('--llvm-symbolizer')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='storm-symbolize-') as scratch:
        libraries = collect(args.symbols, scratch)
        tool = symbolizer(args.llvm_symbolizer)
        lines = [line for export in args.export for line in export.read_text(errors='replace').splitlines()]
        frames = [FRAME.match(line) for line in lines]

        def key(frame):
            return frame[3] or 'name:' + frame[2].rsplit('/', 1)[-1].split('!')[-1]

        wanted = {}
        for frame in filter(None, frames):
            if key(frame) in libraries:
                wanted.setdefault(key(frame), set()).add(frame[1])
        resolved = {}
        for identity, addresses in wanted.items():
            unique = sorted(addresses)
            resolved.update({(identity, a): r for a, r in zip(unique, resolve(tool, libraries[identity], unique))})
        missing = set()
        for line, frame in zip(lines, frames):
            print(line)
            if not frame:
                continue
            if (key(frame), frame[1]) in resolved:
                for function, location in resolved[(key(frame), frame[1])]:
                    print(f'        {function} at {location}')
            elif '/data/app/' in frame[2]:  # ours; system libraries are symbolised by the device already
                missing.add((frame[2].rsplit('/', 1)[-1].split('!')[-1], frame[3] or 'none'))
                print('        NO SYMBOLS here for this library')
        for name, identity in sorted(missing):
            print(f'symbolize: no symbols for {name} (build id {identity}) under {", ".join(map(str, args.symbols))} '
                  '(the release build\'s build-ids.json, symbols/ or native-debug-symbols.zip)', file=sys.stderr)

if __name__ == '__main__':
    main()
