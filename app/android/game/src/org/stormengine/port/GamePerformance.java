package org.stormengine.port;

import android.content.Context;
import android.os.Build;
import android.os.PerformanceHintManager;
import android.os.PowerManager;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

/**
 * Thermal status and an ADPF PerformanceHint session for the SDL game thread.
 *
 * Thermal: API 29+ listener; status changes are logged.
 *
 * ADPF: API 31+. The session covers the SDL main thread, found by its thread name, and the renderer's
 * own thread, which the engine names by its tid once it runs ({@link #addThread(int)}; the frame's work is
 * shared by the two).
 * The engine owns frame timing (Android Frame Pacing): it sets the target with the static
 * {@link #updateTargetWork(long)} (the swap interval it paces at) and reports each frame's
 * CPU work without the present wait with {@link #reportFrameWork(long)}, both over JNI
 * (org/stormengine/port/GamePerformance, "(J)V"; addThread is "(I)V"). See android/game/TESTING.md,
 * "Platform integration hooks".
 */
final class GamePerformance {
    private static volatile long targetFrameNanos =
            33_333_333L; // 30 fps until the engine sets its own.
    private static volatile int thermalStatus;
    private static volatile Object session; // PerformanceHintManager.Session on API 31+.
    private static final Object sessionLock = new Object();
    private static Object
            hintManager; // PerformanceHintManager while started; guarded by sessionLock.
    private static int gameThread, renderThread; // Linux tids, 0 = unknown; guarded by sessionLock.
    private final Context context;
    private Object thermalListener;

    GamePerformance(Context context) {
        this.context = context;
    }

    /** Engine hook: actual CPU time of the last frame on the game thread. */
    static void reportFrameWork(long nanos) {
        Object current = session;
        if (current == null || nanos <= 0 || Build.VERSION.SDK_INT < 31) return;
        try {
            ((PerformanceHintManager.Session) current).reportActualWorkDuration(nanos);
        } catch (RuntimeException closed) {
            /* Session closed concurrently by onStop. */
        }
    }

    /** Engine hook: the frame time the engine paces at; the session aims at it. */
    static void updateTargetWork(long nanos) {
        if (nanos <= 0) return;
        targetFrameNanos = nanos;
        Object current = session;
        if (current == null || Build.VERSION.SDK_INT < 31) return;
        try {
            ((PerformanceHintManager.Session) current).updateTargetWorkDuration(nanos);
        } catch (RuntimeException closed) {
            /* Session closed concurrently by onStop. */
        }
        Log.i("StormPerformance", "ADPF target work " + nanos + " ns");
    }

    /** Engine hook: the renderer's thread (Linux tid) does part of every frame's work; the session takes it in. */
    static void addThread(int tid) {
        if (tid <= 0 || Build.VERSION.SDK_INT < 31) return;
        synchronized (sessionLock) {
            if (renderThread == tid) return;
            renderThread = tid;
            if (session != null) openSession(); // with both threads
        }
    }

    /** (Re)creates the session for the known threads; sessionLock held, API 31+. */
    private static void openSession() {
        PerformanceHintManager hints = (PerformanceHintManager) hintManager;
        if (hints == null || gameThread <= 0) return;
        Object previous = session;
        int[] threads =
                renderThread > 0 ? new int[] {gameThread, renderThread} : new int[] {gameThread};
        try {
            long target = targetFrameNanos;
            session = hints.createHintSession(threads, target);
            Log.i(
                    "StormPerformance",
                    "ADPF hint session for game thread "
                            + gameThread
                            + (renderThread > 0 ? " and render thread " + renderThread : "")
                            + " (target "
                            + target
                            + " ns): "
                            + (session != null));
        } catch (RuntimeException unsupported) {
            // A thread that exited meanwhile (the renderer restarts with the engine) is refused:
            // keep what there was.
            Log.w(
                    "StormPerformance",
                    "ADPF hint session unavailable for threads "
                            + java.util.Arrays.toString(threads),
                    unsupported);
            return;
        }
        if (previous != null) ((PerformanceHintManager.Session) previous).close();
    }

    void start() {
        if (Build.VERSION.SDK_INT >= 29 && thermalListener == null) {
            PowerManager power = context.getSystemService(PowerManager.class);
            PowerManager.OnThermalStatusChangedListener listener =
                    status -> {
                        if (status != thermalStatus)
                            Log.i("StormPerformance", "Thermal status " + status);
                        thermalStatus = status;
                    };
            thermalStatus = power.getCurrentThermalStatus();
            power.addThermalStatusListener(context.getMainExecutor(), listener);
            thermalListener = listener;
        }
        if (Build.VERSION.SDK_INT >= 31 && session == null) {
            int tid = findThread("SDLThread");
            PerformanceHintManager hints = context.getSystemService(PerformanceHintManager.class);
            if (tid > 0 && hints != null && hints.getPreferredUpdateRateNanos() > 0) {
                synchronized (sessionLock) {
                    hintManager = hints;
                    gameThread = tid;
                    openSession();
                }
            }
        }
    }

    void stop() {
        if (Build.VERSION.SDK_INT >= 29 && thermalListener != null) {
            context.getSystemService(PowerManager.class)
                    .removeThermalStatusListener(
                            (PowerManager.OnThermalStatusChangedListener) thermalListener);
            thermalListener = null;
        }
        synchronized (sessionLock) {
            Object current = session;
            session = null;
            hintManager = null;
            if (current != null && Build.VERSION.SDK_INT >= 31)
                ((PerformanceHintManager.Session) current).close();
        }
    }

    /** Linux tid of a thread in this process by its comm name; Java Thread has no tid. */
    private static int findThread(String name) {
        File[] tasks = new File("/proc/self/task").listFiles();
        if (tasks == null) return -1;
        byte[] buffer = new byte[64];
        for (File task : tasks) {
            try (FileInputStream in = new FileInputStream(new File(task, "comm"))) {
                int n = in.read(buffer);
                if (n > 0 && new String(buffer, 0, n, StandardCharsets.UTF_8).trim().equals(name))
                    return Integer.parseInt(task.getName());
            } catch (java.io.IOException | NumberFormatException gone) {
                /* Thread exited while scanning. */
            }
        }
        return -1;
    }
}
