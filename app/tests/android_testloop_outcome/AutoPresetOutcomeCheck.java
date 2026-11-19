package org.stormengine.port;

final class AutoPresetOutcomeCheck {
    private static void expect(boolean outcome, String line) {
        if (AutoPresetOutcome.of(line) != outcome)
            throw new AssertionError(
                    (outcome ? "not an outcome: " : "taken for an outcome: ") + line);
    }

    public static void main(String[] args) {
        String head =
                "1794731835.751 12674 12709 I StormEngine: [2026-11-15 00:37:15.751] [system]"
                        + " [info] ";
        // Progress of the measurement: never an outcome.
        expect(
                false,
                head
                        + "graphics auto preset: will measure on the title screen (stored"
                        + " measurement version 0, current 2; target 30 fps with 1.50x headroom:"
                        + " GPU ms x 1.50 <= 33.33 and p95 present interval <= 35.33 ms, without"
                        + " GPU timers p95 <= 24.22 ms; paced at 60 fps while measuring; relief"
                        + " generation and ray bakes held on the title until it ends; preset 0"
                        + " until then)");
        expect(false, head + "graphics auto preset: measuring preset 1");
        expect(false, head + "graphics auto preset: measuring preset 0 at a 720-line scene");
        expect(
                false,
                head
                        + "graphics auto preset: preset 2 active after 67.3 s (switch prepared and"
                        + " settled); measuring it");
        expect(
                false,
                head
                        + "graphics auto preset: preset 0: 132 frames in 3.01 s (43.9 fps), present"
                        + " ms p50 24.51 p95 33.79 max 51.99, gpu 19.08 ms: holds the target (GPU"
                        + " time x headroom within the budget)");
        expect(
                false,
                head
                        + "graphics auto preset: preset 0: 104 frames in 3.00 s (34.6 fps), present"
                        + " ms p50 31.03 p95 42.93 max 44.49, gpu 19.32 ms: misses the target (p95"
                        + " present interval over the limit)");
        expect(
                false,
                head
                        + "graphics auto preset: preset 0 at a 720-line scene: 90 frames in 3.00 s"
                        + " (30.0 fps), present ms p50 33.3 p95 34.0 max 35.0, gpu n/a (no timer"
                        + " queries): holds the target (p95 present interval within the headroom"
                        + " limit (no GPU timers))");
        // Outcomes.
        expect(
                true,
                head
                        + "graphics auto preset: chose preset 0:"
                        + " {\"version\":2,\"chosen\":0,\"render_height\":0,\"rungs\":[]}");
        expect(
                true,
                head
                        + "graphics auto preset: chose preset 0 at a 720-line scene (it misses the"
                        + " target too: nothing is lower): {}");
        expect(
                true,
                head
                        + "graphics auto preset: measurement aborted: the title screen was left;"
                        + " nothing stored, preset 0 again, the next start measures again; so far"
                        + " {}");
        expect(
                true,
                head + "graphics auto preset: not measured this time: the player chose a preset");
        expect(true, head + "graphics auto preset: off, the player chose preset 1");
        expect(
                true,
                head + "graphics auto preset: off while STORM_FX_OPTIONS overrides the settings");
        expect(true, head + "graphics auto preset: preset 1 was measured (version 2, current 2)");
        expect(
                true,
                head + "graphics auto preset: the app reports no fx_preset_auto; preset 0 stays");
        System.out.println(
                "PASS test loop: the automatic preset's outcome lines end the wait, its progress"
                        + " and rung results do not");
    }
}
