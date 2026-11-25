#!/usr/bin/env python3
"""Entry names that are not UTF-8 (README «Entry names»): ZIP names without the UTF-8 flag in the Russian DOS
(CP866) and Windows (CP1251) code pages, mixed archives, the UTF-8 flag, invalid sequences, names that are not
Cyrillic text, Unicode names of 7z/RAR under any locale, and removal of trees with names that are not UTF-8.
Synthetic fixtures; no game assets."""

import argparse
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

import reader

p = argparse.ArgumentParser()
p.add_argument('--library', required=True)
p.add_argument('--libarchive-tests', type=Path, help='libarchive-3.8.10/libarchive/test (RAR fixtures)')
args, remaining = p.parse_known_args()
LIBRARY = str(Path(args.library).resolve())
lib = reader.load(LIBRARY)
UTF8_FLAG = 1 << 11
GAME = [(b'app/engine.ini', b'[engine]\n'), (b'app/PROGRAM/game.c', b'void game() {}')]


def zip_bytes(entries):
    """A stored ZIP of (name bytes, data[, flags]) written as given: names are not re-encoded and the UTF-8 flag is
    set only when asked for, as DOS and Windows archivers write them."""
    local, central = b'', b''
    for entry in entries:
        name, data, flags = entry if len(entry) == 3 else (*entry, 0)
        crc = zlib.crc32(data)
        header = struct.pack('<4sHHHHHIIIHH', b'PK\3\4', 20, flags, 0, 0, 0, crc, len(data), len(data), len(name), 0)
        central += struct.pack(
            '<4sHHHHHHIIIHHHHHII', b'PK\1\2', 20, 20, flags, 0, 0, 0, crc, len(data), len(data), len(name), 0, 0, 0, 0,
            0, len(local)
        ) + name
        local += header + name + data
    end = struct.pack('<4sHHHHIIH', b'PK\5\6', 0, 0, len(entries), len(entries), len(central), len(local), 0)
    return local + central + end


