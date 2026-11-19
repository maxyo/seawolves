#!/usr/bin/env python3
"""Package the shipped script patch layer and check it with the engine's own parser and applier.

- The shipped layer (script-patches/) must pass `script_patch_tool check`.
- Packaging refuses fixes whose licence does not allow shipping modified scripts.
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


def expect_refused(root, change, message):
    data = json.loads((root / 'sets' / teho / 'set.json').read_text())
    change(data)
    (root / 'sets' / teho / 'set.json').write_text(json.dumps(data))
    try:
        script_patch_assets.collect(root)
    except ValueError as error:
        assert message in str(error), error
        return
    raise AssertionError('packaging accepted: ' + message)


with tempfile.TemporaryDirectory(prefix='storm-script-patches-') as work:
    work = Path(work)
    tool = work / 'script_patch_tool'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-I', str(app.parent / 'engine/src/libs/util/include'),
                    str(app.parent / 'engine/tools/script_patches/script_patch_tool.cpp'), '-o', str(tool)], check=True)

    files, notice = script_patch_assets.collect(shipped)
    layer = work / 'shipped'
    write_layer(files, layer)
    subprocess.run([str(tool), 'check', str(layer)], check=True)
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
            ('path', lambda d: d['targets'][0].update(patch='../dialog.c.patch'), 'not a relative game path')):
        copy = work / ('refused-' + case)
        shutil.copytree(shipped, copy)
        expect_refused(copy, change, message)

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
