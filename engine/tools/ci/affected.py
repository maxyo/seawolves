#!/usr/bin/env python3
"""What a change can affect: the host checks (the CTest project tests/CMakeLists.txt), the game-data checks
(tools/game_checks), the builds a gate runs (the UI host build, the Android engine build) and the engine boot stage
(tools/ci/boot_stage.py). Used by tools/ci/gate.py and `run_host_tests.py --changed-since`; every check or build it
leaves out is reported with the reason.

  affected.py [--base origin/main] [--head HEAD] [--work build/host-ci] [--json]

The builds know what they read: a host check is affected when a changed file is an input of its program in the
configured host check build (Ninja's graph and dependency log), a new file lies in a directory that program reads,
a file of its own directory tests/<group> changed, or a changed file lies under one of the run-time inputs it
declares (STORM_INPUTS: what a script check reads as text, the tool it drives). The host check build's own CMake
files, the compiler launchers (tools/ccache) and the runner select every host check; a changed CMake file of the
engine selects the checks that read its directory. Markdown and the GitHub workflows affect nothing here.
"""

import argparse
import concurrent.futures
import functools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]

# Every build compiles through the launchers; the host check project's top-level files (tests/CMakeLists.txt,
# host_checks.cmake, ...) are tests/<file>: both select everything.
EVERYTHING = ('tools/ccache/',)
# The runner of every host check.
ALL_HOST_CHECKS = ('tools/ci/run_host_tests.py',)
# The GitHub workflows: run by GitHub after the push, read by nothing here.
NOT_LOCAL = ('.github/',)
# The format check and what it reads: every C/C++ file git tracks (tools/format/cxx_format.py), its configuration.
FORMAT_CHECK = 'cxx_format'
FORMATTED = ('.c', '.cc', '.cpp', '.h', '.hpp', '.inl')
FORMAT_INPUTS = ('.clang-format', 'tools/format/')
# Source files: what the engine builds compile or the engine reads at run time.
SOURCE_ROOTS = ('src/', 'android/')
SOURCE_SUFFIXES = ('.c', '.cpp', '.h', '.hpp', '.inc', '.inl', '.sc', '.sh', '.fx', '.json', '.txt', '.cmake',
                   '.patch', '.java', '.def', '.rml', '.rcss')
# Read by the engine at run time and by no build: the scene sweep lays the techniques over the game's, as the APK.
RUNTIME_INPUTS = ('src/techniques/',)
BOOT_TOOLS = ('tools/scene_sweep/', 'tools/ci/boot_stage.py', 'tools/ci/ubsan.supp')


def changed_files(base, head='HEAD', repo=root):
    """Files changed between the merge base of base and head, and head (plus uncommitted ones when head is HEAD).
    Paths are relative to the engine tree (--relative: the engine is the directory engine/ of the repository; changes
    outside it are the application's and are not listed)."""
    out = subprocess.check_output(
        ['git', '-C', str(repo), 'diff', '--relative', '--name-status', '--no-renames', f'{base}...{head}'], text=True
    ).splitlines()
    if head == 'HEAD':
        out += subprocess.check_output(
            ['git', '-C', str(repo), 'diff', '--relative', '--name-status', '--no-renames', 'HEAD'], text=True
        ).splitlines()
    files = {}
    for line in out:
        status, path = line.split('\t', 1)
        if path.endswith('.md'):
            continue  # documentation: read by no build or check
        files[path] = 'A' if status.startswith('A') and files.get(path, 'A') == 'A' else 'M'
    return files


def is_source(path):
    return path.startswith(SOURCE_ROOTS) and path.endswith(SOURCE_SUFFIXES)


def local_files(files):
    """The changed files without the GitHub workflows, which nothing here reads."""
    return {p: s for p, s in files.items() if not p.startswith(NOT_LOCAL)}


def ninja():
    # Without a system ninja: the one tools/android/ci_dependencies.sh installs into $STORM_CI_WORK.
    return shutil.which('ninja') or str(Path(os.environ.get('STORM_CI_WORK', root / 'build/android-ci')) / 'python/bin/ninja')


@functools.cache
def real(path):
    return os.path.realpath(path)


def resolve(build_dir, paths):
    return {real(p if os.path.isabs(p) else os.path.join(build_dir, p)) for p in paths}


