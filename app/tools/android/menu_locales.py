#!/usr/bin/env python3
"""Validate and collect only application-owned UTF-8 menu translations."""
import json
import re
import argparse
import configparser
from pathlib import Path

POSITIONAL_FORMAT = re.compile(r"%([1-9][0-9]*)\$[-#+ 0,(]*(?:[0-9]+)?(?:\.[0-9]+)?([a-zA-Z])")

def format_signature(value):
    """Validate indexed Java Formatter arguments; literal %% is not an argument."""
    value = value.replace("%%", "")
    matches = list(POSITIONAL_FORMAT.finditer(value))
    if "%" in POSITIONAL_FORMAT.sub("", value):
        raise ValueError("Invalid indexed format string")
    # Translators may reorder arguments, but flags/width/precision must retain
    # the known working English specifier. Matching only the conversion letter
    # would admit invalid Formatter forms such as %1$.1d or %1$,,d.
    return sorted((int(match.group(1)), match.group(0).split('$', 1)[1]) for match in matches)

def collect(root):
    result = {}
    baseline = json.loads((root / 'en.json').read_text(encoding='utf-8'))
    if not isinstance(baseline, dict) or not baseline or not all(re.fullmatch(r'[a-z][a-z0-9_]*', k) for k in baseline):
        raise ValueError('Invalid English menu keys')
    for path in sorted(root.glob('*.json')):
        if not re.fullmatch(r'[a-z]{2,3}\.json', path.name):
            raise ValueError('Invalid menu locale filename: ' + path.name)
        data = path.read_bytes()
        if len(data) > 65536:
            raise ValueError('Menu catalog exceeds 64 KiB')
        catalog = json.loads(data.decode('utf-8'))
        if not isinstance(catalog, dict) or catalog.keys() != baseline.keys() or not all(isinstance(v, str) and v.strip() for v in catalog.values()):
            raise ValueError('Incomplete or invalid menu catalog: ' + path.name)
        # Indexed templates are application-owned Formatter strings. Reject a
        # translated argument/type mismatch before it can crash a status update.
        for key, english in baseline.items():
            if re.search(r"%[1-9][0-9]*\$", english):
                try:
                    if format_signature(catalog[key]) != format_signature(english):
                        raise ValueError("Argument mismatch")
                except ValueError as error:
                    raise ValueError("Invalid menu format: " + path.name + ":" + key) from error
        result['assets/locales/' + path.name] = data
    required = {'en', 'ru', 'de', 'es', 'fr', 'it'}
    missing = required - {Path(name).stem for name in result}
    if missing:
        raise ValueError('Missing game-language menu catalogs: ' + ', '.join(sorted(missing)))
    return result

def check_game_languages(path, payload):
    """Check a local game's registry without copying any of its resources."""
    registry = configparser.ConfigParser(interpolation=None)
    registry.read_string(path.read_text(encoding='utf-8-sig'))
    names = {'english': 'en', 'russian': 'ru', 'german': 'de',
             'french': 'fr', 'spanish': 'es', 'italian': 'it'}
    available = {Path(name).stem for name in payload}
    if not registry.has_section('DIRECTORY') or not registry['DIRECTORY']:
        raise ValueError('Game language registry has no DIRECTORY entries')
    unsupported = []
    for name in registry['DIRECTORY']:
        # GetLanguage() exports the registry key, never its directory value.
        code = names.get(name.casefold())
        if code not in available:
            unsupported.append(name)
    if unsupported:
        raise ValueError('Game languages without UI catalogs: ' + ', '.join(unsupported))
    return list(registry['DIRECTORY'])

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-language-ini', type=Path,
                        help='Check a local resource/ini/texts/language.ini; never bundled')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    payload = collect(root / 'android/game/assets/locales')
    if args.game_language_ini:
        print('PASS: registered game languages:', ', '.join(check_game_languages(args.game_language_ini, payload)))
    print('PASS: complete UTF-8 menu catalogs:', ', '.join(payload))
