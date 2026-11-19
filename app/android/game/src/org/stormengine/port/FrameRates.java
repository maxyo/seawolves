package org.stormengine.port;

import java.util.Arrays;
import java.util.TreeSet;

/** The frame rates «Частота кадров» offers (GraphicsSettings key frame_rate): 30, 60, 90, 120 and each refresh
 *  rate of the display's own modes, where one of the display's refresh rates is a whole multiple of it (a frame
 *  every n-th refresh, so the engine's frame pacing can hold it steadily). 60 by default; on a display that cannot
 *  show 60 steadily the highest offered rate below it, else the lowest. No Android types: host-tested
 *  (tests/android_frame_rates). */
final class FrameRates {
    static final int DEFAULT = 60;
    private static final int[] CANDIDATES = {30, 60, 90, 120};

    /** A refresh rate counts as a multiple of a frame rate within this many Hz per refresh (59.94 Hz shows 60). */
    private static final double TOLERANCE_HZ = 0.5;

    private FrameRates() {}

    static int[] offered(float[] refreshRates) {
        TreeSet<Integer> candidates = new TreeSet<>();
        for (int rate : CANDIDATES) candidates.add(rate);
        for (float refresh : refreshRates) if (refresh >= 24) candidates.add(Math.round(refresh));
        TreeSet<Integer> result = new TreeSet<>();
        for (int rate : candidates)
            for (float refresh : refreshRates) {
                long multiple = Math.round(refresh / rate);
                if (multiple >= 1 && Math.abs(refresh / multiple - rate) <= TOLERANCE_HZ) {
                    result.add(rate);
                    break;
                }
            }
        int[] rates = new int[result.size()];
        int i = 0;
        for (int rate : result) rates[i++] = rate;
        return rates;
    }

    static int preferred(int[] offered) {
        int best = -1;
        for (int rate : offered) if (rate <= DEFAULT && rate > best) best = rate;
        if (best > 0) return best;
        return offered.length > 0 ? offered[0] : DEFAULT;
    }

    /** `value` when it is offered, otherwise the preferred rate. */
    static int valid(int value, int[] offered) {
        return Arrays.stream(offered).anyMatch(rate -> rate == value) ? value : preferred(offered);
    }
}
