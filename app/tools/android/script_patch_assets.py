#!/usr/bin/env python3
"""Package the script patch layer (script-patches/) and author patches.

The layer holds unified diffs against one exact version of a game's scripts; the
engine applies them to the player's own files (engine: tools/script_patches/README.md).
Only fixes whose licence permits redistributing modified scripts are packaged:
each fix names its licence and source, and anything outside REDISTRIBUTABLE is
refused. Game files are never packaged.

Source of truth, per patch set (one game version):

  script-patches/sets/<set>/set.json
  script-patches/sets/<set>/<patch files>

set.json:
  {"id": "<set>", "game": "...", "version": "...",
   "identify": [{"path": "PROGRAM/...", "sha256": "..."}],
   "targets": [{"path": "PROGRAM/...", "base_sha256": "...", "result_sha256": "...", "patch": "<file>"}],
   "fixes": [{"id": "...", "title": "...", "targets": ["PROGRAM/..."], "license": "GPL-3.0-only",
              "source": "<upstream repository>", "bug": "<issue or report URL>",
              "save_compatibility": "<what an existing save sees>",
              "platforms": ["android"]}]}

`platforms` (optional, default every one of PLATFORMS) names where a fix is applied: a fix that needs another part
of one platform (its interface, its settings) is packaged only for it. A script one fix changes for one platform and
another for both cannot be split: packaging refuses it.

The layer for a platform (the APK's assets/script-patches, the files a desktop engine build installs beside it):
  python3 tools/android/script_patch_assets.py layer --platform desktop --source script-patches --out <dir>

Authoring:
  python3 tools/android/script_patch_assets.py diff --base <vanilla file> --edited <fixed file> \\
      --target PROGRAM/dialog.c --out script-patches/sets/<set>/dialog.c.patch
prints the base and result sha256 for set.json. Check a set against a game with the
engine's own applier: engine/tools/script_patches/script_patch_tool apply.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

# Licences under which modified game scripts may be shipped in the APK, and the
# notice file (already in assets/licenses) that carries their text.
REDISTRIBUTABLE = {'GPL-3.0-only': 'storm-GPLv3.txt', 'GPL-3.0-or-later': 'storm-GPLv3.txt'}
PLATFORMS = ('android', 'desktop')
SHA256 = re.compile(r'^[0-9a-f]{64}$')
ID = re.compile(r'^[a-z0-9._-]{1,64}$')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _text(value, what):
    if not isinstance(value, str) or not value or any(c in value for c in '\t\n\r'):
        raise ValueError(f'{what} must be a non-empty single-line string without tabs')
    return value


def _relative(value, what):
    _text(value, what)
    parts = value.split('/')
    if value.startswith('/') or '\\' in value or any(p in ('', '.', '..') for p in parts):
        raise ValueError(f'{what}: {value!r} is not a relative game path')
    return value


def load_set(directory: Path):
    """Validate one set; returns (set dict, {patch file name: bytes})."""
    data = json.loads((directory / 'set.json').read_text(encoding='utf-8'))
    where = f'{directory.name}/set.json'
    if data.get('id') != directory.name or not ID.match(data['id']):
        raise ValueError(f'{where}: id must equal the directory name and match {ID.pattern}')
    _text(data.get('game'), f'{where}: game')
    _text(data.get('version'), f'{where}: version')
    if not data.get('identify'):
        raise ValueError(f'{where}: identify the game with at least one file sha256')
    for probe in data['identify']:
        _relative(probe.get('path'), f'{where}: identify path')
        if not SHA256.match(probe.get('sha256', '')):
            raise ValueError(f'{where}: identify {probe["path"]} needs a sha256')
    patches, targets = {}, {}
    for target in data.get('targets', []):
        path = _relative(target.get('path'), f'{where}: target path')
        if path.lower() in targets:
            raise ValueError(f'{where}: duplicate target {path}')
        for key in ('base_sha256', 'result_sha256'):
            if not SHA256.match(target.get(key, '')):
                raise ValueError(f'{where}: target {path} needs {key}')
        name = _relative(target.get('patch'), f'{where}: patch of {path}')
        patch = directory / name
        if patch.is_symlink() or not patch.is_file():
            raise ValueError(f'{where}: patch {name} is missing')
        patches[name] = patch.read_bytes()
        target['patch_sha256'] = sha256(patches[name])
        targets[path.lower()] = target
    used = set()
    ids = set()
    for fix in data.get('fixes', []):
        fid = fix.get('id', '')
        if not ID.match(fid) or fid in ids:
            raise ValueError(f'{where}: bad or duplicate fix id {fid!r}')
        ids.add(fid)
        for key in ('title', 'source', 'bug', 'save_compatibility'):
            _text(fix.get(key), f'{where}: fix {fid} {key}')
        if fix.get('license') not in REDISTRIBUTABLE:
            raise ValueError(f'{where}: fix {fid} has licence {fix.get("license")!r}, which is not cleared for '
                             f'shipping modified scripts (allowed: {", ".join(sorted(REDISTRIBUTABLE))})')
        if not fix.get('targets'):
            raise ValueError(f'{where}: fix {fid} has no targets')
        platforms = fix.get('platforms', list(PLATFORMS))
        if not platforms or not set(platforms) <= set(PLATFORMS) or len(set(platforms)) != len(platforms):
            raise ValueError(f'{where}: fix {fid} platforms {platforms!r} must name some of {", ".join(PLATFORMS)}')
        for path in fix['targets']:
            if path.lower() not in targets:
                raise ValueError(f'{where}: fix {fid} names unknown target {path}')
            used.add(path.lower())
    unused = set(targets) - used
    if unused:
        raise ValueError(f'{where}: targets belonging to no fix: {", ".join(sorted(unused))}')
    return data, patches


def for_platform(data, patches, platform):
    """The set as one platform applies it: its fixes and their targets only."""
    fixes = [fix for fix in data['fixes'] if platform in fix.get('platforms', PLATFORMS)]
    kept = {path.lower() for fix in fixes for path in fix['targets']}
    for fix in data['fixes']:
        shared = kept & {path.lower() for path in fix['targets']}
        if fix not in fixes and shared:
            raise ValueError(f'{data["id"]}/set.json: fix {fix["id"]} is not applied on {platform} but changes '
                             f'{", ".join(sorted(shared))} with a fix that is')
    targets = [target for target in data['targets'] if target['path'].lower() in kept]
    return {**data, 'fixes': fixes, 'targets': targets}, {t['patch']: patches[t['patch']] for t in targets}


def index(data):
    """The engine's index.txt for a validated set."""
    lines = ['storm-script-patches\t1', f'set\t{data["id"]}\t{data["game"]}\t{data["version"]}']
    lines += [f'identify\t{p["path"]}\t{p["sha256"]}' for p in data['identify']]
    lines += [f'target\t{t["path"]}\t{t["base_sha256"]}\t{t["result_sha256"]}\t{t["patch"]}' for t in data['targets']]
    lines += [f'fix\t{f["id"]}\t{",".join(f["targets"])}\t{f["license"]}\t{f["source"]}\t{f["title"]}'
              for f in data['fixes']]
    return ('\n'.join(lines) + '\n').encode()


