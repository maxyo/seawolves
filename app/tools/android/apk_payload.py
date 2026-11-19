"""What the APK ships besides the engine: one definition for packaging and APK checks.

The player brings their own copy of the game in two ways. The import through the system picker takes folders,
archives, Inno Setup installers, multi-volume and nested containers, read by the engine's libstorm_archive.so, with a
choice when several game copies are found. The download library fetches the player's own torrent or magnet link of a
copy they are entitled to: the torrent client libstorm_torrent.so, saved source sets, their screen, the foreground
service in the :downloads process. The app carries no links to the game.
"""

# Archive import.
ARCHIVE_CLASSES = ('ArchiveInstaller', 'NativeArchives', 'ImportCoordinator', 'TreeImporter')
ARCHIVE_LIBRARY = 'storm_archive'
# Download library.
LIBRARY_SOURCES = ('DownloadLibraryActivity', 'DownloadLibraryState', 'TorrentDownloadService', 'NativeTorrents',
                   'GameDownloads', 'DownloadWords', 'SetDiscovery')
TORRENT_LIBRARY = 'storm_torrent'
# Notices of the torrent client's own dependencies (the archive reader ships its own set, with the Boost both use).
TORRENT_LICENSES = ('OpenSSL-Apache-2.0.txt', 'libtorrent-BSD-3-Clause.txt', 'try_signal-license.txt')


def app_java_sources(root, ui_gallery=False, test_loop=False):
    """The application's Java sources for one build: the single list javac sees in packaging and checks."""
    game = root / 'android/game'
    sources = sorted((game / 'src').rglob('*.java'))
    if ui_gallery:
        sources += sorted((game / 'debug').rglob('*.java'))
    sources += sorted((game / 'testloop' / ('on' if test_loop else 'off')).rglob('*.java'))
    return sources


def _class_present(dex, name):
    return ('Lorg/stormengine/port/' + name + ';').encode() in dex


def apk_problems(names, dex):
    """Payload errors of a packaged APK, given its entry names and concatenated classes*.dex bytes."""
    names = set(names)
    problems = []
    for library in (ARCHIVE_LIBRARY, TORRENT_LIBRARY):
        if 'lib/arm64-v8a/lib' + library + '.so' not in names:
            problems.append(f'missing lib{library}.so')
    for name in ARCHIVE_CLASSES:
        if not _class_present(dex, name):
            problems.append(f'missing archive import class {name}')
    for name in LIBRARY_SOURCES:
        if not _class_present(dex, name):
            problems.append(f'missing download library class {name}')
    for name in TORRENT_LICENSES:
        if 'assets/licenses/' + name not in names:
            problems.append(f'missing torrent client notice {name}')
    return problems
