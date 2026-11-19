#!/usr/bin/env python3
"""Run the production UiTouchSplitter on the host: which pointer goes to the portable UI
and which to the Android views, with several fingers at once.

Only android.view.MotionEvent is a stand-in (stubs/); the splitter (with its phase
constants) is the production source. Android's own dispatch to child views and the UI
runtime's hit test are not simulated here (engine: tests/hud_land_check.cpp).
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
with tempfile.TemporaryDirectory(prefix='storm-ui-touch-') as temporary:
    sources = [str(path) for path in sorted((here / 'stubs').rglob('*.java'))]
    sources += [str(java / 'UiTouchSplitter.java'), str(here / 'UiTouchSplitterCheck.java')]
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary, *sources], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary, 'org.stormengine.port.UiTouchSplitterCheck'], check=True)
