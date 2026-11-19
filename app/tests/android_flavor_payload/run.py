#!/usr/bin/env python3
"""Check the per-flavor APK payload rules used by packaging (tools/android/flavors.py).

Synthetic entry lists and DEX bytes only; no APK is built or installed.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/android'))
import flavors

COMMON = ['AndroidManifest.xml', 'classes.dex', 'lib/arm64-v8a/libengine.so', 'assets/licenses/archive-libarchive-3.8.10.txt']


def dex(*names):
    return b''.join(('Lorg/stormengine/port/' + n + ';').encode() for n in names)


def apk(flavor):
    names = COMMON + ['lib/arm64-v8a/libstorm_archive.so']
    classes = ['GameLauncherActivity', *flavors.ARCHIVE_CLASSES]
    if flavor == 'github':
        names += ['lib/arm64-v8a/libstorm_torrent.so'] + ['assets/licenses/' + n for n in flavors.TORRENT_LICENSES]
        classes += flavors.LIBRARY_SOURCES
    return names, dex(*classes)


def problems(flavor, names, classes):
    return flavors.apk_problems(flavor, names, classes)


for flavor in flavors.FLAVORS:
    names, classes = apk(flavor)
    assert problems(flavor, names, classes) == [], (flavor, problems(flavor, names, classes))
    # Archive import is required in both flavors.
    without_archive = [n for n in names if not n.endswith('libstorm_archive.so')]
    assert any('libstorm_archive.so' in p for p in problems(flavor, without_archive, classes)), flavor
    for name in flavors.ARCHIVE_CLASSES:
        stripped = classes.replace(dex(name), b'')
        assert any(name in p for p in problems(flavor, names, stripped)), (flavor, name)

play_names, play_classes = apk('play')
assert any('libstorm_torrent.so' in p for p in problems('play', play_names + ['lib/arm64-v8a/libstorm_torrent.so'], play_classes))
for name in flavors.LIBRARY_SOURCES:
    assert any(name in p for p in problems('play', play_names, play_classes + dex(name))), name
for name in flavors.TORRENT_LICENSES:
    assert any(name in p for p in problems('play', play_names + ['assets/licenses/' + name], play_classes)), name
github_names, github_classes = apk('github')
assert any('libstorm_torrent.so' in p for p in problems('github', [n for n in github_names if 'torrent.so' not in n], github_classes))
assert any('TorrentDownloadService' in p for p in problems('github', github_names, github_classes.replace(dex('TorrentDownloadService'), b'')))
try:
    flavors.has_library('other')
    raise AssertionError('Unknown flavor accepted')
except ValueError:
    pass
assert set(flavors.LIBRARY_SOURCES).isdisjoint(flavors.ARCHIVE_CLASSES)
src = Path(__file__).resolve().parents[2] / 'android/game/src/org/stormengine/port'
for name in (*flavors.LIBRARY_SOURCES, *flavors.ARCHIVE_CLASSES):
    assert (src / (name + '.java')).is_file(), 'stale flavor class list: ' + name
print('PASS flavor payload: both flavors require libstorm_archive.so and the archive import classes; '
      'play rejects libstorm_torrent.so, torrent notices and every download library class; github requires them')