class Names(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.output = self.root / 'out'
        self.output.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def archive(self, entries, name='input.zip'):
        path = self.root / name
        path.write_bytes(zip_bytes(GAME + entries))
        return path

    def extract(self, path):
        return reader.install(lib, path, self.output)

    def written(self):
        """Every name under the output, as bytes: each must be UTF-8."""
        names = []
        for top, dirs, files in os.walk(bytes(self.output)):
            for name in dirs + files:
                full = os.path.relpath(os.path.join(top, name), bytes(self.output))
                full.decode('utf-8')  # raises for a name that is not UTF-8
                names.append(full.decode())
        return sorted(names)

    def test_dos_and_windows_names(self):
        letters = 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя'
        cases = {
            'cp866': ['tavern_ГПК.tga.tx', 'Корабль.txt', letters + '.txt', 'Палуба/№1 бриг.txt'],
            'cp1251': ['tavern_ГПК.tga.tx', 'Корабль.txt', letters + '.txt', 'Палуба/№1 «бриг».txt'],
        }
        for codec, names in cases.items():
            with self.subTest(codec=codec):
                shutil.rmtree(self.output)
                self.output.mkdir()
                path = self.archive([(b'app/RESOURCE/' + n.encode(codec), n.encode()) for n in names])
                self.assertEqual(self.extract(path), (0, ''))
                for name in names:
                    self.assertEqual((self.output / 'RESOURCE' / name).read_text(), name)
                self.written()

    def test_mixed_archive(self):
        # One archive with a flagged UTF-8 name, a CP866 name and a CP1251 name: each is read on its own.
        path = self.archive([
            ('app/RESOURCE/Карта.txt'.encode(), b'utf-8', UTF8_FLAG),
            ('app/RESOURCE/Остров.txt'.encode('cp866'), b'cp866'),
            ('app/RESOURCE/Шлюп.txt'.encode('cp1251'), b'cp1251'),
            ('app/RESOURCE/Корсары/Ветер.txt'.encode('cp866'), b'nested cp866'),
        ])
        self.assertEqual(self.extract(path), (0, ''))
        self.assertEqual(self.written(), sorted([
            'PROGRAM', 'PROGRAM/game.c', 'RESOURCE', 'RESOURCE/Карта.txt', 'RESOURCE/Остров.txt',
            'RESOURCE/Шлюп.txt', 'RESOURCE/Корсары', 'RESOURCE/Корсары/Ветер.txt', 'engine.ini']))

    def test_unflagged_utf8_stays_utf8(self):
        path = self.archive([('app/RESOURCE/Корабль.txt'.encode(), b'unflagged utf-8')])
        self.assertEqual(self.extract(path), (0, ''))
        self.assertIn('RESOURCE/Корабль.txt', self.written())

    def test_ambiguous_name_reads_as_dos(self):
        # "вода" in CP1251 is E2 EE E4 E0, which is "тюфр" in CP866: both are Cyrillic words. The ZIP format's
        # legacy encoding (DOS) is taken, as the README says.
        path = self.archive([(b'app/RESOURCE/' + 'вода'.encode('cp1251') + b'.txt', b'x')])
        self.assertEqual(self.extract(path), (0, ''))
        self.assertIn('RESOURCE/тюфр.txt', self.written())

    def test_names_that_are_not_cyrillic_fail(self):
        for raw, shown in [
            (b'\xb0\xb1.txt', '\\xB0\\xB1.txt'),          # box drawing in CP866, "°±" in CP1251
            (b'caf\xe9.txt', 'caf\\xE9.txt'),             # Windows-1252 "café": "cafщ"/"cafй" mixes scripts
            (b'\x83e\x83X\x83g.txt', '\\x83e\\x83X\\x83g.txt'),  # Shift-JIS katakana
            (b'\xa6\xb6.txt', '\\xA6\\xB6.txt'),          # "ж╢" in CP866, "¦¶" in CP1251
        ]:
            with self.subTest(raw=raw):
                shutil.rmtree(self.output)
                self.output.mkdir()
                code, error = self.extract(self.archive([(b'app/RESOURCE/' + raw, b'x')]))
                self.assertEqual(code, 5)
                self.assertIn('app/RESOURCE/' + shown, error)
                self.written()

    def test_flagged_names_must_be_utf8(self):
        for raw in [b'\xcf\xe0\xeb\xf3\xe1\xe0', b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'a\xe2\x82']:
            with self.subTest(raw=raw):
                shutil.rmtree(self.output)
                self.output.mkdir()
                code, error = self.extract(self.archive([(b'app/RESOURCE/' + raw + b'.txt', b'x', UTF8_FLAG)]))
                self.assertEqual(code, 5, error)
                self.assertIn('entry 3 (after app/PROGRAM/game.c)', error)
                self.written()

    def test_decoded_names_are_validated(self):
        # Checks run on the decoded name: traversal, separators and control characters stay forbidden, and an
        # overlong "/" in a name without the flag is legacy text, never a separator.
        for raw in [b'..\\' + 'Карта'.encode('cp866'), 'Карта'.encode('cp866') + b'\x01.txt',
                    b'app/RESOURCE/' + 'Карта'.encode('cp866') + b'/../../../escape']:
            with self.subTest(raw=raw):
                shutil.rmtree(self.output)
                self.output.mkdir()
                self.assertEqual(self.extract(self.archive([(raw, b'evil')]))[0], 5)
                self.assertFalse((self.root / 'escape').exists())
        shutil.rmtree(self.output)
        self.output.mkdir()
        code, _ = self.extract(self.archive([(b'app/RESOURCE/x\xc0\xaf..\xc0\xafescape', b'evil')]))
        self.assertIn(code, (0, 5))
        self.assertFalse((self.root / 'escape').exists())
        for name in self.written():
            self.assertNotIn('..', name.split('/'))

    def test_listing_has_decoded_names(self):
        path = self.archive([(b'app/RESOURCE/' + 'Корабль'.encode('cp866') + b'.txt', b'x')])
        with tempfile.TemporaryDirectory() as scratch:
            code, document, error = reader.discover(lib, path, scratch)
        self.assertEqual(code, 0, error)
        self.assertEqual(document['candidates'][0]['root'], 'app/')
        self.assertEqual(document['candidates'][0]['file_count'], 3)

    def test_unicode_formats_under_c_locale(self):
        # A host Python sets a UTF-8 locale; run the reader in a process whose locale is C (ASCII): the Unicode names
        # of 7z, RAR and a flagged ZIP must still be read, as on Android, whatever locale the caller set.
        source = self.root / 'source'
        for name, data in [('app/engine.ini', b'e'), ('app/PROGRAM/game.c', b'g'),
                           ('app/RESOURCE/tavern_ГПК.tga.tx', b'r')]:
            (source / name).parent.mkdir(parents=True, exist_ok=True)
            (source / name).write_bytes(data)
        inputs = [self.archive([('app/RESOURCE/tavern_ГПК.tga.tx'.encode(), b'r', UTF8_FLAG)], 'flagged.zip')]
        if shutil.which('7z'):
            inputs.append(self.root / 'input.7z')
            subprocess.run(['7z', 'a', str(inputs[-1]), 'app'], cwd=source, check=True, stdout=subprocess.DEVNULL)
        if args.libarchive_tests:
            inputs += [self.uudecode(f'test_read_format_rar{v}_unicode.rar') for v in ('', '5')]
        script = (
            'import json, locale, os, sys, tempfile\n'
            'assert locale.setlocale(locale.LC_CTYPE) == "C", locale.setlocale(locale.LC_CTYPE)\n'
            'sys.path.insert(0, sys.argv[1])\n'
            'import reader\n'
            'lib = reader.load(sys.argv[2])\n'
            'out = []\n'
            'for path in sys.argv[3:]:\n'
            '    with tempfile.TemporaryDirectory() as staging:\n'
            '        code, error = reader.install(lib, path, staging)\n'
            '        files = [os.path.relpath(os.path.join(d, f), staging) for d, _, n in os.walk(staging) for f in n]\n'
            '    out.append([code, error, files])\n'
            'print(json.dumps(out))\n'
        )
        env = dict(os.environ, LC_ALL='C', PYTHONCOERCECLOCALE='0', PYTHONUTF8='0')
        result = json.loads(subprocess.run([sys.executable, '-I', '-c', script, str(Path(__file__).parent), LIBRARY, *map(str, inputs)],
                                           env=env, check=True, capture_output=True, text=True).stdout)
        for path, (code, error, files) in zip(inputs, result):
            with self.subTest(path=path.name):
                if path.suffix == '.rar':
                    # libarchive's fixtures hold links, which the reader refuses, naming the entry it read.
                    self.assertEqual(code, 5)
                    self.assertIn('表だよ/ファイル' if path.name == 'test_read_format_rar_unicode.rar'
                                  else 'Ⓗⓐⓡⓓ Ⓛⓘⓝⓚ.txt', error)
                    self.assertNotIn('\\x', error)
                else:
                    self.assertEqual((code, error), (0, ''))
                    self.assertIn('RESOURCE/tavern_ГПК.tga.tx', files)

    def uudecode(self, name):
        import binascii
        out, started = bytearray(), False
        for line in (args.libarchive_tests / (name + '.uu')).read_bytes().splitlines():
            if line.startswith(b'begin '):
                started = True
            elif line.strip() == b'end':
                break
            elif started and line.strip():
                count = (line[0] - 32) & 63
                out += binascii.a2b_uu(line[:1 + (count * 4 + 2) // 3])
        path = self.root / name
        path.write_bytes(out)
        return path

    def test_remove_tree_by_bytes(self):
        tree = self.root / 'tree'
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'keep').write_text('keep')
        names = [b'tavern_\x83\x8f\x8a.tga.tx', b'\xca\xe0\xf0\xf2\xe0', b'\xff\xfe']
        os.makedirs(os.path.join(bytes(tree), names[1], names[2]))
        for directory in [b'', names[1], os.path.join(names[1], names[2])]:
            Path(os.path.join(bytes(tree), directory, names[0]).decode(errors='surrogateescape')).write_bytes(b'x')
        os.symlink(bytes(outside), os.path.join(bytes(tree), names[1], b'link'))
        error = C.create_string_buffer(2048)
        self.assertEqual(lib.storm_archive_remove_tree(bytes(tree), error, len(error)), 0, error.value)
        self.assertFalse(tree.exists())
        self.assertEqual((outside / 'keep').read_text(), 'keep')
        self.assertEqual(lib.storm_archive_remove_tree(bytes(tree), error, len(error)), 0, error.value)
        self.assertEqual(lib.storm_archive_remove_tree(b'relative/tree', error, len(error)), 1)
        single = self.root / 'single'
        single.write_bytes(b'x')
        self.assertEqual(lib.storm_archive_remove_tree(bytes(single), error, len(error)), 0)
        self.assertFalse(single.exists())


if __name__ == '__main__':
    unittest.main(argv=['test_names.py', *remaining])
