package org.stormengine.port;

import android.content.Intent;
import android.view.SurfaceView;

/**
 * Builds without the scenario driver for automated runs (every release, and debug builds
 * without build_apk.py --test-loop): the hooks do nothing. The driver is
 * android/game/testloop/on (android/game/TESTING.md).
 */
final class TestLoop {
    private TestLoop() {}

    static void launcher(GameLauncherActivity launcher, Intent intent) {}

    static void game(GameActivity game, EngineUi ui, SurfaceView surface) {}

    static void screen(String name) {}

    static void gameplay(String mode) {}

    static boolean startAtMainMenu() {
        return false;
    }
}
