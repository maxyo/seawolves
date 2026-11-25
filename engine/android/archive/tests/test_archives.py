#!/usr/bin/env python3
"""Formats and adversarial fixtures through the reader's API (discover, then extract_root of the root found, as the
app imports): unsafe paths, links, case collisions, limits, CAB, tar filters, damaged data, staging checks. Every
attack is refused both when the source is read and when a source changed after discovery is extracted. No game
assets."""

import argparse
import io
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

import reader

p = argparse.ArgumentParser()
p.add_argument('--library', required=True)
p.add_argument('--inno-installer', type=Path)
args, remaining = p.parse_known_args()
lib = reader.load(args.library)
DATA = {
    'app/RESOURCE/Textures/Корабль.txt': b'texture\x00fixture',
    'app/PROGRAM/game.c': b'void game() {}',
    'app/engine.ini': b'[engine]\n',
    'app/crack.exe': b'MZnot executable fixture',
    'app/RESOURCE/payload.dll': b'discard',
    'app/README.txt': b'not game data',
}
INSTALLED = ['PROGRAM/game.c', 'RESOURCE/Textures/Корабль.txt', 'engine.ini']


class Archives(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.scratch = self.root / 'scratch'
        self.output = self.root / 'out'
        for d in (self.source, self.scratch, self.output):
            d.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def zip(self, entries=None, name='input.zip'):
        path = self.source / name
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for entry, data in (entries or DATA).items():
                z.writestr(entry, data)
        return path

    def tar(self, entries=None, mode='w', name='input.tar'):
        path = self.source / name
        with tarfile.open(path, mode) as archive:
            for entry, data in (entries or DATA).items():
                info = tarfile.TarInfo(entry)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        return path

    def install(self, path, **options):
        return reader.install(lib, path, self.output, **options)

    def discover(self, path, **options):
        shutil.rmtree(self.scratch)
        self.scratch.mkdir()
        return reader.discover(lib, path, self.scratch, **options)

    def extract(self, path, candidate, **options):
        return reader.extract_root(lib, path, self.scratch, candidate['id'], self.output, **options)

    def refused(self, path):
        """The status of the one container issue discovery reports for path (no game root found in it)."""
        code, document, error = self.discover(path)
        self.assertEqual(code, 0, error)
        self.assertEqual(document['candidates'], [], document)
        self.assertEqual(len(document['issues']), 1, document)
        return document['issues'][0]['status']

    def swapped(self, write_attack):
        """Discovers the plain game in input.zip, replaces it with what write_attack() writes and extracts the root
        found before: the status extraction refuses the changed source with."""
        path = self.zip()
        code, document, error = self.discover(path)
        self.assertEqual(code, 0, error)
        candidate = document['candidates'][0]
        path.unlink()
        write_attack()
        return self.extract(path, candidate)[0]

    def assert_game(self):
        self.assertEqual(
            (self.output / 'RESOURCE/Textures/Корабль.txt').read_bytes(), DATA['app/RESOURCE/Textures/Корабль.txt']
        )
        self.assertEqual(
            sorted(str(p.relative_to(self.output)) for p in self.output.rglob('*') if p.is_file()), INSTALLED
        )

    def test_zip(self):
        self.assertEqual(self.install(self.zip()), (0, ''))
        self.assert_game()

    def test_tar_filters(self):
        for mode in ['w', 'w:gz', 'w:bz2', 'w:xz']:
            with self.subTest(mode=mode):
                shutil.rmtree(self.output)
                self.output.mkdir()
                path = self.tar(mode=mode)
                self.assertEqual(self.install(path), (0, ''))
                self.assert_game()
                path.unlink()

    def test_cab_stored(self):
        entries = {'app/RESOURCE/a.txt': b'resource', 'app/PROGRAM/a.c': b'program', 'app/engine.ini': b'engine'}
        table = b''
        payload = b''
        for name, data in entries.items():
            table += (
                struct.pack('<IIHHHH', len(data), len(payload), 0, 0, 0, 32) + name.replace('/', '\\').encode() + b'\0'
            )
            payload += data
        data_offset = 44 + len(table)
        size = data_offset + 8 + len(payload)
        header = struct.pack('<4sIIIIIBBHHHHH', b'MSCF', 0, size, 0, 44, 0, 3, 1, 1, len(entries), 0, 0, 0)
        folder = struct.pack('<IHH', data_offset, 1, 0)
        block = struct.pack('<IHH', 0, len(payload), len(payload)) + payload
        path = self.source / 'input.cab'
        path.write_bytes(header + folder + table + block)
        self.assertEqual(self.install(path), (0, ''))
        self.assertEqual((self.output / 'RESOURCE/a.txt').read_bytes(), b'resource')

    @unittest.skipUnless(shutil.which('7z'), '7z fixture generator unavailable')
    def test_7z(self):
        content = self.root / 'content'
        for name, data in DATA.items():
            f = content / name
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(data)
        path = self.source / 'input.7z'
        subprocess.run(['7z', 'a', str(path), 'app'], cwd=content, check=True, stdout=subprocess.DEVNULL)
        self.assertEqual(self.install(path), (0, ''))
        self.assert_game()

    UNSAFE = ['../escape', 'app/RESOURCE/../../escape', '/absolute', 'C:/windows', 'app//RESOURCE/file',
              'app/RESOURCE/a\nfile']

    def test_unsafe_paths(self):
        for name in self.UNSAFE:
            with self.subTest(path=name):
                path = self.zip({**DATA, name: b'evil'}, 'evil.zip')
                self.assertEqual(self.refused(path), 5)
                path.unlink()
                self.assertEqual(self.swapped(lambda: self.zip({**DATA, name: b'evil'})), 5)
                self.assertFalse((self.root / 'escape').exists())

    def test_links_and_special_entries(self):
        for kind in [tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE]:
            with self.subTest(type=kind):

                def write():
                    path = self.tar(name='input.zip')  # the name discovery saw; read by signature
                    with tarfile.open(path, 'a') as archive:
                        entry = tarfile.TarInfo('app/RESOURCE/link')
                        entry.type = kind
                        entry.linkname = '/tmp'
                        archive.addfile(entry)
                    return path

                path = write()
                self.assertEqual(self.refused(path), 5)
                path.unlink()
                self.assertEqual(self.swapped(write), 5)
                shutil.rmtree(self.output)
                self.output.mkdir()

    def test_case_collision(self):
        # Android storage may be case-insensitive: two spellings of one path are refused when written.
        path = self.zip({**DATA, 'app/program/GAME.C': b'other'})
        self.assertEqual(self.install(path)[0], 5)

    def test_limits(self):
        for bounds in [reader.limits(total=10), reader.limits(file=3), reader.limits(entries=1)]:
            with self.subTest(bounds=bounds):
                shutil.rmtree(self.output)
                self.output.mkdir()
                self.assertEqual(self.install(self.zip(), bounds=bounds)[0], 6)

    def test_cancel(self):
        self.assertEqual(self.install(self.zip(), progress=lambda *_: 1)[0], 7)

    def test_nonempty_staging(self):
        (self.output / 'sentinel').write_text('keep')
        self.assertEqual(self.install(self.zip())[0], 5)
        self.assertEqual((self.output / 'sentinel').read_text(), 'keep')

    def test_symlink_staging(self):
        real = self.root / 'real'
        real.mkdir()
        self.output.rmdir()
        self.output.symlink_to(real)
        self.assertEqual(self.install(self.zip())[0], 1)
        self.assertEqual(list(real.iterdir()), [])

    def test_executable_is_never_an_archive(self):
        path = self.source / 'setup.exe'
        path.write_bytes(b'MZ' + bytes(1024))
        self.assertEqual(self.refused(path), 3)

    def test_truncated(self):
        path = self.zip()
        data = path.read_bytes()
        path.write_bytes(data[: len(data) // 2])
        self.assertEqual(self.refused(path), 5)

    def test_zip_crc(self):
        # Discovery reads only the directory of a ZIP: a damaged member is found when it is written.
        path = self.source / 'corrupt.zip'
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as z:
            for name, data in DATA.items():
                z.writestr(name, b'known unique payload' if name == 'app/PROGRAM/game.c' else data)
        path.write_bytes(path.read_bytes().replace(b'known unique payload', b'brokenunique payload'))
        code, error = self.install(path)
        self.assertEqual(code, 5)
        self.assertIn('app/PROGRAM/game.c', error)  # the damaged member is named

    def test_reentrant_results(self):
        # A discovery from inside an extraction's progress callback gets its own result, valid after both return.
        path = self.zip()
        nested = []

        def progress(user, stage, name, done, total, files):
            if files and not nested:
                with tempfile.TemporaryDirectory() as scratch:
                    nested.append(reader.discover(lib, path, scratch))
            return 0

        self.assertEqual(self.install(path, progress=progress), (0, ''))
        code, document, error = nested[0]
        self.assertEqual(code, 0, error)
        self.assertEqual(document['candidates'][0]['file_count'], len(INSTALLED))

    @unittest.skipUnless(args.inno_installer, 'Pass --inno-installer for a local real installer')
    def test_inno_bad_volume_signature(self):
        setup = self.source / 'setup.exe'
        shutil.copyfile(args.inno_installer, setup)
        for name in ['setup-1a.bin', 'setup-1b.bin', 'setup-1c.bin']:
            (self.source / name).write_bytes(bytes(128))
        self.assertEqual(self.install(setup)[0], 5)

    @unittest.skipUnless(args.inno_installer, 'Pass --inno-installer for a local real installer')
    def test_inno_decoder_cancel(self):
        progress = lambda user, stage, path, done, total, files: int(stage == 3 and done > 1024 * 1024)
        self.assertEqual(self.install(args.inno_installer, progress=progress)[0], 7)


if __name__ == '__main__':
    unittest.main(argv=['test_archives.py', *remaining])
