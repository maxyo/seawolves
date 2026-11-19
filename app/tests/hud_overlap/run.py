#!/usr/bin/env python3
"""No two HUD elements of a scene overlap, on every screen of build.py HUD_CHECK_SCREENS and with
every party list at its maximum (mobile-ui/design/build.py check_hud_overlap on the real
hud-layout.json), and the check catches an overlap (the block button over the party; a marker under
the sea top row, where the event band drops on a narrow screen). A floating stick's zone listed
first lies under the others (the deck walk zone under the helm and the squadron); listed after
them it overlaps, and its stick at rest must stay clear of them and inside its zone.
"""
from pathlib import Path
import copy
import importlib.util
import json
import sys

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('design_build', root / 'mobile-ui/design/build.py')
build = importlib.util.module_from_spec(spec)
sys.modules['design_build'] = build
spec.loader.exec_module(build)

tokens = build.load_tokens()
design = root / 'mobile-ui/design'
layout = json.loads(build.substitute((design / 'hud-layout.json').read_text(encoding='utf-8'), tokens, 'rcss', 'hud-layout.json'))
notice = json.loads(build.substitute((design / 'components/notice/spec.json').read_text(encoding='utf-8'), tokens, 'rcss', 'notice'))['constants']
rest = build.hud_rest_specs(tokens)

found = build.check_hud_overlap(layout, notice, rest)
assert not found, 'HUD overlaps:\n  ' + '\n  '.join(found)

# The check is not blind: the block button moved into the top-left corner must be caught.
broken = copy.deepcopy(layout)
block = next(e for e in broken['scenes']['land-combat']['elements'] if e['id'] == 'block')
block.update({'anchor': 'left-top', 'left': 12, 'top': 10})
block.pop('bottom', None)
assert any('block' in line for line in build.check_hud_overlap(broken, notice, rest)), 'an overlapping block button was not caught'
# The dropped sea band is checked where it is drawn: below its minimum width (640) it lies under the
# top row, so a marker there collides with it; on 915 the band stays pinned to the top and the marker is free.
marked = copy.deepcopy(layout)
marked['scenes']['sea']['elements'].append({'id': 'marker', 'component': 'c-hud-plate', 'anchor': 'left-top',
                                             'left': 200, 'top': 120, 'width': 40, 'height': 20})
hits = [line for line in build.check_hud_overlap(marked, notice, rest) if 'marker' in line]
assert hits and all('640' in line for line in hits), f'the dropped sea band is not checked: {hits}'
# The deck walk zone lies under the helm and the squadron because it comes first (drawn under them) ...
walk = next(e for e in layout['scenes']['sea']['elements'] if e['id'] == 'walk')
assert layout['scenes']['sea']['elements'][0] is walk and walk['props']['floating'], 'the deck walk zone is the first sea element'
late = copy.deepcopy(layout)
elements = late['scenes']['sea']['elements']
elements.append(elements.pop(0))
assert any('helm overlaps walk' in line for line in build.check_hud_overlap(late, notice, rest)), 'a zone drawn over the helm was not caught'
# ... and its stick at rest must not lie over the helm, nor outside the zone.
resting = copy.deepcopy(layout)
next(e for e in resting['scenes']['sea']['elements'] if e['id'] == 'walk')['props']['restBottom'] = 27
assert any('walk control at rest overlaps helm' in line for line in build.check_hud_overlap(resting, notice, rest)), 'a resting stick over the helm was not caught'
next(e for e in resting['scenes']['sea']['elements'] if e['id'] == 'walk')['props'].update({'restBottom': 99, 'restLeft': 400})
assert any('walk control at rest leaves its zone' in line for line in build.check_hud_overlap(resting, notice, rest)), 'a resting stick outside its zone was not caught'
# The sea helm and sail lever zones are `clear`: checked as whole boxes, so a zone grown into a
# neighbour (the abilities row on the narrowest phone, the ammunition fan) is caught on every screen
# where it meets it; without `clear` the same zone would count as lying under them.
def sea(changed, id, **update):
    element = next(e for e in changed['scenes']['sea']['elements'] if e['id'] == id)
    props = update.pop('props', {})
    element.update(update)
    element['props'].update(props)
    return element

grown = copy.deepcopy(layout)
sea(grown, 'helm', width='36%')
sea(grown, 'sail', width=86)
lines = build.check_hud_overlap(grown, notice, rest)
assert '640x360 sea: helm overlaps abilities-4' in lines and '915x412 sea: sail overlaps ammo-3' in lines, f'grown sea zones were not caught: {lines}'
sea(grown, 'helm').pop('clear')
assert not any('helm overlaps' in line for line in build.check_hud_overlap(grown, notice, rest)), 'a zone without clear lies under the others'
# Their controls at rest stay inside the zones.
moved = copy.deepcopy(layout)
sea(moved, 'helm', props={'restLeft': 100})
sea(moved, 'sail', props={'restInset': 40})
lines = build.check_hud_overlap(moved, notice, rest)
assert '640x360 sea: the helm control at rest leaves its zone' in lines and '640x360 sea: the sail control at rest leaves its zone' in lines, \
    f'resting helm / lever outside their zones were not caught: {lines}'
# The world map's helm and sail lever float the same way (the continuous lever only shifts under the thumb):
# clear zones too, so one grown into the details plate or the action buttons is caught.
def world(changed, id, **update):
    element = next(e for e in changed['scenes']['world-map']['elements'] if e['id'] == id)
    assert element['props'].get('floating') and element.get('clear'), f'the world-map {id} is not a clear floating zone: {element}'
    element.update(update)
    return element

grown = copy.deepcopy(layout)
world(grown, 'helm', height=100)
world(grown, 'sail', bottom=60)
lines = build.check_hud_overlap(grown, notice, rest)
assert any('world-map: helm overlaps details' in line for line in lines) and any('world-map: sail overlaps context' in line for line in lines), \
    f'grown world-map zones were not caught: {lines}'
print(f'PASS: no HUD element overlaps another in {len(layout["scenes"])} scenes on {len(build.HUD_CHECK_SCREENS)} screens with full parties; '
      'an overlap is caught, the dropped sea band is checked, a floating zone lies under the others only when drawn first, its resting stick is checked, the clear sea and world-map zones are checked whole')
