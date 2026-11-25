#!/usr/bin/env python3
"""The interface scripts (mobile-ui/PROGRAM) handle every script event the engine raises
(engine/tools/android/app_contract.json script_events) and take each argument the engine passes. No Android: the
scripts' text only.
"""
import json
from pathlib import Path
import re
import sys

app = Path(__file__).resolve().parents[2]
manifest = json.loads((app.parent / 'engine/tools/android/app_contract.json').read_text())
problems = []

scripts = {path: path.read_text(encoding='utf-8', errors='replace') for path in sorted((app / 'mobile-ui/PROGRAM').rglob('*.c'))}
for event, arguments in sorted(manifest['script_events'].items()):
    handlers = [path for path, source in scripts.items() if re.search(rf'#event_handler\(\s*"{event}"', source)]
    if not handlers:
        problems.append(f'script event {event!r}: no #event_handler in mobile-ui/PROGRAM')
    for argument in arguments:
        if not any(f'"{argument}"' in scripts[path] for path in handlers):
            problems.append(f'script event {event!r}: its handler does not take the argument {argument!r}')

if problems:
    sys.exit('ENGINE CONTRACT: the interface scripts and engine/tools/android/app_contract.json differ:\n  '
             + '\n  '.join(problems))
print(f'PASS: engine contract: {len(manifest["script_events"])} script events are handled by the interface scripts')
