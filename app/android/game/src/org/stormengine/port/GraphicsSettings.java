package org.stormengine.port;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONException;
import org.json.JSONObject;

/** App-owned preferences of the settings screen (renderer, and one game rule); never rewrites the user's game installation.
 *  The settings screen of the portable UI shows them (state channel "appSettings") and
 *  sends them back with «Готово» (UI action "app-settings"); the engine reads them at start and
 *  re-applies the graphics effects at once ("graphics:reload").
 *  fx_preset: the one graphics choice, 0 «Низкая», 1 «Средняя», 2 «Высокая» (engine renderer_bgfx effects); on the
 *  first start the engine measures the title scene and picks it (renderer auto_preset.hpp, autoPresetChosen), again
 *  after an engine with a newer measurement version, never over the player's own choice (PresetSource). The engine
 *  reads "fx_preset_auto" (-1 the player's choice, else the stored measurement's version, 0 none); the settings
 *  screen gets it as a flag (true: the preset was measured) and leaves it out of what it sends back. The measurement
 *  may choose «Низкая» at a lower scene height (fx_auto_render_height, the settings screen shows it under the preset
 *  and leaves it out of what it sends back);
 *  texture_quality: «Качество текстур» 0 Низкое, 1 Среднее, 2 Высокое, 3 Ультра (Высокое and the neural ×2 near
 *  the camera), until the player chooses one the level of the phone's memory class (TextureMemoryClass); the
 *  resolution part at the next start, the ×2 at once (engine fx::TextureQuality).
 *  frame_rate: «Частота кадров», frames per second among FrameRates.offered for the display's refresh rates
 *  (reported to the UI as frame_rate_options), 60 by default; the engine's frame pacing follows it at once.
 *  Debuggable builds only: single fx_* effect overrides (DEBUG_KEYS, -1 = the preset's value) for bisecting
 *  rendering problems on a phone; release builds keep them developer keys of the game's engine.ini.
 *  haptics: «Вибрация», 1 (default) lets the engine's fight moments play short haptic effects (GameActivity.onHaptic).
 *  left_handed: «Под левую руку», 1 mirrors the HUD left to right (engine UI host, the pack's mirrored layout).
 *  pirate_frequency: «Частота пиратов на карте», the percentage of the game's random world-map pirate events kept
 *  (PIRATE_FREQUENCIES, default 20, five times fewer than the game); the engine's PirateEncounterPercent()
 *  reads it at every event for the reviewed script patch of encounters_map.c (engine platform/pirate_frequency.hpp has
 *  the same steps and default). An invalid stored value is logged and the default used.
 *  The graphics API the engine's device runs and why (GameActivity.renderBackendChosen) is recorded for «О программе»
 *  (renderBackend), not reported to the settings screen. */
final class GraphicsSettings {
    private static final String[] KEYS = {
        "fx_preset",
        "texture_quality",
        "frame_rate",
        "pirate_frequency",
        "ui_scale",
        "hud_transparency",
        "left_handed",
        "haptics"
    };

    /** «Размер интерфейса» (percent of the display density the UI lays out at) and «Прозрачность кнопок» (percent the HUD
     *  buttons are see-through); engine platform/display_options.hpp has the same steps and defaults. ui_scale is kept
     *  in a file, not the preferences, because the launcher's process reads it too (StormUiView). */
    static final int[] UI_SCALES = {100, 115, 130}, HUD_TRANSPARENCIES = {0, 35, 60};

    static final int UI_SCALE_DEFAULT = 100, HUD_TRANSPARENCY_DEFAULT = 0;
    static final int[] TEXTURE_QUALITIES = {0, 1, 2, 3};
    static final int TEXTURE_QUALITY_DEFAULT = 2;
    private static final String UI_SCALE_FILE = "ui-scale.txt";

    /** The layout height the screens are designed down to, less a little: a UI size that would leave less of the
     *  display's short side is not offered (ui_scale_options; the screens would overflow). */
    static final int UI_MIN_LAYOUT_DP = 340;

