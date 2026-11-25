#!/usr/bin/env python3
"""Isolated allocation-failure sweep for the private C archive allocator.

Covers source discovery (including a nested container written to scratch), extraction of the
root found, and cancellation.

Build the host reader with -DSTORM_ARCHIVE_TESTING=ON. Every operation/allocation
position runs in a subprocess; a signal, exit(), hang or leaked quota fails the
suite. Single and persistent failures are repeated within each child, followed
by a successful operation after disarming fault injection. No game data needed.
"""

import argparse
import ctypes as C
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

import reader


def load(path):
    lib = reader.load(path)
    lib.storm_archive_test_fault.argtypes = [C.c_size_t, C.c_int, C.c_size_t]
    lib.storm_archive_test_used.restype = C.c_size_t
    lib.storm_archive_test_attempts.restype = C.c_size_t
    lib.storm_archive_test_fault(0, 0, 0)  # Initialize loader/TLS before arming.
    return lib


def child(args):
    lib = load(args.library)
    initial = lib.storm_archive_test_used()

    def operation():
        # A source set holding the input; nested containers go to scratch.
        with (
            tempfile.TemporaryDirectory(prefix='storm-oom-source-') as source,
            tempfile.TemporaryDirectory(prefix='storm-oom-scratch-') as scratch,
            tempfile.TemporaryDirectory(prefix='storm-oom-output-') as out,
        ):
            shutil.copy(args.input, Path(source) / args.input.name)
            if args.operation == 'discover':
                code, document, error = reader.discover(lib, source, scratch)
                if code == 0 and not document['candidates']:
                    code = -1
                return code, error
            exe = args.input.suffix.lower() == '.exe'
            cancel = lambda user, stage, path, done, total, files: int(done > 1024 * 1024) if exe else 1
            return reader.install(lib, source, out, progress=cancel if args.operation == 'cancel' else None)

    outcomes = []
    for _ in range(args.repeat):
        lib.storm_archive_test_fault(args.position, args.persistent, args.quota)
        code, error = operation()
        attempts = lib.storm_archive_test_attempts()
        lib.storm_archive_test_fault(0, 0, 0)
        after = lib.storm_archive_test_used()
        assert after == initial, (args.operation, args.position, 'quota leak', initial, after, code, error)
        expected = (
            12  # STORM_ARCHIVE_MEMORY
            if args.position and attempts >= args.position or args.quota and attempts
            else 7
            if args.operation == 'cancel'
            else 0
        )
        assert code == expected, (args.operation, args.position, 'status', code, expected, error)
        outcomes.append({'status': code, 'allocations': attempts})
    # Same process and same library must remain usable after every error.
    code, error = operation()
    assert code == (7 if args.operation == 'cancel' else 0), ('recovery', code, error)
    assert lib.storm_archive_test_used() == initial, ('recovery quota leak', lib.storm_archive_test_used())
    print(json.dumps({'outcomes': outcomes, 'live_bytes': initial}))


def fixtures(root):
    files = {
        'app/RESOURCE/Корабль.txt': b'fixture ' * 4096,
        'app/PROGRAM/game.c': b'void game() {}',
        'app/engine.ini': b'[engine]',
    }
    path = root / 'game.zip'
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    result = [path]
    path = root / 'game.tar.gz'
    with tarfile.open(path, 'w:gz') as archive:
        for name, data in files.items():
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))
    result.append(path)
    if shutil.which('7z'):
        source = root / 'source'
        for name, data in files.items():
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        path = root / 'game.7z'
        subprocess.run(['7z', 'a', str(path), 'app'], cwd=source, stdout=subprocess.DEVNULL, check=True)
        result.append(path)
    # A game ZIP inside a ZIP: discovery writes the inner archive to scratch.
    path = root / 'nested.zip'
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('inner.zip', (root / 'game.zip').read_bytes())
    result.append(path)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library', required=True, type=Path)
    p.add_argument('--report', type=Path)
    p.add_argument('--inno-installer', type=Path)
    p.add_argument('--inno-only', action='store_true')
    p.add_argument('--child', action='store_true')
    p.add_argument('--input', type=Path)
    p.add_argument('--operation', choices=['discover', 'extract', 'cancel'])
    p.add_argument('--position', type=int, default=0)
    p.add_argument('--persistent', type=int, default=0)
    p.add_argument('--quota', type=int, default=0)
    p.add_argument('--repeat', type=int, default=3)
    args = p.parse_args()
    if args.child:
        return child(args)
    results = []

    def run(path, op, position=0, persistent=0, quota=0):
        cmd = [
            sys.executable,
            __file__,
            '--child',
            '--library',
            str(args.library.resolve()),
            '--input',
            str(path),
            '--operation',
            op,
            '--position',
            str(position),
            '--persistent',
            str(persistent),
            '--quota',
            str(quota),
            '--repeat',
            str(args.repeat),
        ]
        result = subprocess.run(cmd, text=True, capture_output=True, timeout=15)
        if result.returncode:
            raise RuntimeError(
                f'{path.name} {op} allocation={position} persistent={persistent} quota={quota}: process exit {result.returncode}\n{result.stdout}\n{result.stderr}'
            )
        data = json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"outcomes":')))
        results.append(
            {
                'format': path.suffix,
                'operation': op,
                'position': position,
                'persistent': bool(persistent),
                'quota': quota,
                **data,
            }
        )
        return data

    with tempfile.TemporaryDirectory(prefix='storm-oom-fixtures-') as directory:
        inputs = [] if args.inno_only else fixtures(Path(directory))
        if args.inno_installer:
            inputs.append(args.inno_installer.resolve())
        for path in inputs:
            operations = (
                ['discover', 'cancel']
                if path.suffix.lower() == '.exe'
                else ['discover']
                if path.name == 'nested.zip'
                else ['discover', 'extract', 'cancel']
            )
            for op in operations:
                baseline = run(path, op)
                count = baseline['outcomes'][0]['allocations']
                for persistent in [0, 1]:
                    for position in range(1, count + 1):
                        run(path, op, position, persistent)
                for quota in [1, 256, 2048]:
                    run(path, op, quota=quota)
                print(
                    f'{path.name} {op}: {count} allocation positions, single+persistent failures, quota and recovery passed',
                    flush=True,
                )
    report = {'subprocess_cases': len(results), 'repetitions_per_case': args.repeat, 'results': results}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2))
    print(f'Passed {len(results)} isolated subprocess cases; no crashes, exits, or quota leaks')


if __name__ == '__main__':
    main()
