package org.stormengine.port;

import java.util.Arrays;

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
     *  shortcuts of the engine's native queue (engine tools/android/app_contract.json hud_host_input.shortcut);
     *  never anything else. */
    static final String[] ACCEPTED = AppNames.SHORTCUTS;

    synchronized boolean offer(String action, long now) {
        if (!Arrays.asList(ACCEPTED).contains(action)) return false;
        expire(now);
        if (pending != null) return pending.action.equals(action);
        pending = new Pending(action, now);
        return true;
    }

    synchronized String poll(long now, boolean ready) {
        expire(now);
        if (!ready || pending == null) return null;
        String result = pending.action;
        pending = null;
        return result;
    }

    synchronized void clear() {
        pending = null;
    }
}
