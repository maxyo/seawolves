#!/usr/bin/env python3
"""Toasts have one place, the top edge, and no plate: the overlay has a single
toast stack pinned to the top, C-Notice has no bottom or top placement, and toast, log and
hint lines draw no background, frame or box shadow (only a text outline/glow/shadow). The bottom
hint sits in the land HUD between the move zone and the action buttons.
"""
from pathlib import Path
import json
import re

design = Path(__file__).resolve().parents[2] / 'mobile-ui/design'

overlay = (design / 'overlay.rml').read_text(encoding='utf-8')
stacks = re.findall(r'id="(toasts[^"]*)"', overlay)
assert stacks == ['toasts'], f'overlay.rml: one toast stack expected, got {stacks}'
base = (design / 'base.rcss').read_text(encoding='utf-8')
rule = re.search(r'#toasts \{([^}]*)\}', base)
assert rule and 'top:' in rule.group(1) and 'bottom' not in rule.group(1), 'base.rcss: #toasts is pinned to the top edge'

spec = json.loads(re.sub(r'@\{[^}]+\}', '0', (design / 'components/notice/spec.json').read_text(encoding='utf-8')))
assert spec['props']['place']['options'] == ['none', 'hud'], 'C-Notice places: none | hud (toasts never come up from the bottom)'
assert 'offsetBottom' not in spec['constants'] and 'offsetTop' not in spec['constants']
for path in [*design.glob('components/*/template.rml'), *design.glob('screens/*/template.rml'), *design.glob('gallery/*.rml')]:
    text = path.read_text(encoding='utf-8')
    assert not re.search(r"""place=["'](bottom|top)["']|place == 'bottom'""", text), f'{path.name}: a bottom/top toast placement'

style = re.sub(r'@\{[^}]+\}', 'T', (design / 'components/notice/style.rcss').read_text(encoding='utf-8'))  # tokens hold braces
pills = [m.group(0) for m in re.finditer(r'[^{}]*\.ntc-pill[^{}]*\{[^}]*\}', style)]
assert pills, 'no .ntc-pill rules'
for rule in pills:
    body = rule[rule.index('{'):]
    assert not re.search(r'background|border(?!-box)|box-shadow|decorator', body), f'a plate under toast/log/hint text: {rule.strip()}'
assert any('font-effect' in rule for rule in pills), 'toast text keeps an outline/shadow instead of a plate'

layout = json.loads((design / 'hud-layout.json').read_text(encoding='utf-8'))
hints = {name: e for name, scene in layout['scenes'].items() for e in scene['elements'] if e['id'] == 'hint'}
assert list(hints) == ['land'] and hints['land']['props']['variant'] == 'hint' and hints['land']['anchor'].endswith('-bottom'), \
    f'the bottom hint: land only, a c-notice hint at the bottom: {hints}'
print('PASS: one toast place (top), no plates under toast/log/hint text, the land hint at the bottom')
