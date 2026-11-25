package org.stormengine.port;

import java.util.function.BooleanSupplier;

/**
 * Bounded wait for the engine thread when the game Activity is destroyed. SDL joins it without a limit, but the
 * engine sees the quit event only between frames and a scene load runs inside one, so an unbounded join could hold
 * the UI thread into an input ANR (5 s). An ordinary save is waited out; on {@code false} the caller kills the
 * process.
 */
final class EngineExit {
    /** Normal shutdown after the quit event takes about 0.2-0.4 s. */
    static final long WAIT_MILLIS = 3000;

    /**
     * Total cap while a save is being written. It stays below the 5 s input-dispatch
     * ANR limit: the UI thread is blocked meanwhile and any tap would otherwise end in
     * an ANR. A save still running at the cap is cut; the atomic save keeps the
     * previous file.
     */
    static final long SAVE_WAIT_MILLIS = 4500;

    private static final long SAVE_POLL_MILLIS = 50, AFTER_SAVE_MILLIS = 1000;

    private EngineExit() {}

    /** True if the engine thread ended within the bounds. */
    static boolean await(
            Thread engine, BooleanSupplier saving, long waitMillis, long saveWaitMillis)
            throws InterruptedException {
        long start = System.nanoTime();
        engine.join(waitMillis);
        boolean savedLate = false;
        while (engine.isAlive()) {
            long left = saveWaitMillis - (System.nanoTime() - start) / 1_000_000;
            if (left <= 0) break;
            if (saving.getAsBoolean()) {
                savedLate = true;
                engine.join(Math.min(left, SAVE_POLL_MILLIS));
            } else {
                // The frame that saved still has to finish before the engine sees the quit event.
                if (savedLate) engine.join(Math.min(left, AFTER_SAVE_MILLIS));
                break;
            }
        }
        return !engine.isAlive();
    }
}
