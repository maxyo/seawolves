#!/usr/bin/env python3
"""Synthetic metadata v2 discovery: inspects metadata only, never downloads payloads."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile
from run_jvm import encode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inspect', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='storm-discovery-') as directory:
        work = Path(directory)

        def fixture(name, paths):
            files = [{b'length': size, b'path': path.encode().split(b'/')} for path, size in paths]
            total = sum(size for _, size in paths)
            path = work / name
            path.write_bytes(
                encode(
                    {
                        b'info': {
                            b'name': b'bundle',
                            b'piece length': 16384,
                            b'pieces': b'0' * (20 * math.ceil(total / 16384)),
                            b'files': files,
                        }
                    }
                )
            )
            return path

        def inspect(path):
            return json.loads(subprocess.check_output([args.inspect, str(path)], text=True))

        def rejected(path):
            return subprocess.run([args.inspect, str(path)], capture_output=True).returncode != 0

        names = [
            'wrappers/deep/Alpha/pRoGrAm/a.c',
            'wrappers/deep/Alpha/ReSoUrCe/a.tx',
            'wrappers/deep/Alpha/ENGINE.INI',
            'other/Beta/PROGRAM/b.c',
            'other/Beta/RESOURCE/b.tx',
            'broken/Left/PROGRAM/a',
            'broken/Right/RESOURCE/b',
            'installers/Custom Loader.EXE',
            'extra/menu.exe',
            'archives/game.part01.rar',
            'archives/game.part02.rar',
            'split/renamed.001',
            'split/renamed.002',
            'zip/sample.zip',
            'zip/sample.z01',
            'manual/mystery.payload',
            'installers/custom loader-1a.bin',
            'other/Beta/engine.exe',
        ]
        result = inspect(fixture('mixed.torrent', [(name, 10) for name in names]))
        assert result['metadataVersion'] == 2 and result['kind'] == 'mixed-candidates'
        assert result['verifiedGame'] is False and result['evidence'] == 'filenames-only'
        roots = [c for c in result['candidates'] if c['kind'] == 'loose-game-candidate']
        assert {c['root'] for c in roots} == {'bundle/wrappers/deep/Alpha', 'bundle/other/Beta'}
        assert {tuple(c['indices']) for c in roots} == {(0, 1, 2), (3, 4)}
        alpha = next(c for c in roots if c['root'].endswith('Alpha'))
        assert alpha['rootKey'] == 'bundle/wrappers/deep/alpha'
        assert [p['path'] for p in alpha['relativePaths']] == ['PROGRAM/a.c', 'RESOURCE/a.tx', 'engine.ini']
        installers = [c for c in result['candidates'] if c['kind'] == 'installer-candidate']
        # engine.exe inside the Beta game root belongs to that game, not to a separate installer.
        assert {tuple(c['indices']) for c in installers} == {(7,), (8,)}, installers
        loader = next(c for c in installers if c['indices'] == [7])
        assert loader['companionIndices'] == [16], 'Its Inno volume, matched by name whatever the case'
        archives = [c for c in result['candidates'] if c['kind'] == 'archive-candidate']
        # The split ZIP (sample.z01 + sample.zip) is not offered: the archive reader cannot open it.
        assert {tuple(c['indices']) for c in archives} == {(9, 10), (11, 12)}, archives
        assert not any(f['recommended'] for f in result['files']), 'Independent candidates must never be unioned'
        assert all(f['selectable'] for f in result['files'])
        assert result['files'][15]['path'] == 'bundle/manual/mystery.payload'

        single = inspect(fixture('single.torrent', [('setup.exe', 10), ('setup-1.bin', 10), ('readme.txt', 10)]))
        assert single['kind'] == 'installer-candidate'
        assert [f['recommended'] for f in single['files']] == [True, True, False], (
            'One candidate is recommended with its volumes'
        )
        unknown = inspect(fixture('unknown.torrent', [('deep/odd.data', 10)]))
        assert not unknown['candidate'] and unknown['kind'] == 'unknown' and unknown['files'][0]['selectable']
        disconnected = inspect(fixture('disconnected.torrent', [('A/PROGRAM/a', 10), ('B/RESOURCE/b', 10)]))
        assert not disconnected['candidates'], 'Unrelated roots must not look like a game'
        crowded = inspect(
            fixture(
                'crowded.torrent',
                [('loader.exe', 10)] + [(f'loader-{i}.bin', 10) for i in range(1, 71)] + [('data-1.bin', 10)],
            )
        )
        installer = crowded['candidates'][0]
        assert installer['companionIndices'] == list(range(1, 71)), 'Every Inno volume of the installer, nothing else'
        assert installer['indices'] == [0], 'Companions remain hints, not part of the candidate'

        # Multi-volume sets are offered exactly when the archive reader opens them (android/archive/volume_names.hpp).
        sets = inspect(
            fixture(
                'sets.torrent',
                [
                    (name, 10)
                    for name in [
                        'old/Game.rar', 'old/game.r00', 'old/game.r01',  # RAR 2 naming, any case
                        'wide/game.part001.rar', 'wide/game.part002.rar',
                        'split/game.7z.001', 'split/game.7z.002', 'split/game.7z.003',
                        'gap/game.part1.rar', 'gap/game.part3.rar',  # part2 missing: not offered
                        'bare/game.part1', 'bare/game.part2',  # no format writes these
                        'lead/game.r00',  # old RAR without its .rar
                        'zip/game.z01', 'zip/game.zip',  # split ZIP
                        'single/game.tar.gz', 'single/game.zst',
                    ]
                ],
            )
        )
        offered = sorted(
            [sets['files'][i]['path'].split('/', 1)[1] for i in c['indices']]
            for c in sets['candidates']
            if c['kind'] == 'archive-candidate'
        )
        assert offered == [
            ['old/Game.rar', 'old/game.r00', 'old/game.r01'],
            ['single/game.tar.gz'],
            ['split/game.7z.001', 'split/game.7z.002', 'split/game.7z.003'],
            ['wide/game.part001.rar', 'wide/game.part002.rar'],
        ], offered

        for name, paths in [
            ('case-collision', ['Dir/PROGRAM/a', 'dir/RESOURCE/b']),
            ('duplicate', ['A/PROGRAM/a', 'A/PROGRAM/A']),
            ('overlap', ['A/b', 'a/B/c']),
        ]:
            assert rejected(fixture(name + '.torrent', [(p, 10) for p in paths])), name
        alias = work / 'mixed-utf8-alias.torrent'
        alias.write_bytes(
            encode(
                {
                    b'info': {
                        b'name': b'bundle',
                        b'piece length': 16384,
                        b'pieces': b'0' * 20,
                        b'files': [
                            {b'length': 10, b'path': [b'original'], b'path.utf-8': [b'Same']},
                            {b'length': 10, b'path': [b'same']},
                        ],
                    }
                }
            )
        )
        assert rejected(alias), 'A UTF-8 spelling must not collide with another file'

        def legacy(name, info):
            path = work / name
            path.write_bytes(encode({b'info': {b'piece length': 16384, b'pieces': b'0' * 20, **info}}))
            return path

        cp1251 = 'Корабль'.encode('cp1251')
        # libtorrent would write such a name with '_' for each byte; the torrent is refused instead.
        assert rejected(legacy('cp1251-path.torrent', {b'name': b'bundle', b'files': [{b'length': 10, b'path': [cp1251]}]}))
        assert rejected(legacy('cp1251-name.torrent', {b'name': cp1251, b'length': 10}))
        spelled = inspect(
            legacy(
                'cp1251-spelled.torrent',
                {
                    b'name': cp1251,
                    b'name.utf-8': 'Корабль'.encode(),
                    b'files': [{b'length': 10, b'path': [cp1251], b'path.utf-8': ['Палуба'.encode()]}],
                },
            )
        )
        assert spelled['files'][0]['path'] == 'Корабль/Палуба', spelled['files']
        v2_legacy = legacy(
            'v2-cp1251.torrent',
            {b'name': b'bundle', b'meta version': 2, b'file tree': {cp1251: {b'': {b'length': 10, b'pieces root': b'0' * 32}}}},
        )
        assert rejected(v2_legacy), 'v2 names must be UTF-8'
        v2 = work / 'v2-collision.torrent'
        leaf = {b'': {b'length': 10, b'pieces root': b'0' * 32}}
        v2.write_bytes(
            encode(
                {
                    b'info': {
                        b'name': b'bundle',
                        b'piece length': 16384,
                        b'meta version': 2,
                        b'file tree': {b'Dir': {b'a': leaf}, b'dir': {b'b': leaf}},
                    }
                }
            )
        )
        assert rejected(v2), 'v2 file trees get the same case checks'
        print(
            'PASS metadata v2: coherent roots at any depth, executables inside a root, mixed kinds, '
            'multi-volume sets as the archive reader opens them, Inno volumes by name, single-candidate recommendation, '
            'unknown files, '
            'case/duplicate/overlap/UTF-8/v2 collisions'
        )


if __name__ == '__main__':
    main()
