#!/usr/bin/env python3
"""Package the shipped script patch layer and check it with the engine's own parser and applier.

- The shipped layer (script-patches/) must pass `script_patch_tool check`.
- Packaging refuses fixes whose licence does not allow shipping modified scripts.
- Each platform's layer holds the fixes for that platform and only their scripts; a script changed by a fix of one
  platform and a fix of both is refused.
- With STORM_SCRIPT_PATCH_GAME=<game dir> (a vanilla TEHO 1.7.3 copy) the shipped TEHO 1.7.3 set is
  applied end to end and every target must come out with its result sha256; without it that step is
  reported as skipped.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

app = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(app / 'tools/android'))
import script_patch_assets  # noqa: E402

shipped = app / 'script-patches'
teho = 'teho-1.7.3-x64'


def write_layer(files, directory):
    for name, data in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def expect_refused(root, change, message, platform='android'):
    data = json.loads((root / 'sets' / teho / 'set.json').read_text())
    change(data)
    (root / 'sets' / teho / 'set.json').write_text(json.dumps(data))
    try:
        script_patch_assets.collect(root, platform)
    except ValueError as error:
        assert message in str(error), error
        return
    raise AssertionError('packaging accepted: ' + message)


with tempfile.TemporaryDirectory(prefix='storm-script-patches-') as work:
    work = Path(work)
    tool = work / 'script_patch_tool'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-I', str(app.parent / 'engine/src/libs/util/include'),
                    str(app.parent / 'engine/tools/script_patches/script_patch_tool.cpp'), '-o', str(tool)], check=True)

    files, notice = script_patch_assets.collect(shipped, 'android')
    layer = work / 'shipped'
    write_layer(files, layer)
    subprocess.run([str(tool), 'check', str(layer)], check=True)

    def fixes_and_targets(layer_files):
        lines = [line.split('\t') for line in layer_files[f'sets/{teho}/index.txt'].decode().splitlines()]
        return {f[1] for f in lines if f[0] == 'fix'}, {f[1].lower() for f in lines if f[0] == 'target'}

    source = json.loads((shipped / 'sets' / teho / 'set.json').read_text())
    android_fixes, android_targets = fixes_and_targets(files)
    assert android_fixes == {fix['id'] for fix in source['fixes']}, 'Android applies every fix'
    desktop_files, _ = script_patch_assets.collect(shipped, 'desktop')
    write_layer(desktop_files, work / 'desktop')
    subprocess.run([str(tool), 'check', str(work / 'desktop')], check=True)
    desktop_fixes, desktop_targets = fixes_and_targets(desktop_files)
    android_only = {fix['id'] for fix in source['fixes'] if fix.get('platforms') == ['android']}
    assert android_only and desktop_fixes == android_fixes - android_only, desktop_fixes ^ android_fixes
    only_android = {path.lower() for fix in source['fixes'] if fix['id'] in android_only for path in fix['targets']}
    assert desktop_targets == android_targets - only_android, desktop_targets ^ android_targets
    assert not any(name.endswith(('option_sl.c.patch', 'encounters_map.c.patch')) for name in desktop_files)
    print('PASS desktop layer:', len(desktop_fixes), 'of', len(android_fixes), 'fixes; Android only:',
          ', '.join(sorted(android_only)))
    manifest = json.loads(files['manifest.json'])
    assert set(manifest) == set(files) - {'manifest.json'}, 'manifest must cover every layer file'
    assert b'no game files are included' in notice
    assert b'dialog-time-acceleration' in notice and b'0f122655606ba30f' in notice and b'GPL-3.0' in notice
    assert files[f'sets/{teho}/index.txt'].startswith(f'storm-script-patches\t1\nset\t{teho}\t'.encode())

    for case, change, message in (
            ('licence', lambda d: d['fixes'][0].update(license='LicenseRef-Proprietary'), 'not cleared'),
            ('source', lambda d: d['fixes'][0].pop('source'), 'source'),
            ('orphan', lambda d: d['fixes'].clear(), 'belonging to no fix'),
            ('identify', lambda d: d.update(identify=[]), 'identify the game'),
            ('path', lambda d: d['targets'][0].update(patch='../dialog.c.patch'), 'not a relative game path'),
            ('platform', lambda d: d['fixes'][0].update(platforms=['ios']), 'platforms')):
        copy = work / ('refused-' + case)
        shutil.copytree(shipped, copy)
        expect_refused(copy, change, message)

    # items-weight-lookup and skill-steps-trace share RPGUtilite.c: one of them on Android only cannot be packaged.
    copy = work / 'refused-shared'
    shutil.copytree(shipped, copy)
    expect_refused(copy, lambda d: next(f for f in d['fixes'] if f['id'] == 'skill-steps-trace').update(
        platforms=['android']), 'changes program/characters/rpgutilite.c with a fix that is', 'desktop')

    game = os.environ.get('STORM_SCRIPT_PATCH_GAME')
    if not game:
        print('SKIP end-to-end apply: set STORM_SCRIPT_PATCH_GAME to a vanilla TEHO 1.7.3 game directory')
    else:
        result = subprocess.run([str(tool), 'apply', str(layer), game, str(work / 'derived')],
                                capture_output=True, text=True)
        print(result.stdout, end='')
        assert result.returncode == 0, result.stderr
        for target in json.loads((shipped / 'sets' / teho / 'set.json').read_text())['targets']:
            derived = [p for p in (work / 'derived').rglob('*')
                       if p.is_file() and p.as_posix().lower().endswith('/' + target['path'].lower())]
            assert len(derived) == 1, (target['path'], derived)
            assert script_patch_assets.sha256(derived[0].read_bytes()) == target['result_sha256'], target['path']
        print('PASS end-to-end apply of', teho, 'against', game)
print('PASS script patch layer')
