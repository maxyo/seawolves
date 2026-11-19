package org.stormengine.port;

import java.util.Set;

/**
 * When a launcher start only has to bring the app's task back. Android adds a second launcher on
 * top of the existing task when the home screen icon is tapped while the task was begun by
 * another intent (a notification, «About» from the game, adb): the task comes to the front and a
 * new launcher is created over the game. That launcher must not open (one UI host per process:
 * it could not draw), it finishes and the game or the launcher under it is shown again.
 */
final class LauncherEntry {
    private LauncherEntry() {}

    static boolean bringsTaskBack(boolean taskRoot, String action, Set<String> categories) {
        return !taskRoot
                && "android.intent.action.MAIN".equals(action)
                && categories != null
                && categories.contains("android.intent.category.LAUNCHER");
    }
}
