#!/usr/bin/env python3
"""Build and run the engine's host checks (the CTest project tests/CMakeLists.txt; no NDK, SDK, device or game data).

  python3 tools/ci/run_host_tests.py [--work build/host-ci] [--jobs N] [--only NAME ...] [--skip NAME ...]
                                     [--changed-since REF]

Configures <work> once, builds it (cmake --build), runs ctest and prints one line per check: PASS, FAIL (with its
output) or NOT_RUN (exit 77: this machine cannot run it; never a pass), and a summary. A NAME is a check (a ctest
name such as utf8 or renderer_bgfx.skinning_check.vulkan) or a group: the part of the names before the first '.'
(input_std, renderer_bgfx: every renderer check). --changed-since REF runs only the checks the changes since REF can
affect (tools/ci/affected.py) and names each one it leaves out. The checks that draw need a display
(xvfb-run -a -s -noreset when headless: README.md, Display).
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree

root = Path(__file__).resolve().parents[2]


def group(test):
    """The group a check belongs to: renderer_bgfx for the renderer's (also the tests its own CMake files declare),
    else its name up to the first '.'."""
    return 'renderer_bgfx' if 'renderer' in test['labels'] else test['name'].split('.', 1)[0]


def tests(work):
    """Every check of the configured project: [{name, labels, inputs, command}] (ctest --show-only=json-v1)."""
    listing = subprocess.run(
        ['ctest', '--test-dir', str(work), '--show-only=json-v1'], capture_output=True, text=True, check=True
    )
    result = []
    for test in json.loads(listing.stdout)['tests']:
        properties = {p['name']: p['value'] for p in test.get('properties', [])}
        labels = properties.get('LABELS', [])
        if 'setup' in labels:
            continue  # the fixture that empties the temporary directories, not a check
        inputs = properties.get('STORM_INPUTS', '')
        result.append(
            {
                'name': test['name'],
                'labels': labels,
                'inputs': [i for i in (inputs if isinstance(inputs, list) else inputs.split(';')) if i],
                'command': test.get('command', []),
            }
        )
    return result


def configure(work, renderer):
    """Configures <work> unless it is, with the renderer's checks or without them (the build re-runs CMake itself
    when its inputs change). A build configured with another compiler than $CXX is refused: CMake keeps the compiler
    of the first configure and would ignore $CXX."""
    cache = work / 'CMakeCache.txt'
    compiler = os.environ.get('CXX')
    configured = re.search(r'^CMAKE_CXX_COMPILER:\w+=(.+)$', cache.read_text(), re.M) if cache.exists() else None
    if compiler and configured and Path(configured[1]).resolve() != Path(shutil.which(compiler) or compiler).resolve():
        sys.exit(f'{work} is configured with {configured[1]}, not CXX={compiler}: give another --work')
    wanted = f'STORM_CHECK_RENDERER:BOOL={"ON" if renderer else "OFF"}'
    if not cache.exists() or wanted not in cache.read_text():
        subprocess.run(
            ['cmake', '-S', str(root / 'tests'), '-B', str(work), '-G', 'Ninja', '-D' + wanted], check=True
        )


def select(all_tests, only, skip):
    # renderer_bgfx is a group also when it is left out of the build (--skip renderer_bgfx).
    names = {t['name'] for t in all_tests} | {group(t) for t in all_tests} | {'renderer_bgfx'}
    for option, given in (('--only', only or []), ('--skip', skip)):
        unknown = sorted(set(given) - names)
        if unknown:
            sys.exit(f'{option} names no such check or group: ' + ', '.join(unknown))
    chosen = [t for t in all_tests if only is None or t['name'] in only or group(t) in only]
    for name in skip:
        print(f'SKIP {name}: --skip (run elsewhere)', flush=True)
    return [t for t in chosen if t['name'] not in skip and group(t) not in skip]


def results(junit):
    """{name: (status, seconds, output)} from ctest's JUnit file; status PASS, FAIL or NOT_RUN. A test ctest could not
    start (its program was not built) is a FAIL: only exit 77 is NOT_RUN."""
    found = {}
    for case in ElementTree.parse(junit).getroot().iter('testcase'):
        output = case.findtext('system-out') or ''
        skipped = case.find('skipped')
        if case.get('status') == 'run':
            status = 'PASS'
        elif skipped is not None and 'SKIP_RETURN_CODE' in (skipped.get('message') or ''):
            status = 'NOT_RUN'
        else:
            status = 'FAIL'
        found[case.get('name')] = (status, float(case.get('time') or 0), output)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', type=Path, default=root / 'build/host-ci')
    parser.add_argument('--only', nargs='*', help='run only these checks or groups')
    parser.add_argument('--skip', nargs='*', default=[], help='leave out these checks or groups (run elsewhere)')
    parser.add_argument('--jobs', type=int, default=os.cpu_count(), help='checks run in parallel')
    parser.add_argument('--changed-since', metavar='REF', help='only the checks the changes since REF affect')
    args = parser.parse_args()
    work = args.work.resolve()
    # Left out with --skip renderer_bgfx: then this machine needs neither bgfx's build nor a display for it.
    configure(work, 'renderer_bgfx' not in args.skip)
    # The build re-runs the configure step when the project changed, so the list of checks is read after it.
    started = time.monotonic()
    build = subprocess.run(
        ['cmake', '--build', str(work), '--', '-k', '0'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    built = build.returncode == 0
    print(f'{"PASS" if built else "FAIL"} build ({time.monotonic() - started:.1f}s)', flush=True)
    if not built:
        lines = build.stdout.splitlines()
        print('\n'.join(line for line in lines if line.startswith('FAILED') or ': error' in line)[-20000:], flush=True)
    all_tests = tests(work)
    chosen = select(all_tests, args.only, args.skip)
    if args.changed_since:
        sys.path.insert(0, str(root / 'tools/ci'))
        import affected

        plan = affected.host_checks(affected.changed_files(args.changed_since), work)
        for test in chosen:
            if not plan[test['name']][0]:
                print(f'SKIP {test["name"]}: {plan[test["name"]][1]}', flush=True)
        chosen = [t for t in chosen if plan[t['name']][0]]
    failed, not_run = ([] if built else ['build']), []
    if chosen:
        junit = work / 'host-checks.xml'
        junit.unlink(missing_ok=True)
        pattern = '^(' + '|'.join(re.escape(t['name']) for t in chosen) + ')$'
        subprocess.run(
            [
                'ctest',
                '--test-dir',
                str(work),
                '-j',
                str(max(1, args.jobs)),
                '-R',
                pattern,
                '--no-tests=error',
                '--output-junit',
                str(junit),
                '--test-output-size-passed',
                '262144',
                '--test-output-size-failed',
                '1048576',
                '--test-output-truncation',
                'head',
            ],
            check=False,
        )
        found = results(junit) if junit.exists() else {}
        for test in chosen:
            name = test['name']
            status, seconds, output = found.get(name, ('FAIL', 0.0, 'ctest reported nothing for it\n'))
            print(f'{status} {name} ({seconds:.1f}s)', flush=True)
            if status == 'NOT_RUN':
                print(output[-4000:], flush=True)
                not_run.append(name)
            elif status == 'FAIL':
                print(output[-20000:], flush=True)
                failed.append(name)
            else:
                # What a passing check left out (its "SKIP: why" lines) is shown with it: a pass is not a full pass then.
                for line in output.splitlines():
                    if line.startswith('SKIP'):
                        print(f'  NOT RUN in {name}: {line}', flush=True)
    unrun = f'; NOT_RUN (not passes): {", ".join(sorted(not_run))}' if not_run else ''
    if failed:
        sys.exit(f'{len(failed)} of {len(chosen)} host checks failed: ' + ', '.join(failed) + unrun)
    if not_run:
        print(f'{len(chosen) - len(not_run)} of {len(chosen)} host checks passed{unrun}')
    else:
        print(f'All {len(chosen)} host checks passed')


if __name__ == '__main__':
    main()
