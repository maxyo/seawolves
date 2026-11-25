package org.stormengine.port;

import java.util.Collections;
import java.util.Set;

public final class LauncherEntryCheck {
    public static void main(String[] args) {
        final String main = "android.intent.action.MAIN";
        final Set<String> launcher = Collections.singleton("android.intent.category.LAUNCHER");
        // The home screen icon over a task begun by another intent: the task comes back, no second
        // launcher.
        assert LauncherEntry.bringsTaskBack(false, main, launcher);
        // The task's own first launcher always opens.
        assert !LauncherEntry.bringsTaskBack(true, main, launcher);
        // Launcher starts with a purpose open even over the game: About from the game, a finished
        // download.
        assert !LauncherEntry.bringsTaskBack(false, null, null);
        assert !LauncherEntry.bringsTaskBack(false, main, Collections.emptySet());
        assert !LauncherEntry.bringsTaskBack(false, "android.intent.action.VIEW", launcher);
        System.out.println(
                "PASS launcher entry: an icon start over the running task brings it back; root and purposeful starts open");
    }
}
