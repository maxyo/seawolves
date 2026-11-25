#!/usr/bin/env python3
"""The HUD's fleet column (C-HudPlate variant fleet) on the world map and at sea: every ship's
picture sits exactly in the middle of its rings and the members stand in straight columns at an
even step (a short screen wraps the column and must not shift the pictures). The UI ignores the display cutout, so nothing moves the rings apart from their picture.

Real pack, real HUD documents and the engine's recorded states, captured by storm_ui_shot
(software GLES, no GPU). Phones of 780x360, 853x384 (densities 2.34 and 3.75) and 915x412 dp.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

EPSILON = 0.02  # dp: what the printed boxes round away
SCREENS = [((853, 384), 3.75), ((853, 384), 2.34), ((780, 360), 2.75), ((915, 412), 2.625)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--shot', type=Path, required=True)
    parser.add_argument('--engine-source', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='registered task directory for the fixtures and the last capture')
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    states = args.engine_source / 'src/libs/ui/tests/states'
    world = json.loads((states / 'world-map.json').read_text())
    pictures = [[0.625, 0, 0.6875, 0.0625], [0.5, 0, 0.5625, 0.0625]]
    for index, (hull, sails, crew) in enumerate([(95.0, 100.0, 200), (41.0, 70.0, 60)]):
        world['ships'].append({'id': f'9:Fixture:{index}', 'name': f'Fixture {index}', 'hull': hull, 'sails': sails,
                               'crew': crew, 'crewMax': 200, 'shipClass': 5, 'type': 'Brig',
                               'picture': {'texture': 'battle_interface/ship_icons2', 'uv': pictures[index]}})
    world_file = args.work_dir / 'world-map-4.json'
    world_file.write_text(json.dumps(world, ensure_ascii=False))
    scenes = {'world-map': [f'gameplay={states / "gameplay-worldmap.json"}', f'worldMap={world_file}'],
              'sea': [f'gameplay={states / "gameplay-sailing.json"}', f'ship={states / "ship.json"}']}

    def run(scene, size, density, script):
        command = [str(args.shot.resolve()), '--pack', str(args.pack.resolve()), '--size', f'{size[0]}x{size[1]}',
                   '--density', str(density), '--out', str((args.work_dir / 'current.png').resolve()),
                   '--script', '; '.join(script)]
        for state in scenes[scene]:
            command += ['--state', state]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.work_dir / 'last-run.log').write_text(result.stdout)
        assert result.returncode == 0, f'capture failed ({result.returncode}); see {args.work_dir / "last-run.log"}'
        sections = []
        for line in result.stdout.splitlines():
            if line.startswith('bounds selector='):
                sections.append((line.removeprefix('bounds selector='), []))
            elif line.startswith('bounds col='):
                match = re.search(r'x=([\d.-]+) y=([\d.-]+) w=([\d.-]+) h=([\d.-]+)', line)
                assert match and sections, line
                sections[-1][1].append(tuple(map(float, match.groups())))
        return sections

    def centre(box):
        x, y, w, h = box
        return x + w / 2, y + h / 2

    checked = 0
    for scene in scenes:
        for size, density in SCREENS:
            where = f'{scene} {size[0]}x{size[1]} @{density}'
            members = run(scene, size, density, ['bounds #fleet .member'])[0][1]
            assert len(members) >= 2, f'{where}: the fleet shows {len(members)} ship(s)'
            script = []
            for k in range(1, len(members) + 1):
                script += [f'bounds #fleet .member:nth-child({k}) ui-ring', f'bounds #fleet .member:nth-child({k}) .gi-box']
            parts = run(scene, size, density, script)
            for k, member in enumerate(members):
                rings, picture = parts[2 * k][1], parts[2 * k + 1][1]
                assert rings and len(picture) == 1, f'{where}: ship {k + 1}: {len(rings)} rings, {len(picture)} pictures'
                px, py = centre(picture[0])
                for r, ring in enumerate(rings):
                    rx, ry = centre(ring)
                    assert abs(rx - px) <= EPSILON and abs(ry - py) <= EPSILON, \
                        f'{where}: ship {k + 1}, ring {r + 1} centred at ({rx:.2f}, {ry:.2f}), its picture at ({px:.2f}, {py:.2f})'
                    assert r == 0 or ring[2] < rings[r - 1][2], f'{where}: ship {k + 1}: ring {r + 1} is not inside ring {r}'
                mx, my = centre(member)
                assert abs(mx - px) <= EPSILON and abs(my - py) <= EPSILON, f'{where}: ship {k + 1}: the picture is off its cell'
                assert picture[0][2] < rings[-1][2], f'{where}: ship {k + 1}: the picture is not inside its rings'
            # A short screen wraps the column into further columns (hud-layout.json: the sea fleet ends above the
            # spyglass button): each one straight, the columns side by side at an even step, filled top-down in order.
            lefts = sorted({round(m[0], 2) for m in members})
            gaps = {round(b - a, 2) for a, b in zip(lefts, lefts[1:])}
            assert len(gaps) <= 1, f'{where}: the columns are crooked (cell lefts {lefts})'
            order = [lefts.index(round(m[0], 2)) for m in members]
            assert order == sorted(order), f'{where}: the ships do not fill the columns in order (columns {order})'
            for left in lefts:
                column = [m for m in members if abs(m[0] - left) <= EPSILON]
                steps = {round(b[1] - a[1], 2) for a, b in zip(column, column[1:])}
                assert len(steps) <= 1, f'{where}: uneven column step {sorted(steps)}'
                assert abs(column[0][1] - members[0][1]) <= EPSILON, f'{where}: a column does not start at the top'
            checked += len(members)
    print(f'PASS: {checked} fleet cells on the world map and at sea, {len(SCREENS)} screens: '
          'every picture centred in its rings, straight even columns')

if __name__ == '__main__':
    main()
