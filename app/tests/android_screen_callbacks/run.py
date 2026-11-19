#!/usr/bin/env python3
"""Run real GameActivity screen callbacks against a queued UI thread and recordings.

The production method bodies and state declarations are extracted unchanged; Android's
Looper and JNI are not emulated. The tests exercise ordering and the input a screen change drops.
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
    # These selected methods contain no braces inside strings or comments.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fields = [
    'private final MobileMailbox actions = new MobileMailbox();',
    'private final GameplayShortcuts shortcuts = new GameplayShortcuts();',
    'private String screen = "";',
    'private MobileHud hud;',
]
for field in fields:
    assert field in source, field
methods = [
    'private void dropQueuedActions(',  # the purge keeps the state messages (MobileMailbox)
    'public void showMobileScreen(String name)',
]
generated = ('package org.stormengine.port;\nimport java.util.*;\n'
             ''
             'final class GameActivity extends RecordingUi {\n' + '\n'.join(fields) + '\n'
             + '\n'.join(member(signature) for signature in methods) + '\n'
             + '''
    void closeUiEditor() {}
    static final class TestLoop { static void screen(String name) {} } // the Game Loop hook (android/game/testloop/off)
    GameActivity() { hud = new MobileHud(); }
    String currentScreen() { return screen; }
    MobileHud views() { return hud; }
    List<String> commands() { List<String> out = new ArrayList<>(); for (String m; (m = actions.poll()) != null; ) out.add(m); return out; }
    void seedOldInput() {
        actions.offer("camera:zoom\t1.5"); actions.offer("hudowner:lost"); shortcuts.offer("mainmenu", 1);
    }
    // Old input goes with the screen change; a state message (MobileMailbox: "hudowner:") stays.
    boolean oldInputCleared() {
        return commands().equals(Collections.singletonList("hudowner:lost")) && shortcuts.poll(2, true) == null;
    }
}
''')
with tempfile.TemporaryDirectory(prefix='storm-screen-callbacks-java-') as temporary:
    out = Path(temporary)
    (out / 'GameActivity.java').write_text(generated)
    subprocess.run(['javac', '-source', '8', '-target', '8', '-d', temporary,
                    str(out / 'GameActivity.java'), str(here / 'ScreenCallbacksCheck.java'),
                    str(app / 'android/game/src/org/stormengine/port/GameplayShortcuts.java'),
                    str(app / 'android/game/src/org/stormengine/port/MobileMailbox.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary,
                    'org.stormengine.port.ScreenCallbacksCheck'], check=True)
