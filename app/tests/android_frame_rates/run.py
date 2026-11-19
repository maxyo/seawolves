#!/usr/bin/env python3
"""Run the «Частота кадров» option checks (FrameRates.java) on mocked display refresh rates."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='storm-frame-rates-') as d:
    source = app / 'android/game/src/org/stormengine/port/FrameRates.java'
    check = Path(__file__).with_name('FrameRatesCheck.java')
    subprocess.run(['javac', '-source', '8', '-target', '8', '-Xlint:-options', '-d', d, str(source), str(check)], check=True)
    subprocess.run(['java', '-ea', '-cp', d, 'org.stormengine.port.FrameRatesCheck'], check=True)
