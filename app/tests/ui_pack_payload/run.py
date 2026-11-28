#!/usr/bin/env python3
"""The UI pack as a release APK ships it: build.py's real pack collected by the packager's collect_ui_pack (gallery
off). Fails when the packager refuses a file of the pack, or leaves out a file pack.json names (its documents, fonts,
strings, pictures and the icon atlas with its pages).
"""
from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/android'))
from mobile_ui_assets import collect_ui_pack  # noqa: E402

try:
    files = collect_ui_pack(root, root.parent / 'engine', gallery=False)
except ValueError as refused:
    print(f'FAIL: the packager refuses the pack: {refused}')
    sys.exit(1)

pack = root / 'build/ui-pack'
index = json.loads((pack / 'pack.json').read_text(encoding='utf-8'))


def named(value):
    """Every string of pack.json that is a file of the built pack."""
    if isinstance(value, dict):
        for item in value.values():
            yield from named(item)
    elif isinstance(value, list):
        for item in value:
            yield from named(item)
    elif isinstance(value, str) and (pack / value).is_file():
        yield value


wanted = {name for name in named(index) if not name.startswith('gallery/')}
icons = index['icons']
wanted |= {f'{icons.rpartition("/")[0]}/{page["file"]}'
           for page in json.loads((pack / icons).read_text(encoding='utf-8'))['pages']}
missing = sorted(wanted - set(files))
if missing:
    print('FAIL: the packager leaves out files pack.json names:\n  ' + '\n  '.join(missing))
    sys.exit(1)
print(f'PASS: {len(files)} pack files packaged, {len(wanted)} named by pack.json among them')