def collect(root: Path, platform):
    """Layer files (path -> bytes) of one platform, plus the About notice text."""
    if platform not in PLATFORMS:
        raise ValueError(f'unknown platform {platform!r} (one of {", ".join(PLATFORMS)})')
    files, provenance = {}, []
    sets = root / 'sets'
    for directory in sorted(sets.iterdir()) if sets.is_dir() else []:
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f'Unexpected entry in script-patches/sets: {directory.name}')
        data, patches = for_platform(*load_set(directory), platform)
        files[f'sets/{data["id"]}/index.txt'] = index(data)
        for name, patch in patches.items():
            files[f'sets/{data["id"]}/{name}'] = patch
        provenance.append(data)
    files['SOURCE.json'] = json.dumps({
        'purpose': 'Fixes applied by the engine to the player\'s own game scripts; no game files are included',
        'sets': provenance}, indent=2, ensure_ascii=False).encode() + b'\n'
    files['manifest.json'] = json.dumps({n: sha256(d) for n, d in files.items()},
                                       sort_keys=True, indent=2).encode() + b'\n'
    return files, notice(provenance)


def notice(provenance):
    """assets/licenses/script-patches.txt for the About / licences screen."""
    out = ['Script fixes', '',
           'The application carries fixes (unified diffs) to game scripts. They are applied',
           'to your own game files at start-up when those files are exactly the version the',
           'fix was made for; no game files are included in the application.', '']
    if not provenance:
        out.append('This build carries no script fixes.')
    for data in provenance:
        out += [f'{data["game"]} {data["version"]} (set {data["id"]})', '']
        targets = {t['path'].lower(): t for t in data['targets']}
        for fix in data['fixes']:
            out += [f'  {fix["title"]} [{fix["id"]}]', f'    Licence: {fix["license"]} '
                    f'(text: {REDISTRIBUTABLE[fix["license"]]})', f'    Source: {fix["source"]}',
                    f'    Bug: {fix["bug"]}', f'    Saves: {fix["save_compatibility"]}']
            for path in fix['targets']:
                t = targets[path.lower()]
                out.append(f'    {t["path"]}: base sha256 {t["base_sha256"]}, patch sha256 {t["patch_sha256"]}')
            out.append('')
    return ('\n'.join(out) + '\n').encode()


