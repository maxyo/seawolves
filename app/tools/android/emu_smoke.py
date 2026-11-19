#!/usr/bin/env python3
"""Boot smoke of an APK on an emulator or a phone: start the game, load a save, check that it draws.

    emu_smoke.py --apk <--test-loop APK> --serial emulator-5554 --obb <main.*.obb> [--obb <patch.*.obb>] \
        [--scenario 9] [--same-libraries-as <other APK>] [--out <dir>] [--timeout 300]

Installs the APK (only when the device has another one), pushes the game subset OBBs (only when the device has other
ones), and runs a scenario of the on-device driver (android/game/testloop/on, whose header lists the scenarios)
through its TEST_LOOP intent. The default, scenario 9, is the boot smoke: the game starts at its main menu, reaches
the title, loads the subset's town save with «Продолжить» and draws it (a screenshot that is not one colour). Then
this script checks:
- the driver's summary says passed (every step has its own limit and reason);
- the engine that ran is the APK's (the build watermark in the log against libengine.so);
- no crash in the device log: no fatal signal, Java FATAL EXCEPTION, `F` log line or fatal engine stop of the app;
- with --same-libraries-as, the test-loop APK packs the same native libraries as the other APK (SHA-256 each; the
  github flavor's torrent client is the only library the play test-loop APK lacks), so the smoke ran the native
  code of the APK you mean to ship.
Exit 0 only when all hold; the results (screenshots, events, summary, the game's and the device's log) go to --out.

The APK comes from build_apk.py --flavor play --test-loop (debug; STORM_ANDROID_TEST_LOOP_SIBLING=1 of
build_debug.sh); the OBBs from tools/android/game_subset.py (your own game files: kept outside the repository, never
in an APK). The device is any booted arm64-capable emulator or phone; the first run extracts the subset (about a
minute), later runs on the same device reuse it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

import flavors

PACKAGE = 'com.goodoldgames.seawolves'
LAUNCHER = 'org.stormengine.port.GameLauncherActivity'
RESULTS = f'/sdcard/Android/data/{PACKAGE}/files/testloop'
WATERMARK = re.compile(rb'Android_ARM64_[0-9a-f]{12}')
CRASH = re.compile(r'Fatal signal|FATAL EXCEPTION|Engine stopped:|^\S+ \S+\s+\d+\s+\d+ F |AddressSanitizer|Abort message')


def log(text):
    print(f'smoke: {text}', flush=True)


def fail(text):
    sys.exit(f'SMOKE FAILED: {text}')


def adb(serial, *args, check=True, binary=False, timeout=600):
    done = subprocess.run(['adb', '-s', serial, *args], capture_output=True, text=not binary, timeout=timeout)
    if check and done.returncode:
        fail(f'adb {" ".join(args)}: {done.stdout}{done.stderr}'.strip()[:600])
    return done.stdout


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def libraries(apk):
    with zipfile.ZipFile(apk) as z:
        return {name: sha256(z.read(name)) for name in z.namelist() if name.startswith('lib/') and name.endswith('.so')}


def install(serial, apk):
    local = sha256(apk.read_bytes())
    path = adb(serial, 'shell', f'pm path {PACKAGE}', check=False).strip().splitlines()
    base = next((line.split(':', 1)[1] for line in path if line.endswith('base.apk')), None)
    if base and adb(serial, 'shell', f'sha256sum {base}', check=False).split(' ')[0] == local:
        return 'already installed'
    done = subprocess.run(['adb', '-s', serial, 'install', '-r', '-d', '--no-incremental', str(apk)],
                          capture_output=True, text=True, timeout=600)
    if 'Success' not in done.stdout:
        fail(f'install of {apk.name}: {(done.stdout + done.stderr).strip()[:400]}')
    return 'installed'


def push_obbs(serial, obbs):
    directory = f'/sdcard/Android/obb/{PACKAGE}'
    adb(serial, 'shell', f'mkdir -p {directory}')
    present = {}
    for line in adb(serial, 'shell', f'ls -l {directory}', check=False).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[-1].endswith('.obb'):
            present[parts[-1]] = int(parts[4])
    wanted = {obb.name for obb in obbs}
    for name in present:
        if name not in wanted:
            adb(serial, 'shell', f"rm -f '{directory}/{name}'")
    pushed = []
    for obb in obbs:
        if present.get(obb.name) != obb.stat().st_size:
            adb(serial, 'push', str(obb), f'{directory}/{obb.name}', timeout=1800)
            pushed.append(obb.name)
    return pushed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--apk', type=Path, required=True, help='the --test-loop APK (play flavor, debug)')
    parser.add_argument('--serial', required=True, help='a booted emulator (adb devices)')
    parser.add_argument('--obb', type=Path, action='append', required=True,
                        help='the game subset: main.<vc>.<package>.obb and optionally patch.<vc>.<package>.obb')
    parser.add_argument('--scenario', type=int, default=9, help='the driver scenario to run (default: 9, the boot smoke)')
    parser.add_argument('--same-libraries-as', type=Path, help='another APK whose native libraries must be the same')
    parser.add_argument('--out', type=Path, default=Path('build/emu-smoke'))
    parser.add_argument('--timeout', type=int, default=300, help='seconds from the start to the driver summary')
    a = parser.parse_args()
    started = time.monotonic()
    for path in [a.apk, *a.obb] + ([a.same_libraries_as] if a.same_libraries_as else []):
        if not path.is_file():
            fail(f'{path}: no such file')
    ours = libraries(a.apk)
    if 'lib/arm64-v8a/libengine.so' not in ours:
        fail(f'{a.apk} has no lib/arm64-v8a/libengine.so')
    if a.same_libraries_as:
        theirs = libraries(a.same_libraries_as)
        # The test-loop APK is the play flavor; a github APK adds only the torrent client (flavors.py).
        github_only = {f'lib/arm64-v8a/lib{flavors.TORRENT_LIBRARY}.so'}
        differ = sorted(name for name in set(ours) | set(theirs) - github_only if ours.get(name) != theirs.get(name))
        if differ:
            fail(f'the test-loop APK and {a.same_libraries_as.name} pack different native libraries: {", ".join(differ)}')
        log(f'native libraries identical to {a.same_libraries_as.name} ({len(ours)})')
    with zipfile.ZipFile(a.apk) as z:
        expected = WATERMARK.search(z.read('lib/arm64-v8a/libengine.so'))
    if not expected:
        fail('no build watermark (Android_ARM64_<rev>) in the APK\'s libengine.so')
    expected = expected[0].decode()
    if adb(a.serial, 'shell', 'getprop sys.boot_completed', check=False).strip() != '1':
        fail(f'{a.serial} is not a booted device')

    # Room for the APK, the OBBs not yet on the device and their extraction (about their size again).
    free = int(adb(a.serial, 'shell', 'df -k /data').splitlines()[-1].split()[3]) * 1024
    present = adb(a.serial, 'shell', f'ls -l /sdcard/Android/obb/{PACKAGE} 2>/dev/null', check=False)
    needed = a.apk.stat().st_size + sum(2 * obb.stat().st_size for obb in a.obb if f' {obb.stat().st_size} ' not in present)
    if free < needed:
        fail(f'{a.serial} has {free >> 20} MB free on /data, the smoke needs about {needed >> 20} MB (APK, OBBs and their '
             'extraction): free space on the emulator (a read-only AVD loses it at the next start)')
    step = time.monotonic()
    log(f'{install(a.serial, a.apk)} {a.apk.name} ({time.monotonic() - step:.0f} s)')
    step = time.monotonic()
    pushed = push_obbs(a.serial, a.obb)
    log(f'OBBs {"pushed: " + ", ".join(pushed) if pushed else "on the device already"} ({time.monotonic() - step:.0f} s)')
    adb(a.serial, 'shell', f'am force-stop {PACKAGE}')
    adb(a.serial, 'shell', f'rm -rf {RESULTS}')
    adb(a.serial, 'logcat', '-c')
    # The launcher waits for window focus: the screen on and unlocked, no system dialog in front (a slow emulator shows
    # "System UI isn't responding" dialogs that keep the focus).
    focus = adb(a.serial, 'shell', 'dumpsys window | grep mCurrentFocus', check=False).strip()
    if 'Not Responding' in focus:
        log(f'closing a system dialog that holds the focus: {focus}')
    adb(a.serial, 'shell', 'input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard; settings put system screen_off_timeout 1800000; '
        'am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS')
    step = time.monotonic()
    adb(a.serial, 'shell', f'am start -a com.google.intent.action.TEST_LOOP -n {PACKAGE}/{LAUNCHER} --ei scenario {a.scenario}')
    summary_path = f'{RESULTS}/summary-{a.scenario}.json'
    summary = None
    while time.monotonic() - started < a.timeout:
        text = adb(a.serial, 'shell', f'cat {summary_path} 2>/dev/null', check=False)
        if text.strip().startswith('{'):
            summary = json.loads(text)
            break
        time.sleep(2)
    ran = time.monotonic() - step
    time.sleep(2)  # the game process writes its log copy and the done marker after the summary
    a.out.mkdir(parents=True, exist_ok=True)
    adb(a.serial, 'pull', RESULTS, str(a.out), check=False)
    device_log = adb(a.serial, 'logcat', '-d', '-v', 'threadtime', check=False)
    (a.out / 'logcat.txt').write_text(device_log)
    adb(a.serial, 'shell', f'am force-stop {PACKAGE}', check=False)

    problems = []
    if summary is None:
        problems.append(f'no driver summary within {a.timeout} s (see {a.out}/logcat.txt)')
    elif summary.get('status') != 'passed':
        problems.append(f"the driver failed: {summary.get('reason')}")
    running = re.search(r'Running on (Android_ARM64_[0-9a-f]{12})', device_log)
    if not running:
        problems.append('the engine logged no build watermark ("Running on Android_ARM64_...")')
    elif running[1] != expected:
        problems.append(f'the device ran {running[1]}, the APK has {expected} (a stale install?)')
    pids = set(re.findall(rf'Start proc (\d+):{re.escape(PACKAGE)}', device_log))
    crashes = [line for line in device_log.splitlines() if CRASH.search(line) and
               (PACKAGE in line or any(f' {pid} ' in line for pid in pids))]
    if crashes:
        problems.append('crash lines in the device log:\n  ' + '\n  '.join(crashes[:8]))
    game = (summary or {}).get('game', {})
    log(f"title {game.get('mainMenuSeconds', '-')} s, auto preset {game.get('autoPresetSeconds', '-')} s, "
        f"saved scene {game.get('sceneSeconds', '-')} s ({game.get('mode', '-')}) after the game started; "
        f"install of the subset {(summary or {}).get('installMs', '-')} ms; driver {ran:.0f} s, "
        f"total {time.monotonic() - started:.0f} s; results {a.out}")
    if problems:
        fail('\n'.join(problems))
    log(f'passed: {expected}')


if __name__ == '__main__':
    main()