    /** The engine cache's size (GameActivity.measureCache), -1 until measured; reported as cache_bytes. */
    private static volatile long cacheBytes = -1;

    static void cacheMeasured(long bytes) {
        cacheBytes = bytes;
    }

    /** The UI sizes this display can show: 100 % (the screens' 48 dp targets) and the larger ones that leave at least
     *  UI_MIN_LAYOUT_DP of its short side. */
    static int[] uiScaleOffered(Context context) {
        android.util.DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        float shortDp = Math.min(metrics.widthPixels, metrics.heightPixels) / metrics.density;
        int count = 0;
        int[] offered = new int[UI_SCALES.length];
        for (int scale : UI_SCALES)
            if (scale <= UI_SCALE_DEFAULT || shortDp * 100f / scale >= UI_MIN_LAYOUT_DP)
                offered[count++] = scale;
        return java.util.Arrays.copyOf(offered, count);
    }

    static final int[] PIRATE_FREQUENCIES = {20, 50, 100};
    static final int PIRATE_FREQUENCY_DEFAULT = 20;
    private static final String TAG = "StormGraphics";

    /** Who chose fx_preset (PresetSource), the measurement version that chose it, the measurement's numbers (JSON). */
    private static final String SOURCE = "fx_preset_source",
            MEASURED = "fx_preset_measured",
            MEASUREMENT = "fx_preset_measurement";

    /** The scene height of the measured rung below «Низкая» (0: the preset's own); the engine reads it as
     *  fx_auto_render_height while the automatic choice stands (PresetSource.autoRenderHeight). */
    private static final String HEIGHT = "fx_preset_render_height";

    /** Debug overrides: key, lowest and highest value besides -1 (the preset decides). The settings screen
     *  shows a row for each key the host reports; the engine reads them like engine.ini's fx_* keys. */
    private static final Object[][] DEBUG_KEYS = {
        {"fx_msaa", 1, 4},
        {"fx_bloom", 0, 1},
        {"fx_grade", 0, 2},
        {"fx_dither", 0, 1},
        {"fx_shadows", 0, 2},
        {"fx_sea_band_limit", 0, 1},
        {"fx_sea_soft_edges", 0, 1},
        {"fx_sea_specular", 0, 1},
        {"fx_light_per_pixel", 0, 1},
        {"fx_light_point", 0, 1},
        {"fx_light_specular", 0, 1},
        {"fx_light_normal_map", 0, 1},
        {"fx_alpha_to_coverage", 0, 1},
        {"fx_anisotropy", 1, 8},
        {"fx_ao_volume", 0, 3},
        {"fx_smaa", 0, 1},
        {"fx_sharpen", 0, 1},
        {"fx_render_height", 0, 4320},
        {"fx_gpu_profile", 0, 1},
        // Error in tenths of a target pixel: -1 preset, 0 full geometry, 10/20 the engine's debug
        // choices.
        {"fx_far_lod", 0, 40},
        {"fx_lamp_shadows", 0, 16},
        {"fx_lamp_soft", 0, 1},
        {"fx_lamp_model_lift", 0, 100},
        {"fx_lamp_debug", 0, 2},
        {"fx_sail_light", 0, 300},
        {"fx_lamp_tint", 0, 100},
        // The one falloff of every light (engine fx/light_falloff.hpp): reach in %, soft core in
        // cm, level in %.
        {"fx_light_reach", 100, 400},
        {"fx_light_core", 1, 200},
        {"fx_light_level", 10, 200},
        {"fx_town_walls", 0, 1},
        {"fx_gloss", 0, 1},
        {"fx_gloss_strength", 10, 300},
        {"fx_gloss_debug", 0, 5},
        {"fx_sea_waves", 0, 1},
        {"fx_sea_waves_lift", 0, 1},
        {"fx_sea_waves_debug", 0, 2},
        {"fx_town_light", 0, 1},
        {"fx_town_light_debug", 0, 1},
        {"fx_character_relief", 0, 2},
        {"fx_character_relief_strength", 0, 100},
        // The parallax probe: the relief texture under the screen centre and its parallax, drawn by
        // the engine; and a
        // trigger whose every change of value switches that texture's parallax (the engine's user
        // exclusion list).
        {"fx_pom_probe", 0, 1},
        {"fx_pom_toggle", 0, 1},
        // The renderer's own thread (read at the game's start): 0 = render on the game thread; and
        // whether the
        // game thread runs a frame ahead of it: 0 never, 1 always (left out: chosen per frame).
        // Measurements.
        {"render_thread", 0, 1},
        {"render_overlap", 0, 1},
        // The renderer's backend (read at the game's start): 1 = OpenGL ES, 2 = Vulkan (left out or
        // 0: the engine
        // chooses per device). The engine logs the backend it runs and why ("render backend: ..."),
        // also when the
        // one asked for could not start; the diagnostics export carries that line.
        {"render_backend", 0, 2}
    };

