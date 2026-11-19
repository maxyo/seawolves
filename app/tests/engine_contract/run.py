#!/usr/bin/env python3
"""The application side of the engine contract: the Java sources and the interface scripts accept, send,
handle and declare exactly what engine/tools/android/app_contract.json says (engine/tests/app_contract checks
the engine's own sources against the same file).

Checked: the JNI natives per class and the Java methods the engine looks up (name, JNI descriptor, static);
the engine commands the activity's mailbox sends; the HUD input and host actions the activity handles; the
UI state channels it writes; the settings and debug overrides GraphicsSettings stores (each value the settings
screen offers inside its range); the script events of mobile-ui/PROGRAM; the real GameplayShortcuts queue takes
exactly the engine's shortcuts. No Android: text and one javac run.
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

app = Path(__file__).resolve().parents[2]
engine = app.parent / 'engine'
manifest = json.loads((engine / 'tools/android/app_contract.json').read_text())
port = app / 'android/game/src/org/stormengine/port'
problems = []


def strip_comments(source):
    """Java source without comments; string and char literals kept."""
    out, i, n = [], 0, len(source)
    while i < n:
        c = source[i]
        if source.startswith('//', i):
            i = source.find('\n', i)
            i = n if i < 0 else i
        elif source.startswith('/*', i):
            i = source.find('*/', i + 2)
            i = n if i < 0 else i + 2
        elif c in '"\'':
            j = i + 1
            while j < n and source[j] != c:
                j += 2 if source[j] == '\\' else 1
            out.append(source[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


java = {path.stem: strip_comments(path.read_text(encoding='utf-8')) for path in sorted(port.glob('*.java'))}


def block(source, head):
    """The braces-delimited body that follows `head` in source."""
    start = source.index(head)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += {'{': 1, '}': -1}.get(source[end], 0)
        end += 1
    return source[opening:end]


def same(label, found, declared):
    found, declared = set(found), set(declared)
    for item in sorted(found - declared, key=str):
        problems.append(f'{label}: {item!r} is in the application but not in the engine contract')
    for item in sorted(declared - found, key=str):
        problems.append(f'{label}: {item!r} is in the engine contract but not in the application')


def literal(value):
    return value.encode('utf-8').decode('unicode_escape')


# JNI natives: per class (the file of the top-level class) == the engine's exports.
natives = {(cls, name) for cls, source in java.items()
           for name in re.findall(r'\bnative\s+[\w\[\]<>.]+\s+(\w+)\s*\(', source)}
same('JNI native method', natives, {(cls, name) for cls, names in manifest['jni']['natives'].items() for name in names})

# JNI upcalls: the method exists in its class with the engine's descriptor.
DESCRIPTORS = {'boolean': 'Z', 'byte': 'B', 'char': 'C', 'short': 'S', 'int': 'I', 'long': 'J', 'float': 'F',
               'double': 'D', 'void': 'V', 'String': 'Ljava/lang/String;'}
METHOD = re.compile(r'((?:(?:public|protected|private|static|final|synchronized|abstract)\s+)*)'
                    r'([\w.]+(?:\[\])*)\s+(\w+)\s*\(([^)]*)\)\s*(?:throws [\w., ]+)?\{?')


def descriptor(java_type):
    dims = java_type.count('[]')
    base = java_type.replace('[]', '').strip()
    if base not in DESCRIPTORS:
        raise ValueError(f'type {java_type} has no descriptor in this check (add it to DESCRIPTORS)')
    return '[' * dims + DESCRIPTORS[base]


for upcall in manifest['jni']['upcalls']:
    cls, name, wanted = upcall[:3]
    static = 'static' in upcall[3:]
    outer, _, inner = cls.partition('.')
    if outer not in java:
        problems.append(f'JNI upcall {cls}.{name}: no class {outer} in {port.relative_to(app)}')
        continue
    scope = java[outer]
    if inner:
        nested = re.search(rf'\b(?:interface|class)\s+{inner}\b', scope)
        if not nested:
            problems.append(f'JNI upcall {cls}.{name}: {outer} has no nested {inner}')
            continue
        scope = block(scope, nested[0])
    found = []
    for modifiers, returns, method, params in METHOD.findall(scope):
        if method != name:
            continue
        try:
            types = [re.sub(r'\b(final)\s+', '', p).strip().rsplit(None, 1)[0] for p in params.split(',') if p.strip()]
            found.append(('(' + ''.join(map(descriptor, types)) + ')' + descriptor(returns), 'static' in modifiers))
        except ValueError as unsupported:
            problems.append(f'JNI upcall {cls}.{name}: {unsupported}')
    if (wanted, static) not in found:
        problems.append(f'JNI upcall {cls}.{name}{wanted}{" (static)" if static else ""}: the application declares '
                        + (', '.join(f'{d}{" static" if s else ""}' for d, s in found) or 'no such method'))

# Engine commands the activity sends: every mailbox literal is one the engine takes, every command the engine
# expects from the application is sent.
commands = manifest['engine_commands']
sent = set()
for source in java.values():
    sent |= {literal(v) for v in re.findall(r'\bqueueMobileAction\(\s*"([^"]*)"', source)}
for producer in ('AudioFocusState', 'GameplayShortcuts', 'LocationReadiness'):  # build mailbox messages queued elsewhere
    sent |= {literal(v) for v in re.findall(r'"([a-z][a-z-]*:[^"]*)"\s*\+', java[producer])}
for message in sorted(sent):
    if not any(message.startswith(c) for c in commands):
        problems.append(f'the activity sends {message!r}, which no engine command takes')
for command, sender in sorted(commands.items()):
    if 'app' in sender.split('+') and not any(m.startswith(command) or command.startswith(m) for m in sent):
        problems.append(f'engine command {command!r}: the engine expects the application to send it; it does not')

# HUD input and host actions the activity handles.
activity = java['GameActivity']
same('HUD input handled (GameActivity.hudInput)', re.findall(r'verb\.equals\("(\w+)"\)', block(activity, 'void hudInput(')),
     manifest['hud_host_input'])
same('host action handled (GameActivity.onUiAction)', re.findall(r'node\.equals\("([\w-]+)"\)', block(activity, 'void onUiAction(')),
     manifest['host_actions'])

# State channels the hosts write.
written = set()
for source in java.values():
    written |= set(re.findall(r'\bsetState\(\s*"(\w+)"', source))
same('UI state channel written', written, {c for channels in manifest['state_channels'].values() for c in channels})

# Settings: the keys and debug overrides GraphicsSettings stores, the facts it reports, the offered values in range.
graphics = java['GraphicsSettings']
settings = manifest['settings']
same('application setting (GraphicsSettings.KEYS)', re.findall(r'"(\w+)"', re.search(r'KEYS\s*=\s*\{([^}]*)\}', graphics)[1]),
     settings['keys'])
debug = {key: (int(low), int(high)) for key, low, high in
         re.findall(r'\{"(\w+)",\s*(-?\d+),\s*(-?\d+)\}', block(graphics, 'DEBUG_KEYS ='))}
same('debug override (GraphicsSettings.DEBUG_KEYS)', debug, settings['debug'])
for key, offered in sorted(settings['debug'].items()):
    if key in debug:
        low, high = debug[key]
        outside = [v for v in offered if v != -1 and not low <= v <= high]
        if outside:
            problems.append(f'debug override {key!r}: the settings screen offers {outside}, GraphicsSettings keeps {low}..{high}')
report = block(graphics, 'static String json(')
for fact in settings['host_facts']:
    if not re.search(rf'result\.put\(\s*"{fact}"', report):
        problems.append(f'host fact {fact!r}: GraphicsSettings.json does not report it')

# Script events: the interface scripts handle each, with every argument the engine passes.
scripts = {path: path.read_text(encoding='utf-8', errors='replace') for path in sorted((app / 'mobile-ui/PROGRAM').rglob('*.c'))}
for event, arguments in sorted(manifest['script_events'].items()):
    handlers = [path for path, source in scripts.items() if re.search(rf'#event_handler\(\s*"{event}"', source)]
    if not handlers:
        problems.append(f'script event {event!r}: no #event_handler in mobile-ui/PROGRAM')
    for argument in arguments:
        if not any(f'"{argument}"' in scripts[path] for path in handlers):
            problems.append(f'script event {event!r}: its handler does not take the argument {argument!r}')

# The real Java shortcut queue.
with tempfile.TemporaryDirectory(prefix='storm-engine-contract-') as classes:
    check = Path(__file__).with_name('ShortcutContractCheck.java')
    compiled = subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', classes,
                               str(port / 'GameplayShortcuts.java'), str(check)], capture_output=True, text=True)
    if compiled.returncode:
        problems.append('javac GameplayShortcuts: ' + compiled.stdout + compiled.stderr)
    else:
        ran = subprocess.run(['java', '-cp', classes, 'org.stormengine.port.ShortcutContractCheck',
                              *manifest['hud_host_input']['shortcut']], capture_output=True, text=True)
        print(ran.stdout.strip())
        if ran.returncode:
            problems.append('GameplayShortcuts: ' + (ran.stderr.strip().splitlines() or ['failed'])[0])

if problems:
    sys.exit('ENGINE CONTRACT: the application and engine/tools/android/app_contract.json differ:\n  '
             + '\n  '.join(problems))
print(f'PASS: engine contract: {len(natives)} JNI natives, {len(manifest["jni"]["upcalls"])} upcalls, {len(sent)} mailbox commands, '
      f'{len(debug)} debug overrides, {len(manifest["script_events"])} script events match the engine')
