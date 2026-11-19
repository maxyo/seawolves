package org.stormengine.port;

import java.util.Arrays;

/**
 * Pointer samples on their way to the portable UI runtime (StormUiView): any thread adds, the GL
 * thread takes them. Every sample is kept with its event time (drag velocities are measured from
 * them). Samples that arrive before the runtime exists (a tap on the launcher while its UI host is
 * still starting, seconds on a slow cold start) stay queued until it does: the runtime holds them
 * for its first frame, so the tap presses what it was aimed at instead of being lost.
 */
final class PointerQueue {
    int size;
    int[] phase = new int[16], pointer = new int[16];
    float[] x = new float[16], y = new float[16];
    long[] time = new long[16];

    void add(int newPhase, int newPointer, float newX, float newY, long newTime) {
        if (size == phase.length) {
            phase = Arrays.copyOf(phase, size * 2);
            pointer = Arrays.copyOf(pointer, size * 2);
            x = Arrays.copyOf(x, size * 2);
            y = Arrays.copyOf(y, size * 2);
            time = Arrays.copyOf(time, size * 2);
        }
        phase[size] = newPhase;
        pointer[size] = newPointer;
        x[size] = newX;
        y[size] = newY;
        time[size] = newTime;
        size++;
    }

    /** Moves every sample, oldest first, into `target` (emptied first) when the runtime is there; else keeps them. */
    boolean takeInto(PointerQueue target, boolean runtimeReady) {
        target.size = 0;
        if (!runtimeReady) return false;
        for (int i = 0; i < size; i++) target.add(phase[i], pointer[i], x[i], y[i], time[i]);
        size = 0;
        return true;
    }
}
