#!/usr/bin/env python3
"""Format the application's Java sources, or check that they are formatted.

    python3 app/tools/android/java_format.py           # rewrite the files in place
    python3 app/tools/android/java_format.py --check   # list the files that would change; exit 1 if any

One formatter with one configuration: google-java-format in its AOSP style (4-space indent, 100 columns; Javadoc
comments and long string literals are left as written), over every .java file under app/. The formatter is a single jar, pinned by version
and SHA-256 and fetched once into ~/.cache/storm-deps/tools; it needs JDK 17 or newer. The check is part of the
host checks (app/tests/java_format/run.py).
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

APP = Path(__file__).resolve().parents[2]
VERSION = '1.28.0'
SHA256 = '32342e7c1b4600f80df3471da46aee8012d3e1445d5ea1be1fb71289b07cc735'
URL = (f'https://github.com/google/google-java-format/releases/download/v{VERSION}/'
       f'google-java-format-{VERSION}-all-deps.jar')
CACHE = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'storm-deps/tools'


def pinned_jar(name, urls, sha256):
    """A jar pinned by SHA-256, downloaded into the shared tool cache on first use and checked on every use.

    `urls` are places that serve the same file; the first that answers is used."""
    jar = CACHE / name
    if not jar.is_file():
        CACHE.mkdir(parents=True, exist_ok=True)
        partial = jar.with_suffix('.partial')
        # Maven Central refuses a CI runner's address with HTTP 429 for longer than a run can wait: the next source.
        for url in urls:
            try:
                with urllib.request.urlopen(url, timeout=120) as response:
                    partial.write_bytes(response.read())
                break
            except OSError as error:
                print(f'cannot fetch {url}: {error}', file=sys.stderr)
        else:
            sys.exit(f'cannot fetch {name} from any of its sources')
        partial.replace(jar)
    digest = hashlib.sha256(jar.read_bytes()).hexdigest()
    if digest != sha256:
        sys.exit(f'{jar} has SHA-256 {digest}, expected {sha256}: delete it and run again')
    return jar


def formatter():
    return pinned_jar(f'google-java-format-{VERSION}-all-deps.jar', (URL,), SHA256)


def sources():
    listed = subprocess.run(['git', '-C', str(APP), 'ls-files', '-z', '--cached', '--others', '--exclude-standard',
                             '--', '*.java'], check=True, stdout=subprocess.PIPE).stdout.decode()
    return sorted(str(APP / name) for name in listed.split('\0') if name and (APP / name).is_file())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='change nothing; fail when a file is not formatted')
    arguments = parser.parse_args()
    files = sources()
    # Javadoc keeps its hand-made layout: the formatter would reflow plain-text lists into one paragraph. A long
    # message stays one literal that can be searched for, not pieces joined with +.
    command = ['java', '-jar', str(formatter()), '--aosp', '--skip-javadoc-formatting', '--skip-reflowing-long-strings']
    if not arguments.check:
        # A reflowed comment can need a second pass: repeat until the formatter has nothing left to change.
        for _ in range(4):
            subprocess.run([*command, '--replace', *files], check=True)
            if not subprocess.run([*command, '--dry-run', *files], check=True, stdout=subprocess.PIPE).stdout:
                print(f'java_format: {len(files)} files formatted')
                return
        sys.exit('java_format: the formatter keeps changing its own output')
    result = subprocess.run([*command, '--dry-run', *files], stdout=subprocess.PIPE, text=True)
    unformatted = result.stdout.split()
    if result.returncode or unformatted:
        sys.exit('java_format: not formatted (run python3 app/tools/android/java_format.py):\n  '
                 + '\n  '.join(os.path.relpath(name, APP.parent) for name in unformatted))
    print(f'PASS java_format: {len(files)} files are formatted')


if __name__ == '__main__':
    main()
