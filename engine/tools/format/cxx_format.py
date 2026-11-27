#!/usr/bin/env python3
"""Format the engine's own C/C++ sources with .clang-format, or check that they are formatted.

    python3 tools/format/cxx_format.py           # rewrite the files in place
    python3 tools/format/cxx_format.py --check   # list the files that would change; exit 1 if any
    python3 tools/format/cxx_format.py --fetch   # first get the NDK's clang-format if none is found

The files: every C/C++ source and header git tracks under engine/ (*.c, *.cc, *.cpp, *.h, *.hpp, *.inl), except
- the Storm Engine files this engine started from (upstream-files.txt): they keep their own layout and history;
- Storm scripts, not C: the scene sweep's (tools/scene_sweep/*.c, scenarios/*.c) and the script language cases
  (tests/script_language/cases/*.c, *.h) and the game of the interface scripts' check (tests/ui_scripts/game).

The formatter is the clang-format of the Android NDK that builds the engine (pin "ndk" in engine/pins.json), found
where the build scripts unpack that NDK: $STORM_CI_WORK (default build/android-ci)/tools/ndk/<ndk>. Another
clang-format version lays code out differently, so no other one is used: not an NDK that $ANDROID_NDK_HOME or
$STORM_ANDROID_NDK names (GitHub's runners set ANDROID_NDK_HOME to an NDK of their own).
--fetch, when none of these has it, downloads the pinned NDK archive into $STORM_CI_WORK/downloads (checked against
its SHA-256) and unpacks only clang-format into $STORM_CI_WORK/tools/clang-format-<ndk>.

Without a clang-format --check exits with code 77 after saying so: the host check runner reports that as NOT_RUN,
never as a pass. The check is tests/cxx_format.
"""
import argparse
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import zipfile

ENGINE = Path(__file__).resolve().parents[2]
UPSTREAM = Path(__file__).with_name('upstream-files.txt')
SUFFIXES = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inl')
GAME_SCRIPTS = re.compile(r'^(?:tools/scene_sweep/(?:[^/]+|scenarios/[^/]+)\.c|tests/script_language/cases/[^/]+\.[ch]|tests/ui_scripts/game/.+\.c)$')
NOT_RUN = 77
BIN = 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang-format'
sys.path.insert(0, str(ENGINE / 'tools'))
import pins  # noqa: E402


def ci_work():
    return Path(os.environ.get('STORM_CI_WORK', ENGINE / 'build/android-ci'))


def candidates():
    ndk = pins.ndk(ci_work())
    return [ndk / BIN, ci_work() / f'tools/clang-format-{ndk.name}/clang-format']


def find():
    """The NDK's clang-format, or None."""
    return next((path for path in candidates() if path.is_file() and os.access(path, os.X_OK)), None)


def fetch():
    """Downloads the pinned NDK archive (unless it is in $STORM_CI_WORK/downloads) and unpacks clang-format alone."""
    archive = pins.fetch('ndk', ci_work() / 'downloads')
    ndk = pins.ndk(ci_work()).name
    target = ci_work() / f'tools/clang-format-{ndk}/clang-format'
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        partial = target.with_suffix('.partial')
        partial.write_bytes(bundle.read(f'{ndk}/{BIN}'))
    partial.chmod(partial.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    partial.replace(target)
    return target


def tracked():
    listed = subprocess.run(['git', '-C', str(ENGINE), 'ls-files', '-z', '--', *(f'*{s}' for s in SUFFIXES)],
                            check=True, stdout=subprocess.PIPE).stdout.decode()
    return sorted(name for name in listed.split('\0') if name)


def sources():
    """The formatted files (relative to engine/); stops on a stale entry of upstream-files.txt."""
    files = tracked()
    upstream = [line for line in UPSTREAM.read_text().splitlines() if line and not line.startswith('#')]
    stale = sorted(set(upstream) - set(files))
    if stale:
        sys.exit(f'cxx_format: {UPSTREAM.relative_to(ENGINE)} lists files git does not track (remove them):\n  '
                 + '\n  '.join(stale))
    kept = set(upstream)
    return [name for name in files if name not in kept and not GAME_SCRIPTS.match(name)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='change nothing; fail when a file is not formatted')
    parser.add_argument('--fetch', action='store_true', help="get the NDK's clang-format when none is found")
    arguments = parser.parse_args()
    clang_format = find() or (fetch() if arguments.fetch else None)
    if clang_format is None:
        print('NOT_RUN cxx_format: no clang-format of the NDK; looked at\n  '
              + '\n  '.join(map(str, candidates()))
              + '\n(set STORM_CI_WORK, or run python3 tools/format/cxx_format.py --fetch)',
              flush=True)
        sys.exit(NOT_RUN)
    version = subprocess.run([str(clang_format), '--version'], check=True, capture_output=True, text=True).stdout
    files = sources()
    command = [str(clang_format), '--style=file:' + str(ENGINE / '.clang-format')]
    if not arguments.check:
        subprocess.run([*command, '-i', *files], cwd=ENGINE, check=True)
        print(f'cxx_format: {len(files)} files formatted ({version.strip()})')
        return
    result = subprocess.run([*command, '--dry-run', '-Werror', *files], cwd=ENGINE, capture_output=True, text=True)
    unformatted = sorted(set(re.findall(r'^(\S+?):\d+:\d+: error:', result.stderr, re.M)))
    if result.returncode or unformatted:
        if not unformatted:
            sys.exit(f'cxx_format: clang-format failed:\n{result.stderr[-4000:]}')
        sys.exit('cxx_format: not formatted (run python3 engine/tools/format/cxx_format.py):\n  '
                 + '\n  '.join(f'engine/{name}' for name in unformatted))
    print(f'PASS cxx_format: {len(files)} files are formatted ({version.strip()})')


if __name__ == '__main__':
    main()
