"""Java classes of org.stormengine.port generated from the engine's tools/android/app_contract.json, the one home of
the names the application shares with the engine (the engine generates its headers from the same file:
engine/cmake/StormAppContract.cmake). Build outputs: build_apk.py and the Java checks write them next to their
classes, nothing is committed."""
import json
from pathlib import Path
import re

HEADER = '// Generated from engine/tools/android/app_contract.json by app/tools/android/app_contract.py.\n'


def archive_status(contract):
    constants = ''.join(f'    static final int {name} = {code};\n'
                        for name, code in sorted(contract['archive_status'].items(), key=lambda item: item[1]))
    return (HEADER + 'package org.stormengine.port;\n\n'
            '/** The status a {@link NativeArchives} call returns (result[0]), by name; the meanings: engine\n'
            ' *  android/archive/README.md. */\n'
            f'final class ArchiveStatus {{\n{constants}\n    private ArchiveStatus() {{}}\n}}\n')


def constant(name):
    """A Java constant's name for a contract name: appSettings, app-settings, app_settings -> APP_SETTINGS."""
    return re.sub(r'(?<=[a-z])(?=[A-Z])', '_', name).replace('-', '_').upper()


def ui_names(contract):
    """What the UI and the activity name on both sides (the engine's app_ui_names.hpp)."""
    lines = ['    // The HUD input the engine hands to the activity (onUiHudInput), and the values of each.']
    for verb, values in contract['hud_host_input'].items():
        lines.append(f'    static final String HUD_{constant(verb)} = "{verb}";')
        lines += [f'    static final String {constant(verb)}_{constant(v)} = "{v}";' for v in values]
        lines.append(f'    static final String[] {constant(verb)}S = {{' + ', '.join(f'"{v}"' for v in values) + '};')
    lines.append('    // The UI\'s actions the activity handles (onUiAction).')
    lines += [f'    static final String HOST_{constant(a)} = "{a}";' for a in contract['host_actions']]
    lines.append('    // The UI state channels the hosts write.')
    lines += [f'    static final String CHANNEL_{constant(c)} = "{c}";'
              for channels in contract['state_channels'].values() for c in channels]
    settings = contract['settings']
    lines.append('    // The settings the application stores and the engine reads.')
    lines += [f'    static final String SETTING_{constant(k)} = "{k}";' for k in settings['keys']]
    lines.append('    static final String[] SETTINGS = {' + ', '.join(f'SETTING_{constant(k)}' for k in settings['keys']) + '};')
    lines.append('    // The About screen\'s settings: stored by the launcher, never sent by the settings screen.')
    lines += [f'    static final String SETTING_{constant(k)} = "{k}";' for k in settings['about_keys']]
    lines.append('    // The display options\' steps and the value before the player chose one.')
    for key, steps in settings['steps'].items():
        lines.append(f'    static final int[] STEPS_{constant(key)} = {{' + ', '.join(map(str, steps['values'])) + '};')
        lines.append(f'    static final int DEFAULT_{constant(key)} = {steps["default"]};')
    lines.append('    // What the application reports beside the settings.')
    lines += [f'    static final String FACT_{constant(f)} = "{f}";' for f in settings['host_facts']]
    lines.append('    // The debug overrides of a debuggable build and the values the settings screen offers (-1: the preset\'s).')
    lines += [f'    static final String DEBUG_{constant(k)} = "{k}";' for k in settings['debug']]
    lines.append('    static final String[] DEBUG_KEYS = {' + ', '.join(f'DEBUG_{constant(k)}' for k in settings['debug']) + '};')
    lines.append('    static final int[][] DEBUG_VALUES = {'
                 + ', '.join('{' + ', '.join(map(str, values)) + '}' for values in settings['debug'].values()) + '};')
    return (HEADER + 'package org.stormengine.port;\n\n'
            '/** What the UI and the activity name on both sides. */\n'
            'final class AppNames {\n' + '\n'.join(lines) + '\n\n    private AppNames() {}\n}\n')


def write_java(engine, out):
    """Writes the generated classes under `out` (a source root) and returns {class name: path}."""
    contract = json.loads((Path(engine) / 'tools/android/app_contract.json').read_text())
    package = Path(out) / 'org/stormengine/port'
    package.mkdir(parents=True, exist_ok=True)
    written = {}
    for name, text in (('ArchiveStatus', archive_status(contract)), ('AppNames', ui_names(contract))):
        path = package / (name + '.java')
        if not path.is_file() or path.read_text() != text:
            path.write_text(text)
        written[name] = path
    return written
