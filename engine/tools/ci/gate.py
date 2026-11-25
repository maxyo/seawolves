#!/usr/bin/env python3
"""The local check before a pull request: build and check what the change can affect.

  tools/ci/gate.py [--base origin/main] [--full] [--tests NAME ...] [--skip STAGE ...] [--boot auto|on|off]

Run it from engine/ on the branch you are about to propose (README.md here):
1. plan from the diff base...HEAD plus uncommitted changes (tools/ci/affected.py): the host checks, the game-data
   checks (tools/game_checks), the UI host build + ctest, the Android arm64 engine build and the engine boot stage
   (tools/ci/boot_stage.py: an ASan+UBSan host engine loads a save and three scenes; --boot auto runs it when the
   change can affect the engine at run time, on always, off (default) never). Everything left out is printed with
   the reason (SKIP lines); --full selects every check and build (the boot stage still follows --boot), --tests adds
   host checks by name, --skip leaves out a stage you cannot run here (printed as SKIP; say so in the pull request);
2. the stages run in parallel, each logging to build/gate/<stage>.log; a summary with the times. Exit 0 only if
   every selected stage passed; a host check that could not run here (NOT_RUN) is named in the summary.

Build directories: build/ui-host, build/host-ci, build/android-engine (incremental; ccache through tools/ccache).
Inputs of the stages that need more than a compiler; a selected stage whose input is missing stops the gate with
the variable to set (or --skip that stage):
- game_checks: $STORM_GAME_DIR, an installed copy of the game;
- android: $STORM_CI_WORK (default build/android-ci) prepared by tools/android/ci_dependencies.sh (SDK, NDK and
  the pinned dependencies);
- boot: $STORM_GAME_DIR, $STORM_BOOT_SAVE and $STORM_VIRTUALGL (tools/ci/boot_stage.py).
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/ci'))
import affected  # noqa: E402
import check_tmp  # noqa: E402
import run_host_tests  # noqa: E402

sys.path.insert(0, str(root / 'tools'))
import pins  # noqa: E402

ci_work = Path(os.environ.get('STORM_CI_WORK', root / 'build/android-ci'))
game_dir = Path(os.environ['STORM_GAME_DIR']) if os.environ.get('STORM_GAME_DIR') else None
STAGES = ('host_checks', 'game_checks', 'ui_host', 'android', 'boot')
logs = root / 'build/gate'


def git(*args, check=True):
    return subprocess.run(['git', '-C', str(root), *args], check=check, capture_output=True, text=True).stdout.strip()


def run_logged(stage, commands, env=None):
    """Runs the commands one after another into build/gate/<stage>.log; the first failure stops."""
    log = logs / f'{stage}.log'
    with open(log, 'w') as out:
        for command in commands:
            out.write('$ ' + ' '.join(map(str, command)) + '\n')
            out.flush()
            done = subprocess.run([str(c) for c in command], cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT)
            if done.returncode != 0:
                return False
    return True


def tail(stage, lines=15):
    text = (logs / f'{stage}.log').read_text(errors='replace').splitlines()
    return '\n'.join('    ' + line for line in text[-lines:])


def xvfb():
    return ['xvfb-run', '-a'] if shutil.which('xvfb-run') and not os.environ.get('DISPLAY') else []


def stage_ui_host():
    build = root / 'build/ui-host'
    tmp = build / 'tmp'  # the stage's TMPDIR (tools/ci/check_tmp.py)
    commands = []
    if not (build / 'CMakeCache.txt').exists():
        commands.append(['cmake', '-S', root / 'src/libs/ui', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'])
    commands += [
        ['cmake', '--build', build],
        [*xvfb(), 'ctest', '--test-dir', build, '-j8', '--output-on-failure'],
    ]
    passed = run_logged('ui_host', commands, check_tmp.prepare(tmp))
    check_tmp.finish(tmp, passed)
    return passed


def stage_host_checks(names, jobs):
    return run_logged(
        'host_checks', [[*xvfb(), sys.executable, root / 'tools/ci/run_host_tests.py', '--jobs', str(jobs), '--only', *names]]
    )


def cpu_budget(requested):
    """Host checks in parallel: --jobs, or with --jobs 0 what the machine has free (cores minus the load average of
    the last minute, at least 3, at most 8), so the gate shares a busy machine."""
    if requested:
        return requested, f'--jobs {requested}'
    cores, load = os.cpu_count() or 4, os.getloadavg()[0]
    jobs = max(3, min(8, int(cores - load)))
    return jobs, f'{jobs} ({cores} cores, load {load:.0f})'


game_results = root / 'build/game-checks/results'
game_deps = root / 'build/game-checks/deps'


def compiler_identity():
    cxx = os.environ.get('STORM_CCACHE_CXX', os.environ.get('CXX', 'c++'))
    done = subprocess.run([cxx, '--version'], capture_output=True, text=True)
    return f'{cxx}\n{done.stdout}'


def game_fingerprint():
    """Every file of the game, with its size and modification time (0.7 s for the 46000 files of TEHO)."""
    digest = hashlib.sha256(str(game_dir.resolve()).encode())
    for directory, dirs, files in os.walk(game_dir):
        dirs.sort()
        for name in sorted(files):
            info = os.stat(os.path.join(directory, name))
            digest.update(f'{directory}/{name}\0{info.st_size}\0{info.st_mtime_ns}\n'.encode())
    return digest.hexdigest()


def game_check_key(name, game, compiler):
    """The inputs of a game check's last passing run, or None when they are not known: its own files, every file its
    compiles read (tools/ccache/cxx STORM_CXX_DEPS; a CMake check: its Ninja graph), the compiler and the game."""
    check = root / 'tools/game_checks' / name
    inputs = set()
    if (check / 'CMakeLists.txt').exists():
        build = root / 'build/game-checks' / name
        graph = affected.BuildGraph(build)
        if graph.inputs is None:
            return None
        # Its sources, not what the build makes (objects, the binary, CMake's files: those follow from the sources).
        inputs = {p for p in graph.inputs if not p.startswith(str(build.resolve()) + '/') and p != str(build.resolve())}
    else:
        listed = game_deps / f'{name}.txt'
        lines = listed.read_text().splitlines() if listed.is_file() else []
        if not lines or '!incomplete' in lines:
            return None
        inputs = set(lines)
    digest = hashlib.sha256(f'{compiler}\n{game}\n'.encode())
    for path in sorted({str(p) for p in check.rglob('*') if p.is_file()} | inputs):
        try:
            digest.update(path.encode() + b'\0' + hashlib.sha256(Path(path).read_bytes()).digest())
        except OSError:
            return None  # an input is gone: run it
    return digest.hexdigest()


def run_game_check(name, game, compiler):
    """(passed, cached, seconds, log text) of one game check. A pass is remembered with its key (game_check_key);
    the same inputs again are a cached pass, said so in the log."""
    started = time.monotonic()
    record = game_results / f'{name}.json'
    key = game_check_key(name, game, compiler)
    try:
        previous = json.loads(record.read_text()) if key and record.is_file() else {}
    except ValueError:
        previous = {}
    if key and previous.get('key') == key:
        return (
            True,
            True,
            time.monotonic() - started,
            (
                f'CACHED PASS {name}: its inputs (own files, compiled files, compiler, game) are those of the pass at '
                f'{previous["when"]} ({previous["seconds"]:.0f} s); key {key[:16]}\n{previous["tail"]}\n'
            ),
        )
    partial = game_deps / f'{name}.txt.partial'
    partial.unlink(missing_ok=True)
    tmp = root / 'build/game-checks/tmp' / name
    env = check_tmp.prepare(tmp)
    env.update(
        CXX=str(root / 'tools/ccache/cxx'), STORM_CCACHE_CXX=os.environ.get('CXX', 'c++'), STORM_CXX_DEPS=str(partial)
    )
    done = subprocess.run(
        [sys.executable, root / f'tools/game_checks/{name}/run.py', game_dir],
        cwd=root,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors='replace',
    )
    seconds = time.monotonic() - started
    if done.returncode == 0:
        if partial.exists():
            (game_deps / f'{name}.txt').write_text(
                ''.join(f'{line}\n' for line in sorted(set(partial.read_text().splitlines())))
            )
        key = game_check_key(name, game, compiler)
        if key:
            record.write_text(
                json.dumps(
                    {
                        'key': key,
                        'when': time.strftime('%Y-%m-%d %H:%M'),
                        'seconds': seconds,
                        'tail': '\n'.join(done.stdout.splitlines()[-8:]),
                    }
                )
            )
    else:
        record.unlink(missing_ok=True)
    partial.unlink(missing_ok=True)
    check_tmp.finish(tmp, done.returncode == 0)
    return done.returncode == 0, False, seconds, done.stdout


def stage_game_checks(names):
    """The game checks in parallel (each is independent and reads the game only); every result's time in the log."""
    game_results.mkdir(parents=True, exist_ok=True)
    game_deps.mkdir(parents=True, exist_ok=True)
    game, compiler = game_fingerprint(), compiler_identity()
    with concurrent.futures.ThreadPoolExecutor(len(names)) as pool:
        results = dict(zip(names, pool.map(lambda n: run_game_check(n, game, compiler), names)))
    with open(logs / 'game_checks.log', 'w') as out:
        for name, (ok, cached, seconds, text) in results.items():
            out.write(f'$ tools/game_checks/{name}/run.py {game_dir}\n{text}')
        for name, (ok, cached, seconds, text) in results.items():
            out.write(f'{"PASS" if ok else "FAIL"} {name} ({"cached, " if cached else ""}{seconds:.0f}s)\n')
    return all(ok for ok, *_ in results.values())