class BuildGraph:
    """What a Ninja build directory reads: its dependency log (the headers of every object), the inputs of its
    targets and the configure inputs of build.ninja (CMake's re-run edge). inputs is None when it has never been built.
    """

    def __init__(self, build_dir):
        self.build_dir = Path(build_dir)
        self.inputs = None
        if not (self.build_dir / '.ninja_deps').exists() or not (self.build_dir / 'build.ninja').exists():
            return
        self.deps = {}  # object -> the headers it read
        current = None
        log = subprocess.run([ninja(), '-C', str(self.build_dir), '-t', 'deps'], capture_output=True, text=True)
        for line in log.stdout.splitlines():
            if line.startswith('    '):
                self.deps[current].append(line.strip())
            elif line and not line.startswith(' '):
                current = line.split(':', 1)[0]
                self.deps[current] = []
        self.configure = set()
        for line in (self.build_dir / 'build.ninja').read_text(errors='ignore').splitlines():
            if line.startswith('build build.ninja') and ': RERUN_CMAKE' in line and ' | ' in line:
                self.configure = resolve(self.build_dir, (p.replace('$ ', ' ') for p in line.split(' | ', 1)[1].split()))
        self.inputs = self.target_inputs(None) | self.configure
        self.dirs = {os.path.dirname(p) for p in self.inputs}

    @functools.cache
    def target_inputs(self, target):
        """Every file the target (None: the default targets) is built from: its inputs (recursively) and the headers
        their compiles read."""
        listed = subprocess.run(
            [ninja(), '-C', str(self.build_dir), '-t', 'inputs', *([target] if target else [])],
            capture_output=True,
            text=True,
        ).stdout.split()
        paths = set(listed)
        for item in listed:
            paths.update(self.deps.get(item, ()))
        return frozenset(resolve(self.build_dir, paths))

    @functools.cache
    def target_dirs(self, target):
        return frozenset(os.path.dirname(p) for p in self.target_inputs(target))

    def hit(self, files, repo=root, target=None):
        """The first changed file the build (or the given target of it) reads, or None."""
        inputs = self.inputs if target is None else self.target_inputs(target)
        dirs = self.dirs if target is None else self.target_dirs(target)
        for path, status in files.items():
            full = real(str(repo / path))
            if full in inputs:
                return path
            if status == 'A' and os.path.dirname(full) in dirs:
                return f'{path} (new, in a directory the build reads)'
        return None


@functools.cache
def graph(build_dir):
    """The build graph of a directory, read once per process."""
    return BuildGraph(build_dir)


@functools.cache
def host_tests(work):
    """The checks of the configured host check build: ((name, group, target or None, (input, ...)), ...), or None
    when <work> is not configured. target: the Ninja target of the program the check runs (a script check has none)."""
    work = Path(work)
    if not (work / 'CTestTestfile.cmake').exists():
        return None
    listing = subprocess.run(['ctest', '--test-dir', str(work), '--show-only=json-v1'], capture_output=True, text=True)
    if listing.returncode != 0:
        return None
    found = []
    for test in json.loads(listing.stdout)['tests']:
        properties = {p['name']: p['value'] for p in test.get('properties', [])}
        if 'setup' in properties.get('LABELS', []):
            continue
        inputs = properties.get('STORM_INPUTS', [])
        inputs = inputs if isinstance(inputs, list) else [i for i in inputs.split(';') if i]
        command = test.get('command', [])
        program = command[0] if command else ''
        target = None
        if program.startswith(str(work.resolve()) + '/'):
            target = os.path.relpath(program, work.resolve())
            # The ASan tree builds the same programs from the same sources as the main tree.
            if target.startswith('renderer-asan/'):
                target = 'renderer_bgfx/' + target.split('/', 1)[1]
        elif '--target' in command:
            target = command[command.index('--target') + 1]
        script = next((c for c in command[1:2] if c.endswith('.py')), None)
        if script:
            inputs.append(os.path.dirname(script))
        group = 'renderer_bgfx' if 'renderer' in properties.get('LABELS', []) else test['name'].split('.', 1)[0]
        found.append((test['name'], group, target, tuple(inputs)))
    return tuple(found)


def everything_reason(files, built=None, repo=root):
    """Why a change selects everything (the launchers, the host check project), or None. With the host check build:
    why it selects every host check (also its runner and its configure inputs outside src/ and android/)."""
    for path in files:
        if path.startswith(EVERYTHING) or (path.startswith('tests/') and path.count('/') == 1):
            return f'{path} changes the host checks themselves'
    if built is None:
        return None
    path = next((p for p in files if p.startswith(ALL_HOST_CHECKS)), None)
    if path:
        return f'{path} runs every host check'
    if built.inputs is not None:
        for path in files:
            full = real(str(repo / path))
            if full in built.configure and not path.startswith(SOURCE_ROOTS):
                return f'{path} is read by the configure step of the host checks'
    return None


def under(full, inputs):
    """The declared input (file or directory) the path is, or lies under, or None."""
    return next((i for i in inputs if full == real(i) or full.startswith(real(i) + '/')), None)


