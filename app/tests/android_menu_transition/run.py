#!/usr/bin/env python3
"""Run production HUD menu-transition bodies with a virtual Android scheduler.

The screen gate, touch dispatch, rebuild and detach bodies are extracted intact.
Only rendering/platform/native services are recording boundaries.
"""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
app = here.parents[1]
source = (app / 'android/game/src/org/stormengine/port/MobileHud.java').read_text()


def member(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    # Selected members contain no braces inside comments/string literals.
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fields = [
    'private final GameActivity game;',
    'private String screen = "";',
    'private boolean emptyScreenPending;',
    'private final Runnable emptyScreenCommit = this::finishEmptyScreen;',
    'private boolean uiHud;',
    'private LocationReadiness.Snapshot readiness;',
    'private final java.util.ArrayList<MotionEvent> heldTouches = new java.util.ArrayList<>();',
]
for field in fields:
    assert field in source, field
methods = [
    'void showScreen(', 'private void finishEmptyScreen(', 'private void deliverHeldTouches(',
    'boolean menuTransitionPending(', 'public boolean dispatchTouchEvent(',
    'protected void onDetachedFromWindow(', 'private void applyScreen(',
    'private void rebuild(',
]
generated = ('package org.stormengine.port;\nimport java.util.*;\n'
             'final class MobileHud extends FrameLayout {\n' + '\n'.join(fields) + '\n'
             + '\n'.join(member(method) for method in methods) + '\n'
             + '''
    // Touches reach the views of this layout through the splitter; here nothing belongs to the portable UI.
    interface Touches { boolean dispatch(MotionEvent event); }
    final Touches touches=event->superDispatch(event);
    boolean superDispatch(MotionEvent event) { return super.dispatchTouchEvent(event); }
    JSONObject dialogue; boolean loading,cinematic;
    final ArrayList<String> rendered=new ArrayList<>();
    // An open interface shows in the portable UI: rebuild presents it with no Android view.
    MobileHud() { game=new GameActivity(); game.presented=()->{ if(!screen.isEmpty()&&(readiness==null||!readiness.blocks())){rendered.add(screen);background=1;} }; }
    // Stock artwork is native-owned; the app barrier renders no Android view.
    void setReadiness(LocationReadiness.Snapshot value) { readiness=value;rebuild(); }
    void gameplay() { rendered.add("gameplay"); }
    void refresh() { rebuild(); }
    String screen() { return screen; }
    Runnable pendingCallback() { return emptyScreenCommit; }
    int releases() { return game.releases; }
}
''')
with tempfile.TemporaryDirectory(prefix='storm-menu-transition-') as temporary:
    out = Path(temporary)
    (out / 'MobileHud.java').write_text(generated)
    subprocess.run(['javac', '-source', '8', '-target', '8', '-d', temporary,
                    str(out / 'MobileHud.java'), str(here / 'MenuTransitionCheck.java'),
                    str(app / 'android/game/src/org/stormengine/port/LocationReadiness.java')], check=True)
    subprocess.run(['java', '-ea', '-cp', temporary,
                    'org.stormengine.port.MenuTransitionCheck'], check=True)
