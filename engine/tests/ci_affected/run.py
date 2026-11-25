#!/usr/bin/env python3
"""Check of tools/ci/affected.py, the gate's selection, against the configured host check build (--work), with or
without the renderer's checks (a CI part configures it either way): a check is selected by what its program is built
from and by its declared run-time inputs, never by an unrelated change; the project's own files select every check;
every source file of the engine selects at least one host or game-data check or the engine boot stage (a change never
selects nothing). The game-data and boot build graphs are faked with tiny Ninja directories."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/ci'))
import affected  # noqa: E402
import gate  # noqa: E402
import run_host_tests  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True, help='the configured and built host check build')
work = parser.parse_args().work
failures = []


def expect(condition, text):
    if not condition:
        failures.append(text)


def selected(result, group='host_checks'):
    return {name for name, (run, _) in result[group].items() if run}


def fake_build(directory, *reads):
    """A Ninja build directory whose graph reads the given files."""
    directory.mkdir(parents=True)
    (directory / 'build.ninja').write_text(
        'rule copy\n  command = cp $in $out\n' + ''.join(f'build out{i}: copy {r}\n' for i, r in enumerate(reads))
    )
    subprocess.run([shutil.which('ninja') or 'ninja', '-C', str(directory)], check=True, capture_output=True)
    if not (directory / '.ninja_deps').exists():
        (directory / '.ninja_deps').write_bytes(b'')


with tempfile.TemporaryDirectory() as temp:
    builds = Path(temp)
    fake_build(builds / 'boot-asan', root / 'src/libs/core/src/compiler.cpp')
    fake_build(builds / 'game-checks/far_lod', root / 'src/libs/geometry/far_lod/layout.cpp')
    fake_build(builds / 'android-engine', root / 'src/libs/core/src/compiler.cpp')

    def plan(files, **options):
        files = {f: 'M' for f in files} if isinstance(files, (list, tuple)) else files
        return affected.plan(files, work, root, builds, **options)

    everything = set(plan([])['host_checks'])
    listed = {test['name'] for test in run_host_tests.tests(work)}
    expect(everything == listed, f'the plan and the runner differ: {sorted(everything ^ listed)}')
    # The program's build graph: the header it includes, its own source; not another check's.
    for changed in (['src/libs/util/include/utf8.h'], ['tests/utf8/check.cpp']):
        chosen = selected(plan(changed))
        expect('utf8' in chosen and 'haptics' not in chosen, f'{changed} selects {sorted(chosen)}')
    # A new file in a directory a program reads.
    new = selected(plan({'src/libs/util/include/platform/new_header.hpp': 'A'}))
    expect('haptics' in new, f'a new platform header does not select haptics: {sorted(new)}')
    # A source of the renderer: its checks (when the build has them) and the host checks that read it, not the rest.
    shim = selected(plan(['src/libs/renderer_bgfx/src/shim/device.cpp']))
    expect('utf8' not in shim, f'device.cpp selects {sorted(shim)}')
    if 'renderer_bgfx.skinning_check.vulkan' in everything:
        expect('renderer_bgfx.skinning_check.vulkan' in shim, f'device.cpp selects {sorted(shim)}')
    # Declared run-time inputs: the tool a check drives.
    expect('ci_boot' in selected(plan(['tools/ci/boot_stage.py'])), 'boot_stage.py does not select ci_boot')
    # The project's own files, the launchers, the runner: every host check; the runner alone not the builds.
    for changed in ('tests/CMakeLists.txt', 'tests/host_checks.cmake', 'tools/ccache/launcher'):
        expect(selected(plan([changed])) == everything, f'{changed} does not select every host check')
    runner = plan(['tools/ci/run_host_tests.py'])
    expect(selected(runner) == everything and not runner['android'][0], f'run_host_tests.py: {runner["android"]}')
    expect(selected(plan([], full=True)) == everything, '--full does not select every host check')
    # Nothing here reads the GitHub workflows.
    expect(not selected(plan(['.github/workflows/ci.yml'])), 'ci.yml selects host checks')
    # Run-time renderer input of no build, the boot stage's own tools: the boot stage.
    grass = plan(['src/techniques/effects/grass.fx'])
    expect(grass['boot'][0] and 'run time' in grass['boot'][1], f'grass.fx: {grass["boot"]}')
    expect(plan(['tools/scene_sweep/scene_sweep.c'])['boot'][0], 'scene_sweep.c does not select the boot stage')
    # A source no game-data check reads: none of them (each one's include list is complete).
    sea = plan(['src/libs/sea/src/sea.cpp'])['game_checks']
    expect(not any(run for run, _ in sea.values()), f'sea.cpp selects game checks: {sea}')
    # A library a game-data check links.
    expect('far_lod' in selected(plan(['src/libs/geometry/far_lod/layout.cpp']), 'game_checks'), 'far_lod/layout.cpp')
    # The format check: every C/C++ file and the format settings, nothing else.
    for formatted in ('src/libs/ui/src/icons.cpp', 'android/archive/memory.hpp', '.clang-format'):
        expect('cxx_format' in selected(plan([formatted])), f'{formatted} does not select cxx_format')
    expect('cxx_format' not in selected(grass), 'grass.fx selects cxx_format')

    # The gate plans the checks of the host check build as its stage will build it: a check removed from the project
    # since the build was configured is not planned (the stage would refuse its name).
    project, tree = builds / 'project', builds / 'project-build'
    project.mkdir()
    lists = 'cmake_minimum_required(VERSION 3.28)\nproject(checks LANGUAGES NONE)\nenable_testing()\n'
    (project / 'CMakeLists.txt').write_text(lists + 'add_test(NAME kept COMMAND true)\nadd_test(NAME removed COMMAND true)\n')
    subprocess.run(
        ['cmake', '-S', project, '-B', tree, '-G', 'Ninja', '-DSTORM_CHECK_RENDERER:BOOL=ON'], check=True, capture_output=True
    )
    (project / 'CMakeLists.txt').write_text(lists + 'add_test(NAME kept COMMAND true)\n')
    planned = set(gate.plan({}, work=tree, builds=builds)['host_checks'])
    expect(planned == {'kept'}, f'the gate plans the checks of a stale build: {sorted(planned)}')

    # Every source file selects a check or the boot stage; some only the boot stage (owned by no check).
    tracked = subprocess.run(
        ['git', '-C', str(root), 'ls-files', *affected.SOURCE_ROOTS], capture_output=True, text=True, check=True
    ).stdout.split()
    sources = [source for source in tracked if affected.is_source(source)]
    expect(len(sources) > 1000, f'only {len(sources)} source files found')
    boot_only = 0
    for source in sources:
        result = plan([source])
        # The format check selects every C/C++ file for its layout alone: not an owner of what the file does.
        checks = (selected(result) - {affected.FORMAT_CHECK}) | selected(result, 'game_checks')
        if not checks and not result['boot'][0]:
            failures.append(f'{source} selects nothing')
        boot_only += not checks
        if not checks and not result['boot'][1].endswith(('owned by no check', 'run time')) and (
            'boot build reads' not in result['boot'][1]
        ):
            failures.append(f'{source}: no check and not in the boot graph, but {result["boot"]}')
    print(f'{len(sources)} source files; {boot_only} select no host or game-data check, only the boot stage')
    # A source no check and no build reads: the boot stage, as owned by no check.
    orphan = plan({'src/apps/unread/orphan.cpp': 'A'})
    expect(not selected(orphan) - {affected.FORMAT_CHECK} and not selected(orphan, 'game_checks'),
           f'orphan.cpp selects {sorted(selected(orphan))}')
    expect(orphan['boot'] == (True, 'src/apps/unread/orphan.cpp is owned by no check'), f'orphan.cpp: {orphan["boot"]}')

if failures:
    print('\n'.join(failures))
    sys.exit(f'{len(failures)} affected.py failures')
print('affected.py checks passed')
