package org.stormengine.port;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

/** Host checks for the bounded engine-thread wait at Activity destruction. */
public final class EngineExitTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static Thread worker(Runnable body) {
        Thread thread = new Thread(body, "SDLThread");
        thread.setDaemon(true);
        thread.start();
        return thread;
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    private static long millisSince(long start) {
        return (System.nanoTime() - start) / 1_000_000;
    }

    public static void main(String[] arguments) throws Exception {
        // The UI thread is blocked for the whole wait: stay below the 5 s input-dispatch ANR limit.
        require(
                EngineExit.WAIT_MILLIS < EngineExit.SAVE_WAIT_MILLIS
                        && EngineExit.SAVE_WAIT_MILLIS < 5000,
                "Exit wait bounds must stay below the ANR limit");
        // Engine shuts down promptly: returns as soon as the thread ends.
        long start = System.nanoTime();
        require(
                EngineExit.await(worker(() -> sleep(50)), () -> false, 2000, 4000),
                "Prompt shutdown reported as stuck");
        require(
                millisSince(start) < 1000,
                "Prompt shutdown waited for the bound: " + millisSince(start));

        // Scene load that never yields: gives up at the base bound.
        CountDownLatch never = new CountDownLatch(1);
        start = System.nanoTime();
        require(
                !EngineExit.await(
                        worker(
                                () -> {
                                    try {
                                        never.await();
                                    } catch (InterruptedException ignored) {
                                    }
                                }),
                        () -> false,
                        300,
                        4000),
                "Stuck engine reported as stopped");
        long waited = millisSince(start);
        require(
                waited >= 300 && waited < 1000,
                "Stuck engine not bounded by the base wait: " + waited);

        // Save running past the base bound: waited out, then the engine stops.
        AtomicBoolean saving = new AtomicBoolean(true);
        Thread saver =
                worker(
                        () -> {
                            sleep(700);
                            saving.set(false);
                            sleep(50);
                        });
        start = System.nanoTime();
        require(EngineExit.await(saver, saving::get, 300, 4000), "Save in progress was cut");
        require(
                millisSince(start) >= 700,
                "Returned before the save finished: " + millisSince(start));

        // A save that never finishes is still bounded by the save cap.
        start = System.nanoTime();
        require(
                !EngineExit.await(
                        worker(
                                () -> {
                                    try {
                                        never.await();
                                    } catch (InterruptedException ignored) {
                                    }
                                }),
                        () -> true,
                        200,
                        600),
                "Endless save reported as stopped");
        waited = millisSince(start);
        require(
                waited >= 600 && waited < 1300,
                "Endless save not bounded by the save cap: " + waited);

        // The save ends but the engine keeps loading: only the short after-save grace.
        AtomicBoolean briefSave = new AtomicBoolean(true);
        start = System.nanoTime();
        require(
                !EngineExit.await(
                        worker(
                                () -> {
                                    sleep(400);
                                    briefSave.set(false);
                                    try {
                                        never.await();
                                    } catch (InterruptedException ignored) {
                                    }
                                }),
                        briefSave::get,
                        200,
                        4000),
                "Engine still loading after its save reported as stopped");
        waited = millisSince(start);
        require(
                waited >= 1400 && waited < 2500,
                "Wait not ended after the post-save grace: " + waited);
        never.countDown();
        System.out.println(
                "PASS: bounded engine exit (ANR-safe bounds, prompt stop, stuck load, save waited"
                        + " out, save cap, bounded grace after save)");
    }
}