    /** The engine's last report of its device's graphics API and why, kept in a file because «О программе» shows it
     *  from the launcher's process while the engine runs in ":game". */
    private static final String RENDER_BACKEND_FILE = "render-backend.txt";

    static void renderBackend(Context context, String backend, String reason) {
        android.util.Log.i(TAG, "render backend in use: " + backend + " (" + reason + ")");
        java.io.File file = new java.io.File(context.getFilesDir(), RENDER_BACKEND_FILE),
                temp = new java.io.File(file.getPath() + ".tmp");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(temp)) {
            out.write(
                    (backend.replace('\n', ' ') + "\n" + reason.replace('\n', ' ') + "\n")
                            .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (java.io.IOException error) {
            android.util.Log.e(TAG, "cannot record the render backend for «О программе»", error);
            return;
        }
        if (!temp.renameTo(file))
            android.util.Log.e(
                    TAG,
                    "cannot record the render backend for «О программе»: rename of "
                            + temp
                            + " failed");
    }

    /** {backend, reason} of the last device the engine made, or null before the first game start (no file yet). */
    static String[] renderBackend(Context context) {
        java.io.File file = new java.io.File(context.getFilesDir(), RENDER_BACKEND_FILE);
        if (!file.isFile()) return null;
        try {
            String[] lines =
                    new String(
                                    java.nio.file.Files.readAllBytes(file.toPath()),
                                    java.nio.charset.StandardCharsets.UTF_8)
                            .split("\n");
            if (lines.length >= 2) return new String[] {lines[0], lines[1]};
            android.util.Log.e(
                    TAG,
                    "render backend record " + file + " is malformed; «О программе» leaves it out");
        } catch (java.io.IOException error) {
            android.util.Log.e(TAG, "cannot read the render backend record " + file, error);
        }
        return null;
    }

    private static Object[] debugKey(Context context, String key) {
        if (!DebugBuild.on(context)) return null;
        for (Object[] k : DEBUG_KEYS) if (k[0].equals(key)) return k;
        return null;
    }

    private static boolean known(String key) {
        for (String k : KEYS) if (k.equals(key)) return true;
        return false;
    }

    /** Refresh rates of the default display's modes. */
    private static float[] refreshRates(Context context) {
        android.hardware.display.DisplayManager displays =
                context.getSystemService(android.hardware.display.DisplayManager.class);
        android.view.Display display =
                displays == null ? null : displays.getDisplay(android.view.Display.DEFAULT_DISPLAY);
        if (display == null) {
            android.util.Log.e("StormGraphics", "no default display: frame rates from 60 Hz");
            return new float[] {60f};
        }
        android.view.Display.Mode[] modes = display.getSupportedModes();
        float[] rates = new float[modes.length];
        for (int i = 0; i < modes.length; ++i) rates[i] = modes[i].getRefreshRate();
        return rates;
    }

    private static int valid(Context context, String key, int value) {
        if (key.equals("frame_rate"))
            return FrameRates.valid(value, FrameRates.offered(refreshRates(context)));
        if (key.equals("pirate_frequency")) return validPirateFrequency(value);
        if (key.equals("ui_scale"))
            return step(key, value, uiScaleOffered(context), UI_SCALE_DEFAULT);
        if (key.equals("hud_transparency"))
            return step(key, value, HUD_TRANSPARENCIES, HUD_TRANSPARENCY_DEFAULT);
        if (key.equals("left_handed")) return step(key, value, new int[] {0, 1}, 0);
        if (key.equals("haptics")) return step(key, value, new int[] {0, 1}, 1);
        Object[] debug = debugKey(context, key);
        if (debug != null)
            return value == -1 || (value >= (Integer) debug[1] && value <= (Integer) debug[2])
                    ? value
                    : -1;
        if (key.equals("texture_quality"))
            return step(key, value, TEXTURE_QUALITIES, TEXTURE_QUALITY_DEFAULT);
        return value >= 0 && value <= 2 ? value : 0;
    }

    /** `value` when it is one of `steps`; otherwise the default, logged. */
    private static int step(String key, int value, int[] steps, int fallback) {
        for (int s : steps) if (s == value) return value;
        android.util.Log.e(
                TAG,
                key
                        + " "
                        + value
                        + " is not one of "
                        + java.util.Arrays.toString(steps)
                        + ": the default "
                        + fallback
                        + " is used");
        return fallback;
    }

    /** «Размер интерфейса» as a factor of the display density, from its file (the default before the player chose). */
    static float uiScale(Context context) {
        return get(context, "ui_scale", UI_SCALE_DEFAULT) / 100f;
    }

    private static int readUiScale(Context context) {
        java.io.File file = new java.io.File(context.getFilesDir(), UI_SCALE_FILE);
        if (!file.isFile()) return UI_SCALE_DEFAULT;
        try {
            return valid(
                    context,
                    "ui_scale",
                    Integer.parseInt(
                            new String(
                                            java.nio.file.Files.readAllBytes(file.toPath()),
                                            java.nio.charset.StandardCharsets.UTF_8)
                                    .trim()));
        } catch (java.io.IOException | NumberFormatException error) {
            android.util.Log.e(TAG, "cannot read " + file + ": the default UI size is used", error);
            return UI_SCALE_DEFAULT;
        }
    }

    private static void writeUiScale(Context context, int value) {
        java.io.File file = new java.io.File(context.getFilesDir(), UI_SCALE_FILE),
                temp = new java.io.File(file.getPath() + ".tmp");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(temp)) {
            out.write(String.valueOf(value).getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (java.io.IOException error) {
            android.util.Log.e(TAG, "cannot store the UI size " + value + " in " + temp, error);
            return;
        }
        if (!temp.renameTo(file))
            android.util.Log.e(
                    TAG, "cannot store the UI size " + value + ": rename of " + temp + " failed");
    }

    /** The stored texture level; before the player chose one, the level of the phone's memory class
     *  (TextureMemoryClass), not stored. */
    private static int textureQuality(Context context) {
        SharedPreferences prefs = prefs(context);
        if (prefs.contains("texture_quality"))
            return prefs.getInt("texture_quality", TEXTURE_QUALITY_DEFAULT);
        return memoryClassTextureQuality(context);
    }

    private static int memoryClassLevel = -1;

    /** TextureMemoryClass's level for this phone's memory, logged once per process. */
    private static synchronized int memoryClassTextureQuality(Context context) {
        if (memoryClassLevel >= 0) return memoryClassLevel;
        android.app.ActivityManager activities =
                context.getSystemService(android.app.ActivityManager.class);
        long total = 0;
        if (activities != null) {
            android.app.ActivityManager.MemoryInfo info =
                    new android.app.ActivityManager.MemoryInfo();
            activities.getMemoryInfo(info);
            total = info.totalMem;
        }
        memoryClassLevel = TextureMemoryClass.defaultLevel(total);
        if (total <= 0)
            android.util.Log.e(
                    TAG,
                    "texture quality: the phone's memory size is unknown: the default level "
                            + memoryClassLevel);
        else
            android.util.Log.i(
                    TAG,
                    String.format(
                            java.util.Locale.ROOT,
                            "texture quality: none chosen; %.2f GiB of memory: level %d",
                            total / (1024.0 * 1024.0 * 1024.0),
                            memoryClassLevel));
        return memoryClassLevel;
    }

    static int validPirateFrequency(int value) {
        for (int step : PIRATE_FREQUENCIES) if (step == value) return value;
        android.util.Log.e(
                TAG,
                "pirate frequency "
                        + value
                        + " is not one of 20, 50, 100: the default "
                        + PIRATE_FREQUENCY_DEFAULT
                        + "% is used");
        return PIRATE_FREQUENCY_DEFAULT;
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("graphics", Context.MODE_PRIVATE);
    }

    /** Who chose the stored preset: the automatic measurement until the player changes it in the settings. */
    private static String source(Context context) {
        return prefs(context).getString(SOURCE, PresetSource.AUTO);
    }

    /** The stored value; `fallback` for a key this build does not offer and for a debug override left to the preset. */
    static int get(Context context, String key, int fallback) {
        if (key.equals("fx_preset_auto"))
            return PresetSource.autoKey(source(context), prefs(context).getInt(MEASURED, 0));
        if (key.equals("fx_auto_render_height")) return autoRenderHeight(context);
        if (debugKey(context, key) != null) {
            int value = valid(context, key, prefs(context).getInt(key, -1));
            return value < 0 ? fallback : value;
        }
        if (key.equals("frame_rate"))
            return valid(context, key, prefs(context).getInt(key, FrameRates.DEFAULT));
        if (key.equals("pirate_frequency"))
            return valid(context, key, prefs(context).getInt(key, PIRATE_FREQUENCY_DEFAULT));
        if (key.equals("ui_scale")) return readUiScale(context);
        if (key.equals("hud_transparency"))
            return valid(context, key, prefs(context).getInt(key, HUD_TRANSPARENCY_DEFAULT));
        if (key.equals("left_handed")) return valid(context, key, prefs(context).getInt(key, 0));
        if (key.equals("haptics")) return valid(context, key, prefs(context).getInt(key, 1));
        if (key.equals("texture_quality")) return valid(context, key, textureQuality(context));
        return known(key) ? valid(context, key, prefs(context).getInt(key, 0)) : fallback;
    }

    /** Every preference as the JSON of the "appSettings" state channel (debug overrides as stored, -1 = preset). */
    static String json(Context context) {
        JSONObject result = new JSONObject();
        try {
            for (String key : KEYS) result.put(key, get(context, key, 0));
            org.json.JSONArray rates = new org.json.JSONArray();
            for (int rate : FrameRates.offered(refreshRates(context))) rates.put(rate);
            result.put("frame_rate_options", rates);
            org.json.JSONArray scales = new org.json.JSONArray();
            for (int scale : uiScaleOffered(context)) scales.put(scale);
            result.put("ui_scale_options", scales);
            result.put("cache_bytes", cacheBytes);
            result.put(
                    "cache_clear_pending", GameCache.clearMarker(context.getFilesDir()).exists());
            result.put(
                    "fx_preset_auto",
                    PresetSource.AUTO.equals(source(context))
                            && prefs(context).getInt(MEASURED, 0) > 0);
            result.put("fx_auto_render_height", autoRenderHeight(context));
            if (DebugBuild.on(context))
                for (Object[] k : DEBUG_KEYS)
                    result.put(
                            (String) k[0],
                            valid(
                                    context,
                                    (String) k[0],
                                    prefs(context).getInt((String) k[0], -1)));
        } catch (JSONException impossible) {
            throw new IllegalStateException(impossible);
        }
        return result.toString();
    }

    /** Stores the values the settings screen sent; unknown keys and malformed JSON are errors. */
    static void apply(Context context, String json) throws JSONException {
        JSONObject values = new JSONObject(json);
        SharedPreferences.Editor edit = prefs(context).edit();
        int storedPreset = get(context, "fx_preset", 0);
        String source = source(context);
        String after =
                PresetSource.afterSettings(
                        source,
                        storedPreset,
                        values.has("fx_preset")
                                ? valid(context, "fx_preset", values.getInt("fx_preset"))
                                : null);
        if (!after.equals(source)) {
            edit.putString(SOURCE, after)
                    .remove(HEIGHT); // the measured height belongs to the automatic choice
            android.util.Log.i(
                    TAG,
                    "graphics preset: the player chose "
                            + values.getInt("fx_preset")
                            + " (was "
                            + storedPreset
                            + ", "
                            + source
                            + "); no measurement replaces it");
        }
        java.util.Iterator<String> keys = values.keys();
        StringBuilder overrides = new StringBuilder();
        while (keys.hasNext()) {
            String key = keys.next();
            if (!known(key) && debugKey(context, key) == null)
                throw new JSONException("unknown graphics setting " + key);
            int value = valid(context, key, values.getInt(key));
            if (key.equals("ui_scale")) {
                writeUiScale(context, value);
                continue;
            }
            edit.putInt(key, value);
            if (debugKey(context, key) != null && value >= 0)
                overrides.append(' ').append(key).append('=').append(value);
        }
        edit.apply();
        if (DebugBuild.on(context))
            android.util.Log.i(
                    TAG,
                    "debug fx overrides:"
                            + (overrides.length() == 0 ? " none (presets only)" : overrides));
    }

    /** The engine's measurement chose `preset` (measurement `version`, its numbers as JSON): stored unless the player
     *  chose a preset; true when stored. */
    static synchronized boolean measured(
            Context context, int preset, int renderHeight, int version, String report) {
        String source = source(context);
        if (!PresetSource.measurementMayStore(source)) {
            android.util.Log.w(
                    TAG,
                    "graphics preset: the measurement chose "
                            + preset
                            + " (scene height "
                            + renderHeight
                            + ", version "
                            + version
                            + ") but the player chose "
                            + get(context, "fx_preset", 0)
                            + ": kept; "
                            + report);
            return false;
        }
        if (renderHeight < 0 || renderHeight > 4320 || (renderHeight > 0 && preset != 0))
            throw new IllegalArgumentException(
                    "graphics preset: the measurement reports scene height "
                            + renderHeight
                            + " with preset "
                            + preset
                            + "; a measured height belongs to «Низкая» only");
        boolean stored =
                prefs(context)
                        .edit()
                        .putInt("fx_preset", valid(context, "fx_preset", preset))
                        .putString(SOURCE, PresetSource.AUTO)
                        .putInt(HEIGHT, renderHeight)
                        .putInt(MEASURED, version)
                        .putString(MEASUREMENT, report)
                        .commit();
        if (stored)
            android.util.Log.i(
                    TAG,
                    "graphics preset: measured "
                            + preset
                            + (renderHeight > 0 ? " at a " + renderHeight + "-line scene" : "")
                            + " (version "
                            + version
                            + ") stored; "
                            + report);
        else
            android.util.Log.e(
                    TAG,
                    "graphics preset: the measured preset "
                            + preset
                            + " could not be stored; the next start measures again");
        return stored;
    }

    private static int autoRenderHeight(Context context) {
        return PresetSource.autoRenderHeight(
                source(context), get(context, "fx_preset", 0), prefs(context).getInt(HEIGHT, 0));
    }

    /** The graphics choice for the diagnostics export. */
    static void describe(Context context, DiagnosticsReport report) {
        report.fact("preset", get(context, "fx_preset", 0))
                .fact("source", source(context))
                .fact("scene height", autoRenderHeight(context))
                .fact("measurement version", prefs(context).getInt(MEASURED, 0))
                .fact("measurement", prefs(context).getString(MEASUREMENT, "none"))
                .fact("frame_rate", get(context, "frame_rate", FrameRates.DEFAULT))
                .fact(
                        "pirate_frequency",
                        get(context, "pirate_frequency", PIRATE_FREQUENCY_DEFAULT));
    }
}
