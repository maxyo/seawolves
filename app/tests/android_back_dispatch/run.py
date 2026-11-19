#!/usr/bin/env python3
"""Run actual GameActivity BACK dispatch/navigation/lifecycle bodies on the host.

Android events and SDL are recording fakes. This checks key ownership and command
routing, not Android window/IME delivery, predictive gestures, JNI or rendering.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
app = here.parents[1]
source = (app / 'android/game/src/org/stormengine/port/GameActivity.java').read_text()


def member(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    # Selected methods have no braces in strings/comments.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fields = [
    'private long backKeyDownTime = -1;',
    'private int backKeyDeviceId;',
    'private volatile boolean resumed, windowFocused;',
    'private String screen = "";',
    'private MobileHud hud;',
    'private final LocationReadiness locationReadiness = new LocationReadiness();',
    'private final Object loadingInputLock = new Object();',
    'private final EngineUi ui = new EngineUi();',
    'private final MobileMailbox actions = new MobileMailbox();',
    'private final GameplayShortcuts shortcuts = new GameplayShortcuts();',
]
for field in fields:
    assert field in source, field
methods = [
    'private void dropQueuedActions(',  # the purge keeps the state messages (MobileMailbox)
    'public boolean dispatchKeyEvent(',
    'public void onBackPressed(',
    'private boolean backToUi(',
    'void locationLoadAction(',
    'void queueMobileAction(',
    'void requestGameplayShortcut(',
    'public void onWindowFocusChanged(',
    'protected void onPause(',
]
generated = ('package org.stormengine.port;\n'
             'final class GameActivity extends RecordingSDL {\n'
             + '\n'.join(fields) + '\n'
             + '\n'.join(member(method) for method in methods) + '\n'
             + '''
    void publishPresentation() {} void closeUiEditor() {}
    // Audio focus needs a real AudioManager; the lifecycle members only see an absent holder here.
    static final class AudioFocus{void acquire(){}void release(){}} AudioFocus audioFocus;
    int inputReleases;
    void releaseVirtualControls() { inputReleases++; }
    void revokeHudCoverage() {}
    void resumeGameplayAfterDialogs() {}
    void immersive() {}
    static void nativeLifecycle(boolean background) {} // the engine's session marker (native)
    String nextAction() {
        String shortcut = shortcuts.poll(android.os.SystemClock.uptimeMillis(), true);
        return shortcut == null ? actions.poll() : shortcut;
    }
}
''')
with tempfile.TemporaryDirectory(prefix='storm-back-dispatch-') as tmp:
    out = Path(tmp)
    (out / 'GameActivity.java').write_text(generated)
    subprocess.run(['javac', '-source', '8', '-target', '8', '-d', tmp,
                    str(out / 'GameActivity.java'), str(here / 'BackDispatchCheck.java'),
                    str(app / 'android/game/src/org/stormengine/port/GameplayShortcuts.java'),
                    str(app / 'android/game/src/org/stormengine/port/MobileMailbox.java'),
                    str(app / 'android/game/src/org/stormengine/port/LocationReadiness.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', tmp, 'org.stormengine.port.BackDispatchCheck'], check=True)