def unified_diff(base: bytes, edited: bytes, target: str):
    """`diff -u` of two script versions, byte-exact (CRLF kept, latin-1 round trip)."""
    def lines(data):  # only LF ends a line (str.splitlines also splits on bytes such as 0x85)
        return [line for line in re.split('(?<=\n)', data.decode('latin-1')) if line]
    old, new = lines(base), lines(edited)
    out = [f'--- a/{target}\n', f'+++ b/{target}\n']
    for line in list(difflib.unified_diff(old, new, n=3, lineterm='\n'))[2:]:
        if line.startswith('@@'):
            out.append(line if line.endswith('\n') else line + '\n')
        elif line.endswith('\n'):
            out.append(line)
        else:
            out += [line + '\n', '\\ No newline at end of file\n']
    if len(out) == 2:
        raise ValueError(f'{target}: no changes')
    return ''.join(out).encode('latin-1')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    diff = sub.add_parser('diff', help='write a patch and print the sha256 values for set.json')
    diff.add_argument('--base', type=Path, required=True)
    diff.add_argument('--edited', type=Path, required=True)
    diff.add_argument('--target', required=True, help='game-relative path, e.g. PROGRAM/dialog.c')
    diff.add_argument('--out', type=Path, required=True)
    layer = sub.add_parser('layer', help="write one platform's layer into a new directory")
    layer.add_argument('--platform', choices=PLATFORMS, required=True)
    layer.add_argument('--source', type=Path, required=True, help='the script-patches folder (sets/<set>/set.json)')
    layer.add_argument('--out', type=Path, required=True, help='the layer directory, replaced')
    arguments = parser.parse_args()
    if arguments.command == 'layer':
        files, notice = collect(arguments.source, arguments.platform)
        shutil.rmtree(arguments.out, ignore_errors=True)
        for name, data in {**files, 'NOTICE.txt': notice}.items():
            (arguments.out / name).parent.mkdir(parents=True, exist_ok=True)
            (arguments.out / name).write_bytes(data)
        return 0
    base, edited = arguments.base.read_bytes(), arguments.edited.read_bytes()
    arguments.out.write_bytes(unified_diff(base, edited, _relative(arguments.target, '--target')))
    print(json.dumps({'path': arguments.target, 'base_sha256': sha256(base), 'result_sha256': sha256(edited),
                      'patch': arguments.out.name}, indent=2))


if __name__ == '__main__':
    sys.exit(main())
