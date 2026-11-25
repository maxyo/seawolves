#!/usr/bin/env python3
"""Run every host-side check of the application (no NDK or device needed).

    python3 app/tools/android/run_host_tests.py [--local]

Runs every Java check of tests/java_check.py (reported as tests/<name>), every tests/*/run.py and
tools/android/tests/*/run.py, the project boundary and the launcher icon check. CI runs this on pull requests, on main and before a release is packaged.

Three checks need more than a CI runner has and run only on a developer machine:

- tools/android/tests/pirate_spawn/run.py: the shipped pirate-frequency patch on the unchanged game scripts
  (reads the game, copies only the patch targets);
- tests/attribute_highlight/run.py: real pointer taps on the hero screen of this application's pack, captured
  by the engine's storm_ui_shot (software GLES, no GPU), built incrementally in build/ui-shot;
- tests/hud_fleet_rings/run.py: the HUD fleet column on the world map and at sea, every ship picture centred in
  its rings (same storm_ui_shot and pack).

They run when $STORM_GAME_DIR names your copy of the game (the directory with PROGRAM/ and RESOURCE/), or with
--local, which makes a missing input a failure instead of NOT_RUN. The platform icon check needs the SDK platform,
build tools and SDL sources of the engine's Android build workspace ($STORM_CI_WORK, else
engine/build/android-ci) and is NOT_RUN without them.

NOT_RUN is never counted as a pass. Every failure is reported; the exit code is non-zero if any check failed.
"""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--local', action='store_true',
                    help='a developer machine: the checks that need the game and storm_ui_shot must run')
args = parser.parse_args()
# The developer-machine checks: run below with their own arguments where their inputs are found.
# The checks captured by storm_ui_shot on this application's pack (one storm_ui_shot build and one pack for all).
shot_checks = ('tests/attribute_highlight/run.py', 'tests/hud_fleet_rings/run.py')
local_checks = (*shot_checks, 'tools/android/tests/pirate_spawn/run.py')
not_run = {}  # label -> what it requires: reported NOT_RUN
discovered = sorted([*root.glob('tests/*/run.py'), *root.glob('tools/android/tests/*/run.py')])
# The platform icon resource/Java smoke check and the share provider's path check need the SDK (and SDL sources) of
# the engine's Android build workspace ($STORM_CI_WORK, else engine/build/android-ci: build_debug.sh's default).
android_work = Path(os.environ.get('STORM_CI_WORK', root.parent / 'engine/build/android-ci'))
android_jar = android_work / 'tools/platform-36/android-36/android.jar'
share_check = 'tests/android_save_export/run.py'
checks = [(name, [sys.executable, str(path), *(['--android-jar', str(android_jar)]
                                                if name == share_check and android_jar.is_file() else [])])
          for path in discovered if (name := path.relative_to(root).as_posix()) not in local_checks]
if not android_jar.is_file():
    not_run[share_check + ' (ShareSheet provider paths)'] = f'the SDK platform in {android_work} (prepare_sdk.sh)'
platform_icons = {'--android-jar': android_jar,
                  '--aapt2': android_work / 'tools/buildtools-36/android-16/aapt2',
                  '--sdl-source': android_work / 'sources/SDL'}
if all(path.exists() for path in platform_icons.values()):
    checks.append(('tools/android/tests/platform_icons.py',
                   [sys.executable, str(root / 'tools/android/tests/platform_icons.py'),
                    *[str(part) for option in platform_icons.items() for part in option]]))
else:
    not_run['tools/android/tests/platform_icons.py'] = (
        f'the Android build workspace {android_work} (prepare_sdk.sh platform/build-tools 36, SDL sources); '
        'set STORM_CI_WORK or build engine/build/android-ci')
sys.path.insert(0, str(root / 'tests'))
from java_check import CHECKS as java_checks  # noqa: E402

