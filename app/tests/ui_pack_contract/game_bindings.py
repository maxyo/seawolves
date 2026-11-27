#!/usr/bin/env python3
"""Where the UI pack's taps end in the game: every action the pack check's crawl recorded (storm_ui_pack_check
--actions, one JSON object a line) is followed to something that exists and handles it.

- A widget action of a game interface (screen = the interface id, node, verb) reaches a node of the interface's
  ini (RESOURCE/INI/INTERFACES), or a node the engine makes itself (ANDROID_*: named in the engine's xinterface).
  A press (the engine clicks the node: XINTERFACE::MobileApplyAction, ACTION_MOUSECLICK or ACTION_ACTIVATE)
  needs a click or activate command on the node, and that command must do something: an event: the scripts
  handle (SetEventHandler), a com:/select: of the ini, or the interface script's ievnt_command handler naming
  the node.
- An action of an application screen (screen app:launcher, app:downloads, app:about: the verb is the command)
  is a command the application's Java handles (a string literal of android/).

The game's scripts are the installed game's PROGRAM/ with this application's interface scripts
(mobile-ui/PROGRAM) over it and the lines its script patches add (script-patches/sets/*/*.patch).

  game_bindings.py --actions FILE --game DIR [--engine DIR]

Exit 0 when every action ends in something, 1 otherwise, 2 for bad arguments.
"""
import argparse
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Nodes the engine's fixtures add on purpose that no game interface has, each with why; nothing else is excused.
FIXTURE_NODES = {
    ('store', 'B_GUILD'): "the store model's stand-in for a mod's extra button: it fills the «Прочее» sheet",
}


def read(path):
    return path.read_text(encoding='cp1251', errors='replace')


class Game:
    def __init__(self, game, engine):
        self.game = game
        program = game / 'PROGRAM'
        self.scripts = {}  # lower-case PROGRAM-relative path -> text
        for path in program.rglob('*.c'):
            self.scripts[path.relative_to(program).as_posix().lower()] = read(path)
        for path in (root / 'mobile-ui/PROGRAM').rglob('*.c'):
            self.scripts[path.relative_to(root / 'mobile-ui/PROGRAM').as_posix().lower()] = read(path)
        for patch in (root / 'script-patches/sets').glob('*/*.patch'):
            added = '\n'.join(line[1:] for line in read(patch).splitlines() if line.startswith('+') and not line.startswith('+++'))
            key = patch.stem.lower()  # "<file>.c"
            for name in self.scripts:
                if name.endswith('/' + key) or name == key:
                    self.scripts[name] += '\n' + added
        self.handled = set()  # events some script handles
        for text in self.scripts.values():
            self.handled.update(name.lower() for name in re.findall(r'SetEventHandler\s*\(\s*"([^"]+)"', text))
        # interface id (ini stem, lower case) -> its script, from BaseInterface.c
        base = self.scripts.get('interface/baseinterface.c', '')
        ini_of, script_of = {}, {}
        for slot, value in re.findall(r'Interfaces\[(\w+)\]\.IniFile\s*=\s*"([^"]+)"', base):
            ini_of[slot] = value.replace('\\', '/').split('/')[-1]
        for slot, value in re.findall(r'Interfaces\[(\w+)\]\.SectionName\s*=\s*"([^"]+)"', base):
            script_of[slot] = value.replace('\\', '/').lower()
        self.interfaces = {}
        for slot, ini in ini_of.items():
            if slot in script_of:
                self.interfaces[Path(ini).stem.lower()] = (ini, script_of[slot])
        inis = game / 'RESOURCE/INI/interfaces'
        self.ini_files = {path.name.lower(): path for path in inis.glob('*.ini')} if inis.is_dir() else {}
        self.engine_text = ''
        if engine:
            for path in (engine / 'src/libs/xinterface/src').rglob('*'):
                if path.suffix not in ('.c', '.cpp', '.h', '.hpp', '.inc'):
                    continue
                self.engine_text += path.read_text(errors='replace')

    def ini_nodes(self, screen):
        """{NODE (upper case): [command lines]} of the interface's ini, None when it has none."""
        found = self.interfaces.get(screen)
        path = self.ini_files.get((found[0] if found else screen + '.ini').lower())
        if not path:
            return None
        nodes, current = {}, None
        for line in read(path).splitlines():
            line = line.split(';', 1)[0].strip()
            if line.startswith('[') and line.endswith(']'):
                current = line[1:-1].strip().upper()
                nodes.setdefault(current, [])
            elif current and re.match(r'command\s*=', line, re.I):
                nodes[current].append(line.split('=', 1)[1].strip())
            elif current == 'MAIN' and re.match(r'item\s*=', line, re.I):
                nodes.setdefault(line.split(',')[-1].strip().upper(), [])
        return nodes

    def script(self, screen):
        found = self.interfaces.get(screen)
        return self.scripts.get(found[1], '') if found else ''


