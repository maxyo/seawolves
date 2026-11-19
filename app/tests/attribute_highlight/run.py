#!/usr/bin/env python3
"""Exercise production Hero routing and C-Table through real pointer taps.

Uses a caller-built application UI pack and storm_ui_shot. No template or input
behavior is copied into this fixture. storm_ui_shot renders with software GLES: no GPU.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--shot', type=Path, required=True)
    parser.add_argument('--engine-source', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='registered task directory for the fixture and current capture')
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    model = json.loads((args.engine_source / 'src/libs/ui/tests/models/hero.json').read_text())
    tables = [('special', 'TABLE_SPECIAL'), ('personal', 'TABLE_SKILL_1'), ('ship', 'TABLE_SKILL_2')]
    for _, node in tables:
        row = next(w for w in model['widgets'] if w['node'] == node and w['index'] == 0)
        model['widgets'].append({
            'node': 'ANDROID_SKILLHELP', 'type': 'fact', 'index': -1,
            'rowId': node + '_tr0', 'enabled': True,
            'context': {'node': node, 'skill': row['rowId'],
                        'text': 'Stock skill description fixture.\r\n~\r\nSecond paragraph.'}})
    fixture = args.work_dir / 'hero-help.json'
    fixture.write_text(json.dumps(model))

    def run(script, gallery=False):
        command = [str(args.shot.resolve()), '--pack', str(args.pack.resolve()),
                   '--size', '1200x700', '--density', '1',
                   '--out', str((args.work_dir / 'current.png').resolve()), '--script', '; '.join(script)]
        command += ['--doc', 'gallery/table.rml'] if gallery else ['--interface', str(fixture.resolve())]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.work_dir / 'last-run.log').write_text(result.stdout)
        assert result.returncode == 0, f'capture failed ({result.returncode}); see last-run.log'
        sections = []
        for line in result.stdout.splitlines():
            if line.startswith('bounds selector='):
                sections.append((line.removeprefix('bounds selector='), []))
            elif line.startswith('bounds col='):
                assert sections, 'bounds result without selector'
                match = re.search(r'x=([\d.-]+) y=([\d.-]+) w=([\d.-]+) h=([\d.-]+)', line)
                assert match, line
                sections[-1][1].append(tuple(map(float, match.groups())))
        return sections, result.stdout

    def tap(box):
        x, y, width, height = box
        # Stay in the name cell, away from skill increment/decrement arrows.
        x, y = x + min(24, width / 2), y + height / 2
        return [f'down 1 {x:.2f} {y:.2f}', f'up 1 {x:.2f} {y:.2f}']

    probe, _ = run([f'bounds #{name} .tb-row' for name, _ in tables])
    assert len(probe) == 3 and all(rows for _, rows in probe), 'all three skill tables must be visible'
    row_boxes = {name: probe[i][1][0] for i, (name, _) in enumerate(tables)}
    opened, _ = run(tap(row_boxes['special']) + ['bounds #dialog', 'bounds #skill-help-close'])
    assert len(opened) == 2 and all(boxes for _, boxes in opened), 'a pointer tap must open skill help and its close action'
    close_box = opened[1][1][0]

    script, expected = [], []

    def inspect(selector, count, label):
        script.append('bounds ' + selector)
        expected.append((selector, count, label))

    for name, _ in tables:
        for route in ('button', 'outside', 'back'):
            label = name + '/' + route
            script += tap(row_boxes[name])
            inspect('#dialog', 1, label + ': description opens')
            inspect('s-hero c-table .tb-row.sel', 0, label + ': help does not select a skill')
            if route == 'button':
                script += tap(close_box)
            elif route == 'outside':
                script += ['down 1 2 2', 'up 1 2 2']
            else:
                script.append('back')
            inspect('#dialog', 0, label + ': closes')
            inspect('s-hero c-table .tb-row.sel', 0, label + ': highlight cleared')
            script += tap(row_boxes[name])
            inspect('#dialog', 1, label + ': reopens')
            script.append('back')
            inspect('#dialog', 0, label + ': reopened help closes')
            inspect('s-hero c-table .tb-row.sel', 0, label + ': no stale selection after reopen')
    actual, output = run(script)
    assert len(actual) == len(expected), 'missing runtime observations'
    for (selector, boxes), (wanted, count, label) in zip(actual, expected):
        assert selector == wanted and len(boxes) == count, f'{label}: {selector} returned {boxes}'
    assert not re.search(r'^command widget:lscchar_all\b', output, re.M), 'description taps must not send gameplay actions'

    # Control: retain ordinary C-Table optimistic selection using the production gallery.
    control, _ = run(['bounds #tbl-wide .tb-row'], gallery=True)
    assert control and len(control[0][1]) >= 2, 'ordinary table control is visible'
    first = control[0][1][0]
    control, output = run(tap(first) + ['bounds #tbl-wide .tb-row.sel'], gallery=True)
    assert len(control) == 1 and len(control[0][1]) == 1, 'ordinary table retains one selection'
    assert control[0][1][0] == first, 'ordinary table selects the tapped first row'
    assert 'action tbl-wide row 0' in output, 'ordinary table retains its row event'
    print('PASS: 3 skill tables × button/outside/Back close and reopen; ordinary selection retained')


if __name__ == '__main__':
    main()
