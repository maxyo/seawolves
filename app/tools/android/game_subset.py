#!/usr/bin/env python3
"""The part of a game copy a scenario of the on-device driver needs, packed as the OBB the driver installs.

    game_subset.py --game <game root> --trace <strace log> [--trace ...] --save <save file> \
        --out <dir>/main.<versionCode>.com.dustoff.seawolves.obb

The subset is every file of the game root the engine opened in the traced runs (a host engine run under
`strace -f -e trace=openat,open,stat,newfstatat,statx` of the same scenario: boot, title, the save loaded), plus:

- the whole PROGRAM tree, RESOURCE/INI, RESOURCE/shared (the scripts' headers), RESOURCE/techniques, engine.ini and
  options (the APK's own layers go over techniques and shared as on a full import);
- the whole directories of what the game picks at random (WHOLE below: the title's ship, captain and weather, the
  citizens of a town and their animations, ambient and weather sounds, the sea scene's island and gear), which one
  traced run cannot cover: a model the engine does not find stalls the load;
- every texture the included models name.

The save goes to storm/SaveData/player/ (the engine's save data on Android). Layout of the ZIP: game/ = the game
root, storm/ = the app's files/storm. Check a subset with a host run on a tree of only these files: `strace` must
show no ENOENT under resource/.

These are your own game files: the OBB is only ever written outside the repository, pushed to your own device by
tools/android/emu_smoke.py (android/game/TESTING.md), and is never part of an APK.
"""
import argparse
import os
import re
import sys
import zipfile
from pathlib import Path

CALL = re.compile(r'(openat|open|stat|newfstatat|statx)\((?:AT_FDCWD, |\d+, )?"([^"]+)".*\) = (-?\d+)')
ALWAYS = ('engine.ini', 'options')
TREES = ('PROGRAM', 'RESOURCE/INI', 'RESOURCE/shared', 'RESOURCE/techniques')
# Random picks of the game's scripts (interface/mainmenu.c MainMenu_CreateShip: a random ship type and captain,
# ICreateWeather: a random weather; town citizens: random character models with their animations).
WHOLE = ('RESOURCE/MODELS/Ships', 'RESOURCE/Textures/Ships', 'RESOURCE/MODELS/Characters', 'RESOURCE/Textures/Characters',
         'RESOURCE/animation', 'RESOURCE/Textures/WEATHER', 'RESOURCE/MODELS/MainMenu', 'RESOURCE/Textures/MainMenu',
         'RESOURCE/Sounds/Ambient', 'RESOURCE/Sounds/Weather',
         # The world map scenario: the map, its sea and ships, encounters (picked at random).
         'RESOURCE/MODELS/WorldMap', 'RESOURCE/Textures/WorldMap',
         # The sea scenario (the save «Мартиника - море»): the island, the islands' textures and coast foam, what a ship
         # at sea shows and fires, its interface and sounds.
         'RESOURCE/MODELS/Islands/Martinique', 'RESOURCE/Textures/Islands', 'RESOURCE/foam/islands', 'RESOURCE/MODELS/Ammo',
         'RESOURCE/Textures/Ammo', 'RESOURCE/Textures/flags', 'RESOURCE/Textures/BATTLE_INTERFACE', 'RESOURCE/MODELS/Weather',
         'RESOURCE/MODELS/Tornado', 'RESOURCE/MODELS/SwimGoods', 'RESOURCE/MODELS/Animals', 'RESOURCE/MODELS/Particles',
         'RESOURCE/Particles', 'RESOURCE/Sounds/Ship', 'RESOURCE/Sounds/Sea Battles')


def traced(game, trace):
    """Game-relative paths of the regular files the traced engine opened or looked up successfully."""
    root = os.path.realpath(game) + os.sep
    found = set()
    for line in open(trace, errors='replace'):
        match = CALL.search(line)
        if not match or int(match.group(3)) < 0:
            continue
        path = match.group(2)
        # The traced engine ran in a copy of the game root (same relative layout): map by the part below it.
        path = os.path.normpath(path if path.startswith('/') else os.path.join(root, path))
        for marker in ('/PROGRAM/', '/RESOURCE/'):
            if marker in path:
                found.add(path[path.index(marker) + 1:])
                break
    return {rel for rel in found if (Path(game) / rel).is_file()}


def model_textures(game, files):
    """RESOURCE/Textures files named inside the subset's .gm models (a ship's deck, cannons, sails live at the
    top of Textures/, not under its own folder): the engine finds them by name when the model loads."""
    textures = {}
    for top in ('RESOURCE/Textures', 'RESOURCE/TEXTURES'):
        if (game / top).is_dir():
            textures.update({f.name.lower(): str(f.relative_to(game)) for f in (game / top).iterdir() if f.is_file()})
    named = set()
    for rel in files:
        if rel.lower().endswith('.gm'):
            for name in re.findall(rb'([A-Za-z0-9_\-]+\.(?:tga|gif))', (game / rel).read_bytes(), re.IGNORECASE):
                path = textures.get(name.decode().lower() + '.tx')
                if path:
                    named.add(path)
    return named


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--trace', action='append', required=True)
    p.add_argument('--save', type=Path, action='append', default=[])
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--base', type=Path, help='an earlier OBB of this subset: write only what it lacks (a patch.<vc>.<package>.obb)')
    a = p.parse_args()
    if not (a.game / 'engine.ini').is_file():
        sys.exit(f'{a.game} is not a game root (no engine.ini)')
    if Path(os.path.realpath(a.out)).is_relative_to(Path(__file__).resolve().parents[2]):
        sys.exit('The game subset must not be written into the repository')
    files = set(ALWAYS)
    for tree in TREES + WHOLE:
        files |= {str(f.relative_to(a.game)) for f in (a.game / tree).rglob('*') if f.is_file()}
    for trace in a.trace:
        files |= traced(a.game, trace)
    files |= model_textures(a.game, files)
    saves = a.save
    if a.base:
        with zipfile.ZipFile(a.base) as base:
            have = set(base.namelist())
        files = {rel for rel in files if 'game/' + rel not in have}
        saves = [save for save in saves if 'storm/SaveData/player/' + save.name not in have]
    total = 0
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.out, 'w', zipfile.ZIP_STORED, allowZip64=True) as z:
        for rel in sorted(files):
            source = a.game / rel
            total += source.stat().st_size
            z.write(source, 'game/' + rel)
        for save in saves:
            z.write(save, 'storm/SaveData/player/' + save.name)
            total += save.stat().st_size
    print(f'{a.out}: {len(files)} game files + {len(saves)} save(s), {total / 1e6:.1f} MB '
          f'(OBB {a.out.stat().st_size / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
