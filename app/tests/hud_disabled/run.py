#!/usr/bin/env python3
"""HUD buttons draw what the layout gives them: every «Прозрачность кнопок» step has its rule, no layout element
styles itself, and every prop the layout sets is declared by C-HudButton (an undeclared prop marks the element
ui-error: red stripes). The props the engine's HUD sets are checked by running it: tests/ui_pack_contract.
"""
from pathlib import Path
import json
import re
import sys

root = Path(__file__).resolve().parents[2]
design = root / 'mobile-ui/design'
problems = []

style = (design / 'components/hud-button/style.rcss').read_text(encoding='utf-8')
# «Прозрачность кнопок»: the UI host sets hud-buttons-<100 - transparency> for every step but 0; each needs its rule.
engine = Path(sys.argv[1]) if len(sys.argv) > 1 else root.parent / 'engine'
contract = json.loads((engine / 'tools/android/app_contract.json').read_text(encoding='utf-8'))
for step in contract['settings']['steps']['hud_transparency']['values']:
    if step and f'.hud-buttons-{100 - int(step)} c-hud-button' not in style:
        problems.append(f'hud-button/style.rcss: no rule for «Прозрачность кнопок» {step} % (.hud-buttons-{100 - int(step)})')

layout = json.loads((design / 'hud-layout.json').read_text(encoding='utf-8'))
spec = json.loads(re.sub(r'@\{[^}]+\}', '0', (design / 'components/hud-button/spec.json').read_text(encoding='utf-8')))
declared = set(spec['props'])
buttons = set()
for name, scene in layout['scenes'].items():
    for element in scene['elements']:
        if element['component'] != 'c-hud-button':
            continue
        buttons.add(element["id"])
        for key in ('style', 'rcss', 'disabledStyle', 'class'):
            if key in element:
                problems.append(f'hud-layout.json {name}/{element["id"]}: its own "{key}" (the look is the component\'s)')
        for key in {**element.get('props', {}), **element.get('sample', {})}:
            if key not in declared:
                problems.append(f'hud-layout.json {name}/{element["id"]}: prop "{key}" is not a C-HudButton prop')

if problems:
    print('FAIL:\n  ' + '\n  '.join(problems))
    sys.exit(1)
print(f'PASS: {len(buttons)} HUD buttons: transparency steps styled, every prop the layout sets is declared')
