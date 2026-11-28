#!/usr/bin/env python3
"""The pinned downloads of the build (engine/pins.json), for the Python and shell tools; CMake reads the same file
(storm_pin and storm_fetch_pin in cmake/StormBuildCache.cmake).

Each pin has a version and either an archive (url, sha256) or a Git commit (git, commit). "{version}" and
"{commit}" in a url stand for the pin's own fields.

    pins.py get NAME FIELD      print a field of a pin (url expanded)
    pins.py fetch NAME DIR      download the pin's archive into DIR once, check its SHA-256, print its path
    pins.py ndk [WORK]          print the pinned NDK's directory in a build workspace (see ndk())
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import urllib.request

ENGINE = Path(__file__).resolve().parents[1]
PINS = json.loads((ENGINE / 'pins.json').read_text())


def pin(name):
    """The pin's fields, its url expanded."""
    if name not in PINS:
        sys.exit(f'pins: no pin {name!r} in {ENGINE / "pins.json"}')
    fields = dict(PINS[name])
    if 'url' in fields:
        fields['url'] = fields['url'].replace('{version}', fields['version']).replace('{commit}', fields.get('commit', ''))
    return fields


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as data:
        for block in iter(lambda: data.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def fetch(name, directory):
    """The pin's archive in directory (downloaded once, under its file name), checked against its SHA-256."""
    fields = pin(name)
    directory = Path(directory)
    archive = directory / fields['url'].rsplit('/', 1)[-1]
    if not archive.is_file():
        directory.mkdir(parents=True, exist_ok=True)
        partial = archive.with_name(f'.{archive.name}.partial-{os.getpid()}')
        print(f'pins: fetching {fields["url"]}', file=sys.stderr, flush=True)
        try:
            with urllib.request.urlopen(fields['url'], timeout=600) as response, open(partial, 'wb') as out:
                shutil.copyfileobj(response, out, 1 << 20)
        except OSError as error:
            partial.unlink(missing_ok=True)
            sys.exit(f'pins: cannot fetch {fields["url"]}: {error}')
        partial.replace(archive)
    found = sha256(archive)
    if found != fields['sha256']:
        sys.exit(f'pins: {archive} has SHA-256 {found}, not the pinned {fields["sha256"]}: delete it and fetch again')
    return archive


def json_headers(work=None):
    """The include directory of the pinned nlohmann/json, unpacked once into <work>/tools (work as for ndk()): for a
    tool compiled outside CMake that includes the engine's headers."""
    import tarfile
    work = Path(work or os.environ.get('STORM_CI_WORK') or ENGINE / 'build/android-ci')
    root = work / 'tools' / f'nlohmann-json-{pin("nlohmann-json")["version"]}'
    if not (root / 'json/include/nlohmann/json.hpp').is_file():
        archive = fetch('nlohmann-json', work / 'downloads')
        partial = root.with_name(root.name + f'.partial-{os.getpid()}')
        with tarfile.open(archive) as bundle:
            bundle.extractall(partial, filter='data')
        shutil.rmtree(root, ignore_errors=True)
        partial.replace(root)
    return root / 'json/include'


def ndk(work=None):
    """The pinned NDK in a build workspace: <work>/tools/ndk/android-ndk-<version>, where
    tools/android/ci_dependencies.sh unpacks it. Without work: $STORM_ANDROID_NDK, else the workspace $STORM_CI_WORK
    or engine/build/android-ci."""
    if work is None and os.environ.get('STORM_ANDROID_NDK'):
        return Path(os.environ['STORM_ANDROID_NDK'])
    work = Path(work or os.environ.get('STORM_CI_WORK') or ENGINE / 'build/android-ci')
    return work / 'tools/ndk' / f'android-ndk-{pin("ndk")["version"]}'


def llvm_tool(name, ndk_dir=None):
    """An LLVM tool of the pinned NDK (llvm-readelf, llvm-symbolizer, ...); an error when that NDK is not there."""
    tool = Path(ndk_dir or ndk()) / 'toolchains/llvm/prebuilt/linux-x86_64/bin' / name
    if not tool.is_file():
        sys.exit(f'pins: no {tool}: unpack the pinned NDK (tools/android/ci_dependencies.sh) or set STORM_ANDROID_NDK')
    return tool


if __name__ == '__main__':
    command, *rest = sys.argv[1:] or ['']
    if command == 'get' and len(rest) == 2:
        value = pin(rest[0]).get(rest[1])
        if value is None:
            sys.exit(f'pins: {rest[0]} has no {rest[1]}')
        print(value)
    elif command == 'fetch' and len(rest) == 2:
        print(fetch(*rest))
    elif command == 'ndk' and len(rest) <= 1:
        print(ndk(*rest))
    else:
        sys.exit(__doc__)
