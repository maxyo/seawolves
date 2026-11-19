package org.stormengine.port;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;

/** MinimumRequirements: the launcher's below-minimum verdict from RAM, the graphics API and the engine's «Низкая»
 *  measurement (reports in the engine's auto_preset.hpp format, with frame times as phones report them). */
public final class MinimumRequirementsCheck {
    private static final long GB = 1L << 30, MB = 1L << 20;
    private static final int VULKAN_1_0 = 0x00400000,
            VULKAN_1_3 = 0x00403000,
            GLES_3_0 = 0x00030000,
            GLES_3_1 = 0x00030001;

    private static String rung(int preset, int height, double p50, double p95, boolean passes) {
        return String.format(
                java.util.Locale.ROOT,
                "{\"preset\":%d,\"render_height\":%d,\"frames\":30,\"seconds\":4.62,\"fps\":6.5,\"p50_ms\":%.2f,\"p95_ms\":%.2f,\"max_ms\":353.09,\"gpu_ms\":null,\"passes\":%s,\"why\":\"p95"
                    + " present interval over the headroom limit (no GPU timers)\"}",
                preset,
                height,
                p50,
                p95,
                passes);
    }

    private static String report(int chosen, int height, String... rungs) {
        return "{\"version\":2,\"chosen\":"
                + chosen
                + ",\"render_height\":"
                + height
                + ",\"target_fps\":30,\"pacing_fps\":60,\"headroom\":1.50,"
                + "\"limit_p95_ms\":35.33,\"limit_p95_no_gpu_ms\":24.22,\"rungs\":["
                + String.join(",", rungs)
                + "]}";
    }

    private static void expect(List<String> actual, String what, String... expected) {
        if (!actual.equals(Arrays.asList(expected)))
            throw new AssertionError(
                    what + ": expected " + Arrays.asList(expected) + ", got " + actual);
    }

