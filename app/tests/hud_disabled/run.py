#!/usr/bin/env python3
"""One disabled look for every HUD button: C-HudButton's own `dis` class (half
transparent, fully grey), whatever the button or its variant. Refused: a disabled rule for one
button or one variant, a HUD layout element that styles its own disabled state, and a button prop
the engine's HUD sets that the component does not declare (an undeclared prop marks the element
ui-error: the red stripes the shot button showed without a firearm, with its `reason`).
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
settings = (root / 'android/game/src/org/stormengine/port/GraphicsSettings.java').read_text(encoding='utf-8')
for step in re.search(r'HUD_TRANSPARENCIES\s*=\s*\{([^}]*)\}', settings)[1].split(','):
    if int(step) and f'.hud-buttons-{100 - int(step)} c-hud-button' not in style:
        problems.append(f'hud-button/style.rcss: no rule for «Прозрачность кнопок» {step} % (.hud-buttons-{100 - int(step)})')
for selector, body in re.findall(r'([^{}]+)\{([^}]*)\}', re.sub(r'/\*.*?\*/', '', style, flags=re.S)):
    selector = ' '.join(selector.split())
    if re.search(r'\.dis\b|disabled', selector):
        if not re.fullmatch(r'c-hud-button\.dis( \.disc)?', selector):
            problems.append(f'hud-button/style.rcss: a disabled look of its own: {selector}')
        if re.search(r'danger|wax|cross|decorator', body):
            problems.append(f'hud-button/style.rcss: the disabled look is only dimmed and grey: {selector} {{{body.strip()}}}')

layout = json.loads((design / 'hud-layout.json').read_text(encoding='utf-8'))
spec = json.loads(re.sub(r'@\{[^}]+\}', '0', (design / 'components/hud-button/spec.json').read_text(encoding='utf-8')))
declared = set(spec['props'])
buttons = {}
for name, scene in layout['scenes'].items():
    for element in scene['elements']:
        if element['component'] != 'c-hud-button':
            continue
        buttons.setdefault(element['id'], name)
        for key in ('style', 'rcss', 'disabledStyle', 'class'):
            if key in element:
                problems.append(f'hud-layout.json {name}/{element["id"]}: its own "{key}" (the disabled look is the component\'s)')
        for key in {**element.get('props', {}), **element.get('sample', {})}:
            if key not in declared:
                problems.append(f'hud-layout.json {name}/{element["id"]}: prop "{key}" is not a C-HudButton prop')

# What the engine's HUD scenes set on those buttons: view.elements["<id>"] = Json{{"key", …}, …}
engine = Path(sys.argv[1]) if len(sys.argv) > 1 else root.parent / 'engine'
for source in sorted((engine / 'src/libs/ui/src/hud').glob('*.cpp')):
    text = source.read_text(encoding='utf-8')
    for element, body in re.findall(r'view\.elements\["([\w-]+)"\]\s*=\s*Json\{(.*?)\};', text, flags=re.S):
        if element not in buttons:
            continue
        for key in re.findall(r'\{"(\w+)",', body):
            if key not in declared:
                problems.append(f'{source.name}: HUD button "{element}" gets prop "{key}", which C-HudButton does not declare')

if problems:
    print('FAIL:\n  ' + '\n  '.join(problems))
    sys.exit(1)
print(f'PASS: one disabled look for {len(buttons)} HUD buttons; every prop the layout and the engine set is declared')