def press_handled(game, screen, node, commands):
    """Why a press of `node` does something, or None."""
    script = game.script(screen)
    names = re.compile(r'(case\s*"%s"|nodName\s*==\s*"%s"|"%s"\s*==\s*nodName)' % ((re.escape(node),) * 3), re.I)
    for line in commands:
        parts = [part.strip() for part in line.split(',')]
        if parts[0].lower() not in ('click', 'activate'):
            continue
        for part in parts[1:]:
            if part.lower().startswith('event:'):
                event = part[6:].strip()
                if event.lower() in game.handled:
                    return f'{parts[0]} → event {event}'
            elif part.lower().startswith(('com:', 'select:')):
                return f'{parts[0]} → {part}'
        if names.search(script):
            return f'{parts[0]} → ievnt_command case {node}'
    return None


def check(game, record, java):
    screen, node, verb = record['screen'], record['node'], record['verb']
    if screen.startswith('app:'):
        if not re.search(r'"%s"' % re.escape(verb), java):
            return f'the application command "{verb}" is handled nowhere in android/'
        return None
    if screen in ('hud', 'dialogue', 'mainmenu', 'game_over', 'sound') or not record.get('wire'):
        return None  # engine commands: the pack check holds them to tools/android/app_contract.json
    if (screen, node) in FIXTURE_NODES:
        return None
    if node.upper().startswith('ANDROID_'):
        return None if f'"{node}"' in game.engine_text or not game.engine_text else f'the engine has no node {node}'
    nodes = game.ini_nodes(screen)
    if nodes is None:
        return 'other game'  # a fixture of an interface this game does not have (a mod's, for the generic layout)
    commands = nodes.get(node.upper())
    if commands is None:
        if re.search(r'"%s"' % re.escape(node), game.script(screen), re.I):
            return None  # made by the interface script (CreateNode / a table of its own)
        if f'"{node}"' in game.engine_text:
            return None  # a node of the game's shared scripts the engine acts on itself (tooltip_fader)
        return f'the interface {screen} has no node {node}'
    wire = record.get('wire', '')
    if verb == 'press' or wire == 'press':
        if not press_handled(game, screen, node, commands):
            return f'a press of {screen} {node} does nothing in the game: no click/activate command that reaches a handler'
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--actions', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path, help='the installed game (holding PROGRAM/ and RESOURCE/)')
    parser.add_argument('--engine', type=Path, default=root.parent / 'engine')
    args = parser.parse_args()
    if not (args.game / 'PROGRAM/interface/BaseInterface.c').is_file():
        sys.exit(f'game_bindings: {args.game} has no PROGRAM/interface/BaseInterface.c')
    game = Game(args.game, args.engine if (args.engine / 'src/libs/xinterface').is_dir() else None)
    java = '\n'.join(path.read_text(errors='replace') for path in (root / 'android').rglob('*.java'))
    problems, seen, other = [], set(), set()
    records = [json.loads(line) for line in args.actions.read_text().splitlines() if line.strip()]
    for record in records:
        key = (record['screen'], record['node'], record['verb'].split('@')[0])
        if key in seen:
            continue
        seen.add(key)
        wrong = check(game, record, java)
        if wrong == 'other game':
            other.add(record['screen'])
        elif wrong:
            problems.append(f'{record["case"]}: {record["tap"]}: {wrong}\n      via {record["path"]}')
    for line in problems:
        print('FAIL', line)
    for screen in sorted(other):
        print(f'NOT IN THIS GAME {screen}: a fixture of another game\'s interface (no ini of that name), not followed')
    print(f'game bindings: {len(seen)} distinct actions of {len(records)} taps, {len(problems)} ending in nothing')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
