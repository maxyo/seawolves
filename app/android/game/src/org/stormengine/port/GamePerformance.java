package org.stormengine.port;

import android.annotation.TargetApi;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PerformanceHintManager;
import android.util.Log;

import java.util.Arrays;

/**
 * The battery temperature for the engine's thermal lines, and an ADPF PerformanceHint session (API 31+) for the
 * engine's game thread and the renderer's thread: the engine owns frame timing, names both threads, sets the target and
 * reports each frame's CPU work through the static methods here.
 */
final class GamePerformance {
    private static volatile long targetFrameNanos =
            33_333_333L; // 30 fps until the engine sets its own.
    // PerformanceHintManager.Session on API 31+; used under sessionLock.
    private static volatile Object session;
    private static final Object sessionLock = new Object();
    private static Object
            hintManager; // PerformanceHintManager while started; guarded by sessionLock.
    private static int gameThread, renderThread; // Linux tids, 0 = unknown; guarded by sessionLock.
    private final Context context;
    private BroadcastReceiver batteryReceiver;

    /** {@code BatteryManager.EXTRA_TEMPERATURE} in tenths of a degree Celsius, or NO_TEMPERATURE. */
    private static native void nativeBatteryTemperature(int tenthsCelsius);

    /** The engine's effects::kNoBatteryTemperature: the broadcast carried no temperature. */
    private static final int NO_TEMPERATURE = -100000;

    GamePerformance(Context context) {
        this.context = context;
    }

    /**
     * Engine hook: actual CPU time of the last frame on the game thread. Under sessionLock, so stop() cannot close the
     * session during the call; anything the session still throws reaches the engine, which logs it and stops calling.
     */
    static void reportFrameWork(long nanos) {
        if (nanos <= 0 || Build.VERSION.SDK_INT < 31) return;
        synchronized (sessionLock) {
            Object current = session;
            if (current != null)
                ((PerformanceHintManager.Session) current).reportActualWorkDuration(nanos);
        }
    }

    /** Engine hook: the frame time the engine paces at; the session aims at it. */
    static void updateTargetWork(long nanos) {
        if (nanos <= 0) return;
        targetFrameNanos = nanos;
        if (Build.VERSION.SDK_INT < 31) return;
        synchronized (sessionLock) { // as reportFrameWork
            Object current = session;
            if (current == null) return;
            ((PerformanceHintManager.Session) current).updateTargetWorkDuration(nanos);
        }
        Log.i("StormPerformance", "ADPF target work " + nanos + " ns");
    }

    /** Engine hook: the game thread (Linux tid) at the start of its loop; the session is opened for it. */
    static void gameThread(int tid) {
        if (tid <= 0 || Build.VERSION.SDK_INT < 31) return;
        synchronized (sessionLock) {
            if (gameThread == tid) return;
            gameThread = tid;
            openSession();
        }
    }

    /** Engine hook: the renderer's thread (Linux tid) does part of every frame's work; the session takes it in. */
    static void addThread(int tid) {
        if (tid <= 0 || Build.VERSION.SDK_INT < 31) return;
        synchronized (sessionLock) {
            if (renderThread == tid) return;
            renderThread = tid;
            openSession();
        }
    }

    /** (Re)creates the session for the known threads once start() has the hint manager; sessionLock held. */
    @TargetApi(31)
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
                    "ADPF hint session unavailable for threads " + Arrays.toString(threads),
                    unsupported);
            return;
        }
        if (previous != null) ((PerformanceHintManager.Session) previous).close();
    }

    void start() {
        if (batteryReceiver == null) {
            BroadcastReceiver receiver =
                    new BroadcastReceiver() {
                        @Override
                        public void onReceive(Context ignored, Intent intent) {
                            nativeBatteryTemperature(
                                    intent.getIntExtra(
                                            BatteryManager.EXTRA_TEMPERATURE, NO_TEMPERATURE));
                        }
                    };
            // A protected system broadcast: no export flag needed. Sticky: the current value
            // arrives at once.
            context.registerReceiver(receiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            batteryReceiver = receiver;
        }
        if (Build.VERSION.SDK_INT >= 31) {
            PerformanceHintManager hints = context.getSystemService(PerformanceHintManager.class);
            if (hints == null || hints.getPreferredUpdateRateNanos() <= 0) {
                Log.w("StormPerformance", "ADPF hint sessions are not supported on this device");
                return;
            }
            synchronized (sessionLock) {
                hintManager = hints;
                if (session == null) openSession();
            }
        }
    }

    void stop() {
        if (batteryReceiver != null) {
            context.unregisterReceiver(batteryReceiver);
            batteryReceiver = null;
        }
        synchronized (sessionLock) {
            Object current = session;
            session = null;
            hintManager = null;
            if (current != null && Build.VERSION.SDK_INT >= 31)
                ((PerformanceHintManager.Session) current).close();
        }
    }
}
