#!/usr/bin/env python3
"""The Java host checks: each compiles production classes of org.stormengine.port with its check and runs it.

    python3 app/tests/java_check.py NAME [ARG...]   one check of CHECKS (extra arguments go to its main)
    python3 app/tests/java_check.py --list          the names

The checks run on a desktop JVM, so the classes under test use no android.* type: the few Android types a check
needs are recording stand-ins (STUBS, android_ui_touch/stubs). org.json is Android's own (AOSP libcore) on the
classpath, the test-only jar ORG_JSON, never packaged.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP / 'tools/android'))
from java_format import pinned_jar  # noqa: E402
import app_contract  # noqa: E402

PORT = APP / 'android/game/src/org/stormengine/port'
TESTS = APP / 'tests'
# Recording stand-ins of android.util.Log and of the archive reader's tree removal (HostTreeRemoval).
STUBS = TESTS / 'android_archive_install/stubs'
TOUCH_STUBS = TESTS / 'android_ui_touch/stubs'
ORG_JSON_PATH = 'com/vaadin/external/google/android-json/0.0.20131108.vaadin1/android-json-0.0.20131108.vaadin1.jar'
ORG_JSON = ('android-json-0.0.20131108.vaadin1.jar',
            (f'https://repo1.maven.org/maven2/{ORG_JSON_PATH}',
             f'https://maven-central.storage-download.googleapis.com/maven2/{ORG_JSON_PATH}'),
            'dfb7bae2f404cfe0b72b4d23944698cb716b7665171812a0a4d0f5926c0fac79')

# name (the directory under tests/ that holds the check): (check file, production classes, options).
# Options: stubs (directories of stand-ins), args (passed to main; "{work}" is the class directory, which is also
# java.io.tmpdir), main (default: org.stormengine.port.<check file's name>).
CHECKS = {
    'android_app_language': ('AppLanguageCheck.java', ['GameLanguage'], {}),
    'android_archive_install': ('ArchiveInstallerCheck.java', ['ResourceImporter', 'ArchiveInstaller', 'ArchiveStatus', 'Streams',
                                                         'ZipDirectory'],
                                {'stubs': [STUBS], 'args': ['{work}/data']}),
    'android_audio_focus': ('AudioFocusStateTest.java', ['AudioFocusState'], {}),
    'android_back_dispatch': ('BackDispatchCheck.java', ['BackDispatch'], {}),
    'android_camera_drag': ('CameraDragCheck.java', ['CameraDrag'], {}),
    'android_camera_gesture': ('CameraGestureCheck.java', ['UiTouchSplitter', 'CameraGesture'],
                               {'stubs': [TOUCH_STUBS]}),
    'android_diagnostics_export': ('DiagnosticsReportCheck.java', ['DiagnosticsReport'], {}),
    'android_download_library': ('DownloadLibraryCheck.java',
                                 ['GameDownloads', 'DownloadLibraryState', 'DownloadProblem', 'TorrentStatus',
                                  'DurableFile', 'ResourceImporter', 'ArchiveStatus', 'Streams'],
                                 {'stubs': [STUBS / 'android', STUBS / 'org/stormengine',
                                            TESTS / 'android_resource_import/stubs'],
                                  'args': ['{work}/data']}),
    'android_engine_exit': ('EngineExitTest.java', ['EngineExit'], {}),
    'android_frame_rates': ('FrameRatesCheck.java', ['FrameRates'], {}),
    'android_game_cache': ('GameCacheCheck.java', ['GameCache', 'Trees'], {'args': ['{work}']}),
    'android_game_restart': ('GameRestartTest.java', ['GameRestart'], {}),
    'android_import_lock': ('ImportLockCheck.java', ['ResourceImporter', 'ArchiveStatus'],
                            {'stubs': [STUBS / 'android', STUBS / 'org/stormengine',
                                       TESTS / 'android_resource_import/stubs'],
                             'args': ['{work}']}),
    'android_layers': ('LayersCheck.java', ['MobileResources', 'Streams', 'Trees'],
                       {'stubs': [STUBS / 'android'], 'args': ['{work}']}),
    'android_launcher_entry': ('LauncherEntryCheck.java', ['LauncherEntry'], {}),
    'android_launcher_start': ('LauncherStartCheck.java', ['PointerQueue'], {}),
    'android_location_readiness': ('LocationReadinessCheck.java', ['LocationReadiness', 'MobileMailbox', 'AudioFocusState'],
                                   {}),
    'android_mailbox': ('MobileMailboxCheck.java', ['MobileMailbox', 'AudioFocusState'], {}),
    'android_menu_transition': ('MenuTransitionCheck.java', ['MenuTransition', 'LocationReadiness', 'MobileMailbox',
                                                         'AudioFocusState'], {}),
    'android_minimum_requirements': ('MinimumRequirementsCheck.java', ['MinimumRequirements', 'DurableFile'],
                                     {'stubs': [STUBS], 'args': ['{work}/files']}),
    'android_preset_source': ('PresetSourceCheck.java', ['PresetSource'], {}),
    # The check starts a child JVM of itself (process death) and keeps its files in java.io.tmpdir. NativeArchives
    # stands in for the reader's tree removal (HostTreeRemoval).
    'android_resource_import': ('ResourceImporterTest.java', ['ResourceImporter', 'ArchiveStatus'],
                                {'stubs': [STUBS / 'android', STUBS / 'org/stormengine',
                                           TESTS / 'android_resource_import/stubs'],
                                 'main': 'ResourceImporterTest'}),
    # Extra arguments: real save files (kept outside the repository) to round-trip as well. The engine's save check
    # is a stand-in (stubs): the reader itself is the engine's save_check.
    'android_save_restore': ('SaveRestoreCheck.java', ['SaveArchive', 'SaveExport', 'SaveRestore', 'Streams', 'ZipDirectory'],
                             {'stubs': [TESTS / 'android_save_restore/stubs']}),
    'android_shortcuts': ('GameplayShortcutsCheck.java', ['GameplayShortcuts', 'AppNames'], {}),
    'android_streams': ('StreamsCheck.java', ['Streams'], {}),
    'android_texture_memory_class': ('TextureMemoryClassCheck.java', ['TextureMemoryClass'], {}),
    'android_trees': ('TreesCheck.java', ['Trees'], {'args': ['{work}']}),
    'android_ui_touch': ('UiTouchSplitterCheck.java', ['UiTouchSplitter'], {'stubs': [TOUCH_STUBS]}),
}


def run(name, extra=()):
    check, classes, options = CHECKS[name]
    check = TESTS / name / check
    with tempfile.TemporaryDirectory(prefix=f'storm-{name}-') as directory:
        work = Path(directory)
        classpath = str(pinned_jar(*ORG_JSON))
        generated = app_contract.write_java(APP.parent / 'engine', work / 'contract')
        sources = [generated.get(cls, PORT / (cls + '.java')) for cls in classes]
        sources += [path for stub in options.get('stubs', ()) for path in sorted(Path(stub).rglob('*.java'))]
        subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-Xlint:-options', '-cp', classpath,
                        '-d', str(work), *map(str, sources), str(check)], check=True)
        args = [arg.replace('{work}', str(work)) for arg in options.get('args', ())] + list(extra)
        return subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + str(work), '-cp', f'{work}:{classpath}',
                               options.get('main', 'org.stormengine.port.' + check.stem), *args]).returncode


if __name__ == '__main__':
    if sys.argv[1:] == ['--list']:
        print('\n'.join(CHECKS))
    elif len(sys.argv) < 2 or sys.argv[1] not in CHECKS:
        sys.exit(__doc__ + '\nchecks: ' + ' '.join(CHECKS))
    else:
        sys.exit(run(sys.argv[1], sys.argv[2:]))
