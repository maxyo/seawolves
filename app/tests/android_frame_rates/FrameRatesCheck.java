package org.stormengine.port;

import java.util.Arrays;

/** Host checks for FrameRates: the «Частота кадров» options on mocked display mode sets. */
public final class FrameRatesCheck {
    private static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    private static void offers(float[] refresh, int[] expected, int preferred, String what) {
        int[] offered = FrameRates.offered(refresh);
        check(Arrays.equals(offered, expected), what + ": offered " + Arrays.toString(offered));
        check(
                FrameRates.preferred(offered) == preferred,
                what + ": preferred " + FrameRates.preferred(offered));
    }

    public static void main(String[] args) {
        offers(new float[] {60f}, new int[] {30, 60}, 60, "60 Hz panel (the emulator)");
        offers(new float[] {60f, 90f, 120f}, new int[] {30, 60, 90, 120}, 60, "60/90/120 Hz phone");
        offers(
                new float[] {120f},
                new int[] {30, 60, 120},
                60,
                "120 Hz only: 60 is every second refresh, 90 is not offered");
        offers(
                new float[] {90f},
                new int[] {30, 90},
                30,
                "90 Hz only: no steady 60, the highest rate below it");
        offers(new float[] {59.94f}, new int[] {30, 60}, 60, "59.94 Hz counts as 60");
        offers(
                new float[] {60f, 144f},
                new int[] {30, 60, 144},
                60,
                "a 144 Hz mode offers its own rate");
        int[] phone = FrameRates.offered(new float[] {60f, 90f, 120f});
        check(FrameRates.valid(120, phone) == 120, "an offered rate is kept");
        check(FrameRates.valid(45, phone) == 60, "a rate the display cannot hold falls back to 60");
        check(
                FrameRates.valid(90, FrameRates.offered(new float[] {60f})) == 60,
                "a stored 90 on a 60 Hz display becomes 60");
        System.out.println(
                "PASS: frame rate options (60, 60/90/120, 120, 90, 59.94, 60/144 Hz displays)");
    }
}
