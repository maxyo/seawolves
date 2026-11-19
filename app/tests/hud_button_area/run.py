#!/usr/bin/env python3
"""The land HUD's button area is one place in peace (explore) and in combat (melee): the same
rect from the same source (hud-layout.json "areas"), every button of either set inside it, and
the buttons both sets have (abilities, weapon) at the same spot. The actions button is at the top in both
(where the sea orders button is), out of the area; the transitions (travel) button is in the area in peace
only. Uses the pack build's own placement (mobile-ui/design/build.py build_hud) on the real hud-layout.json.
"""
from pathlib import Path
import importlib.util
import json
import re
import sys

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('design_build', root / 'mobile-ui/design/build.py')
build = importlib.util.module_from_spec(spec)
sys.modules['design_build'] = build
spec.loader.exec_module(build)

layout_text, _ = build.build_hud(build.load_tokens())
layout = json.loads(layout_text)
area = layout['areas']['actions']
scenes = {name: {e['id']: e for e in layout['scenes'][name]['elements']} for name in ('land', 'land-combat')}


def box(element):
    """right, bottom, width, height in dp from its rcss (right-bottom elements)."""
    values = dict(re.findall(r'(right|bottom|width|height): (-?[\d.]+)dp', element['rcss']))
    return tuple(float(values[k]) for k in ('right', 'bottom', 'width', 'height'))


rect = (area['right'], area['bottom'], area['width'], area['height'])
for name, elements in scenes.items():
    members = [e for e in elements.values() if e.get('area') == 'actions']
    assert members, f'{name}: no element in the button area'
    for element in members:
        right, bottom, width, height = box(element)
        assert rect[0] <= right and right + width <= rect[0] + rect[2], f'{name}/{element["id"]}: outside the area horizontally'
        assert rect[1] <= bottom and bottom + height <= rect[1] + rect[3], f'{name}/{element["id"]}: outside the area vertically'
    # The set fills the area from its corner, as in peace: the bottom-right button sits in it.
    corner = min(members, key=lambda e: box(e)[0] + box(e)[1])
    assert box(corner)[0] - rect[0] <= 8 and box(corner)[1] - rect[1] <= 8, f'{name}: the area corner is empty ({corner["id"]})'
for common in ('abilities', 'weapon'):
    assert box(scenes['land'][common]) == box(scenes['land-combat'][common]), f'{common} moves between explore and melee'
assert scenes['land']['actions']['rcss'] == scenes['land-combat']['actions']['rcss'] and 'area' not in scenes['land']['actions'], \
    'the actions button is one place at the top in explore and melee'
assert scenes['land']['travel'].get('area') == 'actions' and 'travel' not in scenes['land-combat'], 'the transitions: in the area, in peace only'
print(f'PASS: land button area {rect} (right, bottom, width, height dp) is the same in explore and melee; '
      'both sets inside it, from its corner; abilities/weapon in place, actions at the top, transitions in peace')
