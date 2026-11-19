package org.stormengine.port;

/**
 * Launcher side of the pause menu's exit to the main menu. The engine runs once per process
 * (GameActivity.endProcess), so after the game's exit the ending :game process names its pid
 * and the launcher starts the next game Activity straight at the main menu only once the system
 * no longer lists that process: a start before would be created in the ending process. One
 * restart at a time; a request or Play while it waits is dropped by the caller. No Android
 * dependencies: host-tested by tests/android_game_restart.
 */
final class GameRestart {
    /** The game process ends within milliseconds of the request; this bounds a stuck one. */
    static final long TIMEOUT_MILLIS = 5000, POLL_MILLIS = 20;

    interface Host {
        boolean alive(int pid);

        void start();

        void failed(String reason);

        void later(Runnable step, long millis);

        long now();
    }

    private final Host host;
    private int pid = -1;
    private long since;

    GameRestart(Host host) {
        this.host = host;
    }

    boolean pending() {
        return pid >= 0;
    }

    /** False when a restart is already waiting (this request is not taken). */
    boolean request(int endingPid) {
        if (pending() || endingPid < 0) return false;
        pid = endingPid;
        since = host.now();
        poll();
        return true;
    }

    private void poll() {
        if (!host.alive(pid)) {
            pid = -1;
            host.start();
            return;
        }
        long waited = host.now() - since;
        if (waited >= TIMEOUT_MILLIS) {
            String reason =
                    "the ending game process " + pid + " is still running after " + waited + " ms";
            pid = -1;
            host.failed(reason);
            return;
        }
        host.later(this::poll, POLL_MILLIS);
    }
}