    public static void main(String[] args) throws Exception {
        // Galaxy A12 (PowerVR GE8320, 3751 MB, Vulkan 1.1 on the denylist: GLES 3.2): «Низкая» 151
        // ms on the title.
        String a12 =
                report(
                        0,
                        720,
                        rung(0, 0, 150.95, 308.80, false),
                        rung(0, 720, 150.22, 303.95, false));
        double a12Low = MinimumRequirements.lowFrameMs(a12);
        if (Math.abs(a12Low - 150.95) > 1e-9)
            throw new AssertionError("A12 «Низкая» median: " + a12Low);
        expect(
                MinimumRequirements.misses(3751 * MB, 0x00401000, 0x00030002, -1),
                "A12 before its first measurement: nothing to say yet");
        expect(
                MinimumRequirements.misses(3751 * MB, 0x00401000, 0x00030002, a12Low),
                "A12 measured: too slow",
                MinimumRequirements.SPEED);
        // Pixel 10 Pro (PowerVR D-Series, GLES): «Низкая» 24.9 ms on the title, the 720 rung kept;
        // meets the minimum.
        String p10 =
                report(0, 720, rung(0, 0, 24.92, 25.39, false), rung(0, 720, 32.15, 34.27, false));
        expect(
                MinimumRequirements.misses(
                        15544 * MB, 0, 0x00030002, MinimumRequirements.lowFrameMs(p10)),
                "Pixel 10 Pro meets it");
        // Galaxy S23: «Низкая» passed and higher rungs followed; the «Низкая» rung is found among
        // them.
        String s23 =
                report(
                        1,
                        0,
                        rung(0, 0, 16.7, 17.5, true),
                        rung(1, 0, 25.0, 27.0, true),
                        rung(2, 0, 33.4, 35.0, false));
        expect(
                MinimumRequirements.misses(
                        8 * GB, VULKAN_1_3, 0x00030002, MinimumRequirements.lowFrameMs(s23)),
                "Galaxy S23 meets it");
        // A report without the «Низкая» rung (none of today's: it is always measured first) says
        // nothing.
        if (MinimumRequirements.lowFrameMs(report(0, 720, rung(0, 720, 80, 90, false))) != -1)
            throw new AssertionError("no «Низкая» rung: -1");

        // The limits themselves: 3 GB phones report about 2.7–2.9 GB, 4 GB phones 3.5–3.8 GB.
        expect(
                MinimumRequirements.misses(2800 * MB, VULKAN_1_3, 0x00030002, 20),
                "a 3 GB phone",
                MinimumRequirements.RAM);
        expect(
                MinimumRequirements.misses(3 * GB, VULKAN_1_3, 0x00030002, 20),
                "exactly the floor passes");
        expect(
                MinimumRequirements.misses(0, VULKAN_1_3, 0x00030002, 20),
                "RAM unknown (0) says nothing");
        expect(
                MinimumRequirements.misses(4 * GB, VULKAN_1_0, GLES_3_1, 20),
                "Vulkan 1.0 and GLES 3.1 only",
                MinimumRequirements.GRAPHICS_API);
        expect(
                MinimumRequirements.misses(4 * GB, 0, GLES_3_0, 20),
                "no Vulkan, GLES 3.0",
                MinimumRequirements.GRAPHICS_API);
        expect(
                MinimumRequirements.misses(4 * GB, 0x00401000, GLES_3_0, 20),
                "Vulkan 1.1 is enough");
        expect(MinimumRequirements.misses(4 * GB, 0, 0x00030002, 20), "GLES 3.2 is enough");
        expect(
                MinimumRequirements.misses(4 * GB, VULKAN_1_3, 0x00030002, 60.0),
                "60 ms on «Низкая» passes");
        expect(
                MinimumRequirements.misses(4 * GB, VULKAN_1_3, 0x00030002, 60.01),
                "over 60 ms does not",
                MinimumRequirements.SPEED);
        expect(
                MinimumRequirements.misses(2 * GB, 0, GLES_3_0, 200),
                "everything at once",
                MinimumRequirements.RAM,
                MinimumRequirements.GRAPHICS_API,
                MinimumRequirements.SPEED);
        String line = MinimumRequirements.describe(3751 * MB, 0x00401000, 0x00030002, a12Low);
        if (!line.equals(
                        "below the minimum requirements (speed): RAM 3751 MB (floor 3072), Vulkan"
                                + " 1.1, OpenGL ES 3.2, «Низкая» on the title 150.9 ms (limit 60)")
                && !line.equals(
                        "below the minimum requirements (speed): RAM 3751 MB (floor 3072), Vulkan"
                                + " 1.1, OpenGL ES 3.2, «Низкая» on the title 151.0 ms (limit 60)"))
            throw new AssertionError("log line: " + line);
        if (!MinimumRequirements.describe(8 * GB, 0, GLES_3_0, -1)
                .contains("Vulkan none, OpenGL ES 3.0, «Низкая» on the title not measured yet"))
            throw new AssertionError(
                    "log line without Vulkan or a measurement: "
                            + MinimumRequirements.describe(8 * GB, 0, GLES_3_0, -1));

        // The game process records the median for the launcher's process; a report without one
        // keeps the record.
        File dir = new File(args[0]);
        if (!dir.mkdirs() && !dir.isDirectory()) throw new AssertionError("cannot create " + dir);
        if (MinimumRequirements.recordedLowFrame(dir) != -1)
            throw new AssertionError("nothing recorded yet: -1");
        MinimumRequirements.recordLowFrame(dir, a12);
        if (Math.abs(MinimumRequirements.recordedLowFrame(dir) - 150.95) > 1e-9)
            throw new AssertionError("recorded: " + MinimumRequirements.recordedLowFrame(dir));
        MinimumRequirements.recordLowFrame(dir, report(0, 720, rung(0, 720, 80, 90, false)));
        if (Math.abs(MinimumRequirements.recordedLowFrame(dir) - 150.95) > 1e-9)
            throw new AssertionError("a report without «Низкая» keeps the record");
        MinimumRequirements.recordLowFrame(dir, p10);
        if (Math.abs(MinimumRequirements.recordedLowFrame(dir) - 24.92) > 1e-9)
            throw new AssertionError("a new measurement replaces it");
        if (new File(dir, MinimumRequirements.LOW_FRAME_FILE + ".tmp").exists())
            throw new AssertionError("no temporary file left");
        Files.write(
                new File(dir, MinimumRequirements.LOW_FRAME_FILE).toPath(),
                "garbage".getBytes(StandardCharsets.UTF_8));
        try {
            MinimumRequirements.recordedLowFrame(dir);
            throw new AssertionError("a damaged record must fail loudly");
        } catch (java.io.IOException expected) {
            /* the launcher logs it */
        }
        try {
            MinimumRequirements.lowFrameMs("{\"version\":2}");
            throw new AssertionError("a report without rungs must fail loudly");
        } catch (org.json.JSONException expected) {
            /* the game process logs it */
        }
        System.out.println("minimum requirements checks passed");
    }
}
