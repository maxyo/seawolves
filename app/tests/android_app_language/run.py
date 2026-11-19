#!/usr/bin/env python3
"""The production GameLanguage: the app's screens speak the installed game's language (its language.ini), the
phone's while no game is installed, and an installed game whose language the app cannot speak is stated."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-app-language-') as temporary:
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary,
                    str(java / 'GameLanguage.java'), str(here / 'AppLanguageCheck.java')], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + temporary, '-cp', temporary, 'org.stormengine.port.AppLanguageCheck'], check=True)
