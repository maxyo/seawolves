package org.stormengine.port;

/** The production CameraDrag: no motion is lost to SDL's whole-pixel relative mouse. */
public final class CameraDragCheck {
    public static void main(String[] args) {
        CameraDrag drag = new CameraDrag();
        // A slow drag on a fast digitizer: 200 events of 0.6 px. Passed on one by one, SDL's
        // cast made every one of them 0 (the device repro: no turn at all for 120 px).
        int x = 0;
        for (int i = 0; i < 200; i++) x += drag.take(0.6f, 0)[0];
        assert Math.abs(x - 120) <= 1 : "slow drag kept: " + x;
        // The stick finger moving: every other event carries no camera motion, the rest a fraction.
        drag.reset();
        int y = 0;
        for (int i = 0; i < 300; i++) y += drag.take(0, i % 2 == 0 ? 0f : -0.4f)[1];
        assert Math.abs(y + 60) <= 1 : "two-finger drag kept: " + y;
        // Whole moves pass at once; a fraction never leaves alone; reset forgets the remainder.
        drag.reset();
        int[] whole = drag.take(12.75f, -3.5f);
        assert whole[0] == 12 && whole[1] == -3 : "whole counts now";
        int[] rest = drag.take(0.25f, -0.5f);
        assert rest[0] == 1 && rest[1] == -1 : "the remainder completes the next count";
        assert drag.take(0.9f, 0.9f)[0] == 0 : "a fraction waits";
        drag.reset();
        assert drag.take(0.2f, 0)[0] == 0 : "a new drag does not inherit the old fraction";
        System.out.println(
                "PASS camera drag: sub-pixel motion kept across events and fingers, whole counts out, reset per drag");
    }
}