def host_checks(files, work=None, repo=root):
    """{name: (affected, reason)} for every check of the host check build in work (default build/host-ci)."""
    work = Path(work or repo / 'build/host-ci')
    files = local_files(files)
    if host_tests(str(work)) is None:
        # The checks are known once the project is configured (seconds; the build directory the runner uses).
        subprocess.run(['cmake', '-S', str(repo / 'tests'), '-B', str(work), '-G', 'Ninja'], check=True,
                       stdout=subprocess.DEVNULL)
        host_tests.cache_clear()
    listed = host_tests(str(work))
    built = graph(str(work))
    every = everything_reason(files, built, repo)
    # A changed CMake file of the engine: the checks that read its directory.
    cmake_dirs = [
        os.path.dirname(os.path.realpath(repo / p))
        for p in files
        if built.inputs is not None and os.path.realpath(repo / p) in built.configure and p.startswith(SOURCE_ROOTS)
    ]

    def decide(test):
        name, group, target, inputs = test
        if every:
            return True, every
        own = next((p for p in files if p.startswith(f'tests/{group}/')), None)
        if own:
            return True, f'its own {own} changed'
        if name == FORMAT_CHECK:
            hit = next((p for p in files if p.endswith(FORMATTED) or p.startswith(FORMAT_INPUTS)), None)
            return (True, f'formats {hit}') if hit else (False, 'no changed C/C++ file or format setting')
        for path in files:
            declared = under(real(str(repo / path)), inputs)
            if declared:
                return True, f'it reads {path} (under its declared input {os.path.relpath(declared, repo)})'
        if target:
            if built.inputs is None:
                return True, f'no build graph yet in {work}'
            program = built.target_inputs(target)
            hit = built.hit(files, repo, target)
            if hit:
                return True, f'its program reads {hit}'
            reads = next((d for d in cmake_dirs if any(p.startswith(d + '/') for p in program)), None)
            if reads:
                return True, f'its program is built from {os.path.relpath(reads, repo)}, whose CMake file changed'
            return False, 'no changed file is an input of its program'
        return False, 'no changed file is one of its inputs'

    if built.inputs is not None:  # the programs' inputs, read once per process (one ninja call each)
        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            list(pool.map(built.target_inputs, {target for _, _, target, _ in listed if target}))
    return {test[0]: decide(test) for test in listed}


def build_affected(build_dir, files, label, repo=root):
    every = everything_reason(files)
    if every:
        return True, every
    files = local_files(files)
    if any(p.startswith('tools/android/') for p in files):
        return True, 'tools/android changed'
    if not files:
        return False, 'only the GitHub workflows changed'
    built = graph(str(build_dir))
    if built.inputs is None:
        return True, f'{label}: no build graph yet in {build_dir}'
    hit = built.hit(files, repo)
    return (True, f'it reads {hit}') if hit else (False, f'no changed file is an input of {label} ({build_dir})')


@functools.cache
def included_headers(check, repo, cxx, glm):
    """The headers a game check's check.cpp includes (the -I directories its run.py passes and GLM's, compiler -M:
    unlike -MM it fails on a missing <header> instead of leaving it out), or the reason they could not be listed."""
    check, repo = Path(check), Path(repo)
    includes = ['-I' + str(repo / m) for m in re.findall(r"'-I' \+ str\(root / '([^']+)'\)", check.read_text())]
    if not glm:
        return 'the pinned GLM is not known: configure the host check build (build/host-ci) first'
    includes += ['-I' + directory for directory in glm]
    dep = subprocess.run(
        [cxx, '-std=c++20', '-M', *includes, str(check.parent / 'check.cpp')], capture_output=True, text=True
    )
    if dep.returncode != 0:
        return dep.stderr.strip()[:200]
    return frozenset(os.path.realpath(h) for h in dep.stdout.replace('\\\n', ' ').split()[1:])


def pinned_glm(work):
    """The include directories of GLM in the host check build (STORM_GLM_HEADERS in its cache: storm_glm.h's and the
    pinned GLM's), or None."""
    cache = Path(work) / 'CMakeCache.txt'
    found = re.search(r'^STORM_GLM_HEADERS:\w+=(.+)$', cache.read_text(), re.M) if cache.exists() else None
    return tuple(found[1].split(';')) if found else None