checks += [(f'tests/{name}', [sys.executable, str(root / 'tests/java_check.py'), name]) for name in java_checks]
checks += [
    ('tools/android/check_project_boundary.py', [sys.executable, str(root / 'tools/android/check_project_boundary.py')]),
    ('tools/android/app_icon.py', [sys.executable, str(root / 'tools/android/app_icon.py'), '--check']),
]
failed = []
executed = 0


def run(command, label):
    global executed
    executed += 1
    started = time.monotonic()
    result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    status = 'PASS' if result.returncode == 0 else 'FAIL'
    print(f'{status} {label} ({time.monotonic() - started:.1f}s)', flush=True)
    if result.returncode:
        print(result.stdout, flush=True)
        failed.append(label)
    return result.returncode == 0


def shot_checks_run(work, labels):
    """storm_ui_shot (incremental in build/ui-shot, the engine's ccache launcher), a pack built into `work`, the checks."""
    build = root / 'build/ui-shot'
    steps = [] if (build / 'CMakeCache.txt').is_file() else [
        (['cmake', '-S', str(engine_source / 'src/libs/ui'), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release', '-Wno-dev',
          *(['-G', 'Ninja'] if shutil.which('ninja') else [])], 'configure build/ui-shot')]
    steps += [(['cmake', '--build', str(build), '--target', 'storm_ui_shot', '--parallel'], 'build storm_ui_shot'),
              ([sys.executable, str(root / 'mobile-ui/design/build.py'), '--pack', str(work / 'pack'),
                '--engine-root', str(engine_source)], 'build the UI pack')]
    for command, what in steps:
        if not run(command, f'storm_ui_shot checks: {what}'):
            return
    for label in labels:
        run([sys.executable, str(root / label), '--pack', str(work / 'pack'), '--shot', str(build / 'storm_ui_shot'),
             '--engine-source', str(engine_source), '--work-dir', str(work / Path(label).parent.name)], label)


game = Path(os.environ['STORM_GAME_DIR']) if os.environ.get('STORM_GAME_DIR') else None
engine_source = root.parent / 'engine'
cxx = os.environ.get('CXX', 'c++')
game_input = ('your copy of the game in $STORM_GAME_DIR (the directory with PROGRAM/)',
              game is not None and (game / 'PROGRAM').is_dir())
local_inputs = {'tools/android/tests/pirate_spawn/run.py': [game_input, (f'a C++ compiler ({cxx}, set CXX)', shutil.which(cxx) is not None)],
                **{label: [] for label in shot_checks}}
# A game copy marks a developer machine: with it the developer-machine checks are demanded as with --local.
local_demanded = args.local or game_input[1]
local_runs = set()
for label, inputs in sorted(local_inputs.items()):
    missing_inputs = '; '.join(name for name, found in inputs if not found)
    if not local_demanded:
        not_run[label] = 'a developer machine (STORM_GAME_DIR set, or --local)' + ''.join(
            f'; {name}' for name, _ in inputs)
    elif missing_inputs:
        print(f'FAIL {label}: requires {missing_inputs}', flush=True)
        failed.append(label)
    else:
        local_runs.add(label)
for label, requirements in not_run.items():
    print(f'NOT_RUN {label}: requires {requirements}', flush=True)
for label, command in sorted(checks):
    run(command, label)
if 'tools/android/tests/pirate_spawn/run.py' in local_runs:
    run([sys.executable, str(root / 'tools/android/tests/pirate_spawn/run.py'), '--game', str(game),
         '--engine', str(engine_source)], 'tools/android/tests/pirate_spawn/run.py')
if shot_runs := [label for label in shot_checks if label in local_runs]:
    with tempfile.TemporaryDirectory(prefix='storm-ui-shot-') as work:
        shot_checks_run(Path(work), shot_runs)
print(f'Host checks: {executed - len(failed)} passed, {len(failed)} failed, {len(not_run)} NOT_RUN '
      '(not passes)', flush=True)
if failed:
    sys.exit(f'{len(failed)} host check(s) failed: ' + ', '.join(failed))
