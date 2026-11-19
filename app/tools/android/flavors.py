"""What each distribution flavor ships: one definition for packaging and APK checks.

Both flavors import the player's own game through the system picker: folders,
archives, Inno Setup installers, multi-volume and nested containers read by the
engine's libstorm_archive.so, with a choice when several game copies are found. Only
the github flavor adds the download library: torrent client, saved source sets,
their UI, the foreground service in the :downloads process and its permissions.
"""
import re

FLAVORS = ('github', 'play')
# Archive import (both flavors).
ARCHIVE_CLASSES = ('ArchiveInstaller', 'NativeArchives', 'ImportCoordinator', 'TreeImporter')
ARCHIVE_LIBRARY = 'storm_archive'
# Download library (github only).
LIBRARY_SOURCES = ('DownloadLibraryActivity', 'DownloadLibraryState', 'TorrentDownloadService', 'NativeTorrents',
                   'GameDownloads', 'DownloadWords', 'SetDiscovery')
LIBRARY_COMPONENTS = ('org.stormengine.port.DownloadLibraryActivity', 'org.stormengine.port.TorrentDownloadService')
LIBRARY_PERMISSIONS = ('android.permission.INTERNET', 'android.permission.FOREGROUND_SERVICE',
                       'android.permission.FOREGROUND_SERVICE_DATA_SYNC', 'android.permission.POST_NOTIFICATIONS',
                       'android.permission.WAKE_LOCK', 'android.permission.ACCESS_NETWORK_STATE')
TORRENT_LIBRARY = 'storm_torrent'
# Notices of the torrent client's own dependencies (the archive reader ships its own set, with the Boost both use).
TORRENT_LICENSES = ('OpenSSL-Apache-2.0.txt', 'libtorrent-BSD-3-Clause.txt', 'try_signal-license.txt')


def has_library(flavor):
    if flavor not in FLAVORS:
        raise ValueError('Unknown flavor: ' + flavor)
    return flavor == 'github'


def app_java_sources(root, flavor, ui_gallery=False, test_loop=False):
    """The application's Java sources for one build: the single list javac sees in packaging and checks."""
    game = root / 'android/game'
    sources = sorted((game / 'src').rglob('*.java'))
    if not has_library(flavor):
        sources = [source for source in sources if source.stem not in LIBRARY_SOURCES]
    sources += sorted((game / 'flavors' / flavor).rglob('*.java'))
    if ui_gallery:
        sources += sorted((game / 'debug').rglob('*.java'))
    sources += sorted((game / 'testloop' / ('on' if test_loop else 'off')).rglob('*.java'))
    return sources


def _class_present(dex, name):
    return ('Lorg/stormengine/port/' + name + ';').encode() in dex


def apk_problems(flavor, names, dex):
    """Payload errors of a packaged APK, given its entry names and concatenated classes*.dex bytes."""
    library = has_library(flavor)
    names = set(names)
    problems = []
    def native(name):
        return 'lib/arm64-v8a/lib' + name + '.so' in names
    if not native(ARCHIVE_LIBRARY):
        problems.append(f'{flavor}: missing lib{ARCHIVE_LIBRARY}.so')
    if native(TORRENT_LIBRARY) != library:
        problems.append(f'{flavor}: unexpected presence of lib{TORRENT_LIBRARY}.so' if not library
                        else f'{flavor}: missing lib{TORRENT_LIBRARY}.so')
    for name in ARCHIVE_CLASSES:
        if not _class_present(dex, name):
            problems.append(f'{flavor}: missing archive import class {name}')
    for name in LIBRARY_SOURCES:
        if _class_present(dex, name) != library:
            problems.append(f'{flavor}: unexpected presence of {name}' if not library else f'{flavor}: missing {name}')
    if not library:
        leaked = [n for n in names if re.fullmatch(r'assets/licenses/(' + '|'.join(map(re.escape, TORRENT_LICENSES)) + ')', n)]
        problems += [f'{flavor}: unexpected torrent notice {n}' for n in sorted(leaked)]
    return problems
