#!/usr/bin/env python3
"""Run the import transaction checks (ResourceImporter.java): replacement, failure and cancel keep the old game,
process death, recovery, locking."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-resource-import-') as d:
    source = app / 'android/game/src/org/stormengine/port/ResourceImporter.java'
    check = Path(__file__).with_name('ResourceImporterTest.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    # The check starts a child JVM of itself (process death) and keeps its files in the temporary directory.
    subprocess.run(['java', '-ea', '-Djava.io.tmpdir=' + d, '-cp', d, 'ResourceImporterTest'], check=True)
