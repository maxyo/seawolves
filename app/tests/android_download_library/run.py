#!/usr/bin/env python3
"""Exercise the real catalog, selection gate, status mapping and screen state of the download library.

The catalog and DownloadLibraryState run unmodified; service members are extracted
unchanged. JSON objects are recording test boundaries (Android org.json is not under
test); no native transfer, networking, foreground execution or Android UI is claimed.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
app = here.parents[1]
java = app / 'android/game/src/org/stormengine/port'
service = (java / 'TorrentDownloadService.java').read_text()


def member(signature):
    start = service.index(signature)
    opening = service.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (service[end] == '{') - (service[end] == '}')
        end += 1
    return service[start:end]


methods = [
    'private void keepMetadata(', 'static void prepareSelection(', 'private void applyStatus(',
    'static String errorKind(',
]
generated = ('package org.stormengine.port;\nimport java.io.*;\nimport java.util.*;\n'
             'final class TorrentDownloadService {\nprivate static final String TAG = "StormDownloads";\nGameDownloads catalog;\n'
             'boolean stallLogged, metadataKept, phoneOffline, lastOffline; long session; String lastNetworkError = "";\n'
             'boolean online() { return !phoneOffline; }\n'
             + '\n'.join(member(signature) for signature in methods) + '\n}\n')
with tempfile.TemporaryDirectory(prefix='storm-download-catalog-') as tmp:
    out = Path(tmp)
    (out / 'TorrentDownloadService.java').write_text(generated)
    subprocess.run(['javac', '-d', tmp, str(java / 'GameDownloads.java'), str(java / 'DownloadLibraryState.java'), str(java / 'DownloadProblem.java'),
                    str(out / 'TorrentDownloadService.java'), str(java / 'ProcessMarker.java'),
                    str(here / 'DownloadLibraryCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', tmp, 'org.stormengine.port.DownloadLibraryCheck', str(out / 'data')], check=True)
