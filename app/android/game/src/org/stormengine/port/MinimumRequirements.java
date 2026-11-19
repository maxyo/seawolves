package org.stormengine.port;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Whether the phone clearly misses the game's minimum requirements (README «Requirements»): the launcher then shows
 *  one closable warning (state launcher.belowMinimum); play is never blocked. Only signals the phone or the engine
 *  measures, no list of device names:
 *  - RAM: less than RAM_FLOOR_BYTES in total (a 3 GB phone or smaller; the game alone takes 0.6–0.9 GB);
 *  - graphics API: neither Vulkan 1.1 nor OpenGL ES 3.2 (a GPU of the GLES 3.0/3.1 generation);
 *  - speed: the engine's automatic preset measurement (renderer auto_preset.hpp, its report) showed «Низкая» on the
 *    title scene, the cheapest scene of the game, slower than LOW_FRAME_LIMIT_MS at the median. Phones of the
 *    supported GPU classes run it at 17–29 ms; a PowerVR GE8320 phone at 151 ms.
 *  The speed is known only after the game's first start that measured (the game process records it in
 *  LOW_FRAME_FILE, which the launcher's process reads); RAM and API from the first start of the launcher.
 *  Host-testable, no Android types (tests/android_minimum_requirements). */
final class MinimumRequirements {
    static final long RAM_FLOOR_BYTES = 3L << 30;

    /** Vulkan 1.1 (PackageManager.FEATURE_VULKAN_HARDWARE_VERSION) and OpenGL ES 3.2 (ConfigurationInfo.reqGlEsVersion). */
    static final int VULKAN_1_1 = 0x00401000, GLES_3_2 = 0x00030002;

    /** Below about 16 fps on the title at «Низкая» (four refreshes of 60 Hz and more): towns cost up to 1.5× the title
     *  (auto_preset.hpp), about 11 fps there. Phones of the minimum GPU class are estimated at 40–50 ms from public
     *  benchmarks; the limit leaves them a margin. */
    static final double LOW_FRAME_LIMIT_MS = 60.0;

    /** The «Низкая» median of the latest measurement, in the app's files directory. */
    static final String LOW_FRAME_FILE = "low-preset-frame-ms.txt";

    static final String RAM = "ram", GRAPHICS_API = "graphics_api", SPEED = "speed";

    private MinimumRequirements() {}

    /** The requirements the phone misses (RAM, GRAPHICS_API, SPEED), empty when it meets them or nothing says
     *  otherwise. `vulkanVersion` 0: no Vulkan; `lowFrameMs` < 0: not measured. */
    static java.util.List<String> misses(
            long totalRamBytes, int vulkanVersion, int glesVersion, double lowFrameMs) {
        java.util.List<String> misses = new java.util.ArrayList<>();
        if (totalRamBytes > 0 && totalRamBytes < RAM_FLOOR_BYTES) misses.add(RAM);
        if (vulkanVersion < VULKAN_1_1 && glesVersion < GLES_3_2) misses.add(GRAPHICS_API);
        if (lowFrameMs > LOW_FRAME_LIMIT_MS) misses.add(SPEED);
        return misses;
    }

    /** The signals and the verdict as one log line. */
    static String describe(
            long totalRamBytes, int vulkanVersion, int glesVersion, double lowFrameMs) {
        java.util.List<String> misses =
                misses(totalRamBytes, vulkanVersion, glesVersion, lowFrameMs);
        return (misses.isEmpty()
                        ? "meets the minimum requirements"
                        : "below the minimum requirements (" + String.join(", ", misses) + ")")
                + ": RAM "
                + (totalRamBytes >> 20)
                + " MB (floor "
                + (RAM_FLOOR_BYTES >> 20)
                + "), Vulkan "
                + version(vulkanVersion >> 22, (vulkanVersion >> 12) & 0x3ff)
                + ", OpenGL ES "
                + version(glesVersion >> 16, glesVersion & 0xffff)
                + ", «Низкая» on the title "
                + (lowFrameMs < 0
                        ? "not measured yet"
                        : String.format(
                                java.util.Locale.ROOT,
                                "%.1f ms (limit %.0f)",
                                lowFrameMs,
                                LOW_FRAME_LIMIT_MS));
    }

    private static String version(int major, int minor) {
        return major == 0 && minor == 0 ? "none" : major + "." + minor;
    }

    /** The median present interval of the «Низкая» rung at the preset's own scene height in the engine's automatic
     *  preset report; -1 when the report has no such rung. */
    static double lowFrameMs(String report) throws JSONException {
        JSONArray rungs = new JSONObject(report).getJSONArray("rungs");
        for (int i = 0; i < rungs.length(); i++) {
            JSONObject rung = rungs.getJSONObject(i);
            if (rung.getInt("preset") == 0 && rung.getInt("render_height") == 0)
                return rung.getDouble("p50_ms");
        }
        return -1;
    }

    /** Game process, after a measurement: its «Низкая» median for the launcher's process (no file when the report has
     *  none: the previous one stays). */
    static void recordLowFrame(java.io.File filesDir, String report)
            throws java.io.IOException, JSONException {
        double ms = lowFrameMs(report);
        if (ms < 0) return;
        java.io.File file = new java.io.File(filesDir, LOW_FRAME_FILE),
                temp = new java.io.File(file.getPath() + ".tmp");
        java.nio.file.Files.write(
                temp.toPath(),
                String.format(java.util.Locale.ROOT, "%.2f\n", ms)
                        .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        if (!temp.renameTo(file))
            throw new java.io.IOException("cannot rename " + temp + " to " + file);
    }

    /** The recorded «Низкая» median, -1 before the first measurement. A damaged record is an IOException. */
    static double recordedLowFrame(java.io.File filesDir) throws java.io.IOException {
        java.io.File file = new java.io.File(filesDir, LOW_FRAME_FILE);
        if (!file.isFile()) return -1;
        String text =
                new String(
                                java.nio.file.Files.readAllBytes(file.toPath()),
                                java.nio.charset.StandardCharsets.UTF_8)
                        .trim();
        try {
            double ms = Double.parseDouble(text);
            if (ms >= 0) return ms;
        } catch (NumberFormatException ignored) {
            // reported below
        }
        throw new java.io.IOException(
                "malformed «Низкая» frame time record " + file + ": '" + text + "'");
    }
}
