#!/usr/bin/env python3
"""The UI pack contract: this application's UI pack (mobile-ui/design, built by build.py) drawn
with the engine's real screen and HUD adapters (engine/src/libs/ui) and this application's interface scripts on the
engine's script VM (engine src/libs/ui/tools/script_game.h) on the engine's recorded
models and states, through the engine's storm_ui_pack_check (engine/src/libs/ui/tools/pack_check.cpp).

The pack is the application's and the adapters that fill it are the engine's; this is where they meet
before a device does. A case fails on any error of the UI host (an unknown element, prop or icon, a missing
string, a rejected admission, a screen that threw) and on any element marked ui-error; every ported
screen must have a case or be listed as uncovered (engine src/libs/ui/tests/pack-contract.json).

The check also crawls every state taps reach without the game and checks where every tap ends (engine
tools/pack_check.cpp). With --game DIR (an installed game: PROGRAM/ and RESOURCE/) the actions the taps sent are
followed into the game's interface scripts and this application's Java (game_bindings.py); without it that part is
not run, and the run says so. The cases run in parallel shards, one process each (one UI host per process).

Needs cmake, a C++20 compiler and the SDL2 runtime library (the script VM links it); Ninja and ccache when installed. No render backend is built (no bgfx, shaderc, X11 or OpenGL). The build directory is build/ui-pack-contract (incremental); the pack is
built into a temporary directory.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
engine = root.parent / 'engine'
ui = engine / 'src/libs/ui'
build = root / 'build/ui-pack-contract'


def run(command, **kwargs):
    result = subprocess.run([str(part) for part in command], cwd=root, **kwargs)
    if result.returncode:
        sys.exit(f'ui_pack_contract: {" ".join(str(part) for part in command)} failed ({result.returncode})')
    return result


# Without the render backend (engine STORM_UI_RENDER_BACKEND=OFF): the check draws nothing, so no bgfx, no host
# shaderc and no X11/OpenGL headers. A build directory configured before with the backend is reconfigured.
cache = build / 'CMakeCache.txt'
if not cache.is_file() or 'STORM_UI_RENDER_BACKEND:BOOL=OFF' not in cache.read_text():
    generator = [] if cache.is_file() else ['-G', 'Ninja'] if shutil.which('ninja') else []
    run(['cmake', '-S', ui, '-B', build, *generator, '-DCMAKE_BUILD_TYPE=Release', '-DSTORM_UI_RENDER_BACKEND=OFF', '-Wno-dev'],
        stdout=subprocess.DEVNULL)
run(['cmake', '--build', build, '--target', 'storm_ui_pack_check', '--parallel'], stdout=subprocess.DEVNULL)
parser = argparse.ArgumentParser()
parser.add_argument('--game', type=Path, help='an installed game: follow the taps into its scripts (game_bindings.py)')
parser.add_argument('--only', help='one case')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='storm-ui-pack-') as pack:
    run([sys.executable, root / 'mobile-ui/design/build.py', '--pack', pack, '--engine-root', engine], stdout=subprocess.DEVNULL)
    shards = 1 if args.only else max(1, os.cpu_count() or 1)
    # The host's own log lines go to stderr; the check prints every case and what is wrong with it.
    jobs = []
    for shard in range(shards):
        command = [build / 'storm_ui_pack_check', '--pack', pack, '--fixtures', ui / 'tests', '--shard', f'{shard}/{shards}',
                   '--actions', Path(pack) / f'actions-{shard}.jsonl']
        if args.only:
            command += ['--only', args.only]
        jobs.append(subprocess.Popen([str(part) for part in command], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True))
    failed, lines, summaries = False, [], []
    for job in jobs:
        out, _ = job.communicate()
        failed = failed or job.returncode != 0
        for line in out.splitlines():
            (summaries if line.startswith('pack contract:') else lines).append(line)
    print('\n'.join(line for line in lines if not line.startswith('PASS ')), flush=True)
    clean = total = problems = 0
    for line in summaries:  # "pack contract: C of T cases clean, P coverage/manifest problems"
        words = line.split()
        clean, total, problems = clean + int(words[2]), total + int(words[4]), problems + int(words[7])
    print(f'pack contract: {clean} of {total} cases clean, {problems} coverage/manifest problem{"" if problems == 1 else "s"}', flush=True)
    if args.game:
        actions = Path(pack) / 'actions.jsonl'
        actions.write_text(''.join(Path(pack, f'actions-{shard}.jsonl').read_text() for shard in range(shards)))
        bound = subprocess.run([sys.executable, Path(__file__).with_name('game_bindings.py'), '--actions', actions, '--game', args.game,
                                '--engine', engine], cwd=root)
        failed = failed or bound.returncode != 0
    else:
        print('game bindings: not followed into the game\'s scripts (no --game DIR)')
sys.exit(1 if failed else 0)
