#!/usr/bin/env python3
"""Run the production CameraGesture behind the production UiTouchSplitter on the host: one
finger looks, two pinch, lifting a finger never turns or zooms the camera, and a thumb on the
UI's move stick plus a finger elsewhere is a look, not a pinch.

Only android.view.MotionEvent is a stand-in (tests/android_ui_touch/stubs). The engine's side
(the pinch factor applied to each camera's own zoom within the game's limits) is the engine's
tests/android_camera_zoom.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
java = here.parents[1] / 'android/game/src/org/stormengine/port'
stubs = here.parent / 'android_ui_touch/stubs'
with tempfile.TemporaryDirectory(prefix='storm-camera-gesture-') as temporary:
    sources = [str(path) for path in sorted(stubs.rglob('*.java'))]
    sources += [str(java / 'UiTouchSplitter.java'), str(java / 'CameraGesture.java'), str(here / 'CameraGestureCheck.java')]
    subprocess.run(['javac', '-source', '8', '-target', '8', '-nowarn', '-d', temporary, *sources], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary, 'org.stormengine.port.CameraGestureCheck'], check=True)
