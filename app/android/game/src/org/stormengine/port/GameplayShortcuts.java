package org.stormengine.port;

/** UI-to-engine commands: bounded, expiring, independent of a wall-clock key hold. */
final class GameplayShortcuts {
    static final long LIFETIME_MS = 30000;

    private static final class Pending {
        final String action;
        final long queuedAt;

        Pending(String action, long queuedAt) {
            this.action = action;
            this.queuedAt = queuedAt;
        }
    }

    private Pending pending;

    private void expire(long now) {
        if (pending != null && now - pending.queuedAt >= LIFETIME_MS) pending = null;
    }

    /** The game's Esc and F2, and the screens a HUD portrait / squadron tap or an event line opens: exactly the
     *  shortcuts of the engine's native queue (platform/android_shortcuts.hpp; engine tools/android/app_contract.json
     *  hud_host_input.shortcut, checked by tests/engine_contract); never anything else. */
    static final String[] ACCEPTED = {
        "mainmenu", "character", "characterscreen", "shipscreen", "journalscreen", "itemsscreen"
    };

    synchronized boolean offer(String action, long now) {
        if (!java.util.Arrays.asList(ACCEPTED).contains(action)) return false;
        expire(now);
        if (pending != null) return pending.action.equals(action);
        pending = new Pending(action, now);
        return true;
    }

    synchronized String poll(long now, boolean ready) {
        expire(now);
        if (!ready || pending == null) return null;
        String result = "shortcut:" + pending.action;
        pending = null;
        return result;
    }

    synchronized void clear() {
        pending = null;
    }
}