def game_checks(files, repo=root, builds=None, work=None):
    """{name: (affected, reason)} for every tools/game_checks/<name>/run.py; builds: their build directories'
    parent (default <repo>/build/game-checks)."""
    builds = Path(builds or repo / 'build/game-checks')
    result = {}
    every = everything_reason(files)
    files = local_files(files)
    cxx = os.environ.get('STORM_CCACHE_CXX', 'c++')
    glm = pinned_glm(work or repo / 'build/host-ci')
    for check in sorted((repo / 'tools/game_checks').glob('*/run.py')):
        name = check.parent.name
        own = next((p for p in files if p.startswith(f'tools/game_checks/{name}/')), None)
        if every or own:
            result[name] = (True, every or f'its own {own} changed')
            continue
        # The libraries it links (its CMakeLists.txt): its build graph once built.
        if (check.parent / 'CMakeLists.txt').exists():
            built = graph(str(builds / name))
            if built.inputs is None:
                result[name] = (True, f'no build graph yet in {builds / name}')
                continue
            hit = built.hit(files, repo)
            if hit:
                result[name] = (True, f'its build reads {hit}')
                continue
        headers = included_headers(str(check), str(repo), cxx, glm)
        if isinstance(headers, str):
            result[name] = (True, 'its include list could not be computed: ' + headers)
            continue
        hit = next((p for p in files if os.path.realpath(repo / p) in headers), None)
        result[name] = (True, f'check.cpp includes {hit}') if hit else (False, 'check.cpp includes no changed file')
    return result


def boot_stage(files, build_dir, orphans=(), repo=root):
    """(affected, reason) of the engine boot stage (tools/ci/boot_stage.py). orphans: the changed source files no
    host or game-data check selects."""
    every = everything_reason(files)
    if every:
        return True, every
    files = local_files(files)
    tool = next((p for p in files if p.startswith(BOOT_TOOLS)), None)
    if tool:
        return True, f'{tool} changed (the boot stage itself)'
    data = next((p for p in files if p.startswith(RUNTIME_INPUTS)), None)
    if data:
        return True, f'{data} is read by the engine at run time'
    built = graph(str(build_dir))
    if built.inputs is None:
        engine = next((p for p in files if p.startswith(SOURCE_ROOTS)), None)
        if engine:
            return True, f'{engine} (the boot build {build_dir} has no build graph yet)'
    else:
        hit = built.hit(files, repo)
        if hit:
            return True, f'the boot build reads {hit}'
    if orphans:
        return True, f'{orphans[0]} is owned by no check'
    return False, 'no changed file can change what the engine does at run time'


def plan(files, work=None, repo=root, builds=None, full=False):
    """Every check and stage of a change: {group: {name: (affected, reason)}} for host_checks and game_checks,
    (affected, reason) for ui_host, android and boot. builds: the build directories' parent (default <repo>/build).
    full: every check and build, whatever changed (the boot stage still follows its own rule)."""
    builds = Path(builds or repo / 'build')
    work = work if work is not None else builds / 'host-ci'
    host = host_checks(files, work, repo)
    games = game_checks(files, repo, builds / 'game-checks', work)
    orphans = []
    for path in (p for p in files if is_source(p)):
        alone = {path: files[path]}
        if alone == files:  # a change of one file: its own selection
            if not any(run for n, (run, _) in host.items() if n != FORMAT_CHECK) and not any(
                run for run, _ in games.values()
            ):
                orphans.append(path)
            continue
        # The format check reads every C/C++ file but checks only its layout: it owns no source.
        owners = {n: chosen for n, chosen in host_checks(alone, work, repo).items() if n != FORMAT_CHECK}
        if not any(run for run, _ in owners.values()) and not any(
            run for run, _ in game_checks(alone, repo, builds / 'game-checks', work).values()
        ):
            orphans.append(path)
    result = {
        'host_checks': host,
        'game_checks': games,
        'ui_host': build_affected(builds / 'ui-host', files, 'the UI host build', repo),
        'android': build_affected(builds / 'android-engine', files, 'the Android engine build', repo),
        'boot': boot_stage(files, builds / 'boot-asan', orphans, repo),
    }
    if full:
        for group in ('host_checks', 'game_checks'):
            result[group] = {name: (True, 'full gate (--full)') for name in result[group]}
        for stage in ('ui_host', 'android'):
            result[stage] = (True, 'full gate (--full)')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default='origin/main')
    parser.add_argument('--head', default='HEAD')
    parser.add_argument('--work', type=Path, default=root / 'build/host-ci')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    files = changed_files(args.base, args.head)
    result = {'changed': files, **plan(files, args.work)}
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        return
    print(f'{len(files)} changed files')
    for group in ('host_checks', 'game_checks'):
        for name, (run, why) in result[group].items():
            print(f'{"RUN " if run else "skip"} {group[:-1]} {name}: {why}')
    for stage in ('ui_host', 'android', 'boot'):
        run, why = result[stage]
        print(f'{"RUN " if run else "skip"} {stage}: {why}')


if __name__ == '__main__':
    main()
