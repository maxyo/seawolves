#!/usr/bin/env python3
"""Run the save restore checks: an archive of «Отправить сохранение» / «Отправить все сохранения»
restored byte for byte into an empty app (export → restore round trip), a bare save file, the profile
it goes to, conflicts (replace / keep both, names compared the engine's way), and every refusal: not a
save, a damaged archive, unsafe entry names, no space; nothing half-written is ever left in SaveData.

--save adds real save files (kept outside the repository) to the round trip.
This does not run Android: the picker, the content resolver and the dialogs are checked on a device.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--save', type=Path, action='append', default=[], help='a real save file to round-trip as well')
args = parser.parse_args()
app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-java-save-restore-') as d:
    port = app/'android/game/src/org/stormengine/port'
    check = Path(__file__).with_name('SaveRestoreCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(port/'SaveArchive.java'),
                    str(port/'SaveExport.java'), str(port/'SaveRestore.java'), str(check)], check=True)
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', d, 'org.stormengine.port.SaveRestoreCheck',
                    *[str(p.resolve()) for p in args.save]], check=True)
