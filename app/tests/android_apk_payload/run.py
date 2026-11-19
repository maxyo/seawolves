#!/usr/bin/env python3
"""Check the APK payload rules used by packaging (tools/android/apk_payload.py).

Synthetic entry lists and DEX bytes only; no APK is built or installed.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/android'))
import apk_payload

NAMES = ['AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libengine.so', 'assets/licenses/archive-libarchive-3.8.10.txt',
         'lib/arm64-v8a/libstorm_archive.so', 'lib/arm64-v8a/libstorm_torrent.so',
         *('assets/licenses/' + n for n in apk_payload.TORRENT_LICENSES)]


def dex(*names):
    return b''.join(('Lorg/stormengine/port/' + n + ';').encode() for n in names)


CLASSES = dex('GameLauncherActivity', *apk_payload.ARCHIVE_CLASSES, *apk_payload.LIBRARY_SOURCES)
problems = apk_payload.apk_problems

assert problems(NAMES, CLASSES) == [], problems(NAMES, CLASSES)
for library in ('libstorm_archive.so', 'libstorm_torrent.so'):
    assert any(library in p for p in problems([n for n in NAMES if not n.endswith(library)], CLASSES)), library
for name in (*apk_payload.ARCHIVE_CLASSES, *apk_payload.LIBRARY_SOURCES):
    assert any(name in p for p in problems(NAMES, CLASSES.replace(dex(name), b''))), name
for name in apk_payload.TORRENT_LICENSES:
    assert any(name in p for p in problems([n for n in NAMES if not n.endswith(name)], CLASSES)), name
assert set(apk_payload.LIBRARY_SOURCES).isdisjoint(apk_payload.ARCHIVE_CLASSES)
src = Path(__file__).resolve().parents[2] / 'android/game/src/org/stormengine/port'
for name in (*apk_payload.LIBRARY_SOURCES, *apk_payload.ARCHIVE_CLASSES):
    assert (src / (name + '.java')).is_file(), 'stale payload class list: ' + name
print('PASS APK payload: libstorm_archive.so, libstorm_torrent.so, the archive import and download library classes '
      'and the torrent client notices are required')