def stage_boot():
    return run_logged('boot', [[sys.executable, root / 'tools/ci/boot_stage.py']])


def stage_android():
    build = root / 'build/android-engine'
    env = dict(os.environ, PATH=f'{ci_work / "python/bin"}:{os.environ["PATH"]}')
    commands = []
    if not (build / 'CMakeCache.txt').exists():  # later builds reconfigure themselves when an input of it changes
        commands.append(
            [
                'cmake',
                '-S',
                root,
                '-B',
                build,
                '-G',
                'Ninja',
                '-DCMAKE_BUILD_TYPE=Release',
                f'-DCMAKE_TOOLCHAIN_FILE={root}/cmake/toolchains/android-arm64.cmake',
                f'-DSTORM_ANDROID_NDK={pins.ndk(ci_work)}',
                f'-DSTORM_ANDROID_DEPS={ci_work}/prefix',
            ]
        )
    commands.append(['ninja', '-C', build, 'engine'])
    return run_logged('android', commands, env)


def plan(files, full=False, host_checks=True, work=root / 'build/host-ci', builds=None):
    """The checks and stages of a change (tools/ci/affected.py). The plan reads the checks the host check build lists:
    with the host checks, that build is first configured as their stage runs it (with the renderer's checks) and
    regenerated when an input of its configure changed, so a check added or removed since the last build is planned
    as the stage will find it."""
    if host_checks:
        run_host_tests.configure(work, renderer=True)
        subprocess.run([affected.ninja(), '-C', str(work), 'build.ninja'], check=True, stdout=subprocess.DEVNULL)
    return affected.plan(files, work, builds=builds, full=full)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default='origin/main', help='what the change is compared with (default origin/main)')
    parser.add_argument(
        '--full', action='store_true', help='every check and build, whatever changed; add --boot on for a release'
    )
    parser.add_argument('--tests', nargs='*', default=[], help='host checks to run in addition')
    parser.add_argument(
        '--jobs',
        type=int,
        default=0,
        help='host checks in parallel; 0 (default): the free cores (cores minus load, 3 to 8)',
    )
    parser.add_argument(
        '--skip',
        nargs='*',
        default=[],
        choices=STAGES,
        metavar='STAGE',
        help=f'stages to leave out even when selected: {", ".join(STAGES)}',
    )
    parser.add_argument(
        '--boot',
        choices=('auto', 'on', 'off'),
        default='off',
        help='engine boot stage: off (default); auto when the change can affect the engine at run time; on always',
    )
    args = parser.parse_args()
    # Only this run's logs and boot report.
    shutil.rmtree(logs, ignore_errors=True)
    logs.mkdir(parents=True)
    started = time.monotonic()
    dirty = git('status', '--porcelain', '--untracked-files=no', '--', '.')
    if dirty:
        print('GATE: the gate includes uncommitted changes:\n' + dirty, flush=True)
    base_sha = git('rev-parse', args.base)
    files = affected.changed_files(args.base)
    selection = plan(files, full=args.full, host_checks='host_checks' not in args.skip)
    checks, games = selection['host_checks'], selection['game_checks']
    ui, android = selection['ui_host'], selection['android']
    boot_selection = selection['boot']
    boot = {
        'auto': boot_selection,
        'on': (True, 'on (--boot on)'),
        'off': (
            False,
            f'off (--boot off, the default); auto would {"RUN" if boot_selection[0] else "SKIP"}: {boot_selection[1]}',
        ),
    }[args.boot]

    def skipped_by_option(stage, chosen):
        return (False, f'left out (--skip {stage})') if stage in args.skip and chosen[0] else chosen

    ui, android, boot = (
        skipped_by_option('ui_host', ui),
        skipped_by_option('android', android),
        skipped_by_option('boot', boot),
    )
    if 'game_checks' in args.skip:
        games = {n: skipped_by_option('game_checks', chosen) for n, chosen in games.items()}
    names = sorted({n for n, (run, _) in checks.items() if run} | set(args.tests))
    if 'host_checks' in args.skip:
        names = []
    plan_lines = [
        f'GATE {git("rev-parse", "--short", "HEAD")} over {args.base} {base_sha[:8]}: {len(files)} changed files'
    ]
    for name, (run, why) in checks.items():
        if not run and name not in args.tests:
            plan_lines.append(f'SKIP host check {name}: {why}')
    for name, (run, why) in games.items():
        plan_lines.append(f'{"RUN " if run else "SKIP"} game check {name}: {why}')
    jobs, jobs_why = cpu_budget(args.jobs)
    plan_lines.append(
        f'{"RUN " if names else "SKIP"} host checks ({len(names)}, {jobs_why} in parallel): '
        f'{" ".join(names) or ("left out (--skip host_checks)" if "host_checks" in args.skip else "none affected")}'
    )
    for label, (run, why) in (('ui_host', ui), ('android', android)):
        plan_lines.append(f'{"RUN " if run else "SKIP"} {label}: {why}')
    plan_lines.append(f'{"RUN " if boot[0] else "SKIP"} boot: {boot[1]}')  # --boot decides, not --full
    (logs / 'plan.txt').write_text('\n'.join(plan_lines) + '\n')
    skipped = sum(1 for line in plan_lines if line.startswith('SKIP host check '))
    for line in plan_lines:
        if not line.startswith('SKIP host check '):
            print(line, flush=True)
    if skipped:
        print(f'SKIP {skipped} host checks no changed file can affect (reasons: build/gate/plan.txt)', flush=True)

    stages = {}
    if ui[0]:
        stages['ui_host'] = stage_ui_host
    if names:
        stages['host_checks'] = lambda: stage_host_checks(names, jobs)
    game_names = [n for n, (run, _) in games.items() if run]
    if game_names:
        if game_dir is None or not game_dir.is_dir():
            sys.exit(
                f'GATE: the game-data checks {game_names} need an installed copy of the game: set STORM_GAME_DIR '
                f'({game_dir or "not set"}), or --skip game_checks'
            )
        stages['game_checks'] = lambda: stage_game_checks(game_names)
    if android[0]:
        if not (ci_work / 'tools/ndk').is_dir():
            sys.exit(
                f'GATE: the Android build needs the SDK, NDK and dependencies in {ci_work}: run '
                f'tools/android/ci_dependencies.sh (STORM_CI_WORK), or --skip android'
            )
        stages['android'] = stage_android
    if boot[0]:
        stages['boot'] = stage_boot  # tools/ci/boot_stage.py checks its environment itself
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max(1, len(stages))) as pool:
        futures = {}
        for name, stage in stages.items():
            futures[pool.submit(lambda s=stage: (time.monotonic(), s(), time.monotonic()))] = name
        for future in concurrent.futures.as_completed(futures):
            name = futures[future]
            begin, ok, end = future.result()
            results[name] = (ok, end - begin)
            print(f'{"PASS" if ok else "FAIL"} {name} ({end - begin:.0f}s, build/gate/{name}.log)', flush=True)
            if not ok:
                print(tail(name), flush=True)
    failed = [name for name, (ok, _) in results.items() if not ok]
    total = time.monotonic() - started
    # A host check that could not run here (tools/ci/run_host_tests.py: exit 77) is no pass: named in the summary.
    not_run = []
    if 'host_checks' in results:
        not_run = [
            line.split()[1]
            for line in (logs / 'host_checks.log').read_text(errors='replace').splitlines()
            if line.startswith('NOT_RUN ')
        ]
    unrun = f'; NOT_RUN (not passes): {", ".join(not_run)} (build/gate/host_checks.log)' if not_run else ''
    if failed:
        sys.exit(f'GATE FAILED ({total:.0f}s): {", ".join(failed)}{unrun}')
    print(
        f'GATE PASSED on {git("rev-parse", "--short", "HEAD")} over {args.base} {base_sha[:8]} ({total:.0f}s){unrun}',
        flush=True,
    )


if __name__ == '__main__':
    main()
