#!/usr/bin/env python3
"""Check the actual items action button through pointer hold/cancel/drag.

Uses the production app pack and screen model. Requires a serialized GPU slot.
The retained warning.png needs visual review for exact translated warning text;
bounds establish that the warning above the actual button is laid out, not its wording.
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
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    model = json.loads((args.engine_source / 'src/libs/ui/tests/models/items.json').read_text())
    row_id = next(w['rowId'] for w in model['widgets'] if w['node'] == 'TABLE_ITEMS' and w['index'] == 0)
    for widget in model['widgets']:
        if widget['node'] == 'ANDROID_ITEMS' and widget['index'] == 0:
            widget['context'] = {'slot': 'amulet', 'worn': '1', 'quest': '0', 'map': '0',
                                 'action': 'amulet', 'days': '70', 'remain': '64', 'itemId': 'fixture-timed-amulet'}
    fixture = args.work_dir / 'items-timed-amulet.json'
    fixture.write_text(json.dumps(model))
    settings = json.loads((args.pack / 'pack.json').read_text())['settings']
    hold = settings['holdConfirmMs'] / 1000

    def run(name, script):
        command = [str(args.shot.resolve()), '--pack', str(args.pack.resolve()),
                   '--interface', str(fixture.resolve()), '--size', '1200x700', '--density', '1',
                   '--out', str((args.work_dir / ('warning.png' if name == 'probe' else 'current.png')).resolve()),
                   '--script', '; '.join(script)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.work_dir / 'last-run.log').write_text(result.stdout)
        assert result.returncode == 0, f'{name}: capture failed; see last-run.log'
        return result.stdout

    probe = run('probe', ['bounds #item-equip', 'bounds #item-removal-warning'])
    sections = probe.split('bounds selector=')[1:]
    assert len(sections) == 2, 'action button and warning observations are required'
    boxes = []
    for section in sections:
        match = re.search(r'bounds col=.*?x=([\d.-]+) y=([\d.-]+) w=([\d.-]+) h=([\d.-]+)', section)
        assert match, 'timed amulet action and consumption warning must both be visible'
        boxes.append(tuple(map(float, match.groups())))
    x, y, width, height = boxes[0]
    x, y = x + width / 2, y + height / 2
    down, up = f'down 1 {x:.2f} {y:.2f}', f'up 1 {x:.2f} {y:.2f}'
    cases = [
        ('short-tap', [down, f'wait {hold / 5}', up, f'wait {hold + 0.2}'], 0),
        ('cancel', [down, f'wait {hold / 5}', 'cancel', f'wait {hold + 0.2}', up], 0),
        ('drag-out', [down, 'move 1 2 2', f'wait {hold + 0.2}', 'up 1 2 2'], 0),
        ('full-hold', [down, f'wait {hold + 0.2}', f'wait {hold + 0.2}', up, f'wait {hold + 0.2}'], 1),
    ]
    for name, script, count in cases:
        output = run(name, script)
        commands = [line for line in output.splitlines() if line.startswith('command widget:items\t')]
        assert len(commands) == count, f'{name}: expected {count} gameplay commands, got {commands}'
        if commands:
            assert '\tEQUIP_BUTTON\t-1\tpress@' in commands[0] and commands[0].endswith(':' + row_id), \
                f'{name}: removal must target the selected guarded row: {commands}'
    print('PASS: removal warning has bounds; short tap/cancel/drag send nothing; full hold sends once')
    print('Exact warning wording still requires visual review of warning.png.')


if __name__ == '__main__':
    main()
