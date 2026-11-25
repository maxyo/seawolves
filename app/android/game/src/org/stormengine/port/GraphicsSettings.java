package org.stormengine.port;

import android.app.ActivityManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.display.DisplayManager;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Iterator;
import java.util.Locale;

/**
 * The app-owned preferences of the settings screen (graphics, interface, one game rule), stored in the app and read
 * by the engine; never written into the player's game. The keys and their meaning: android/game/README.md.
 */
final class GraphicsSettings {
    static final int[] TEXTURE_QUALITIES = {0, 1, 2};
    static final int TEXTURE_QUALITY_DEFAULT = 2;

    /** «Размер интерфейса» and «Подробная диагностика» are kept in files, not the preferences, because both the
     *  launcher's process and the game's use them (StormUiView; the About screen while the game runs), and a
     *  process reads its preferences only once. */
    private static final String UI_SCALE_FILE = "ui-scale.txt",
            DIAGNOSTICS_FILE = "diagnostics-detailed.txt";

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
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        float shortDp = Math.min(metrics.widthPixels, metrics.heightPixels) / metrics.density;
        int count = 0;
        int[] offered = new int[AppNames.STEPS_UI_SCALE.length];
        for (int scale : AppNames.STEPS_UI_SCALE)
            if (scale <= AppNames.DEFAULT_UI_SCALE || shortDp * 100f / scale >= UI_MIN_LAYOUT_DP)
                offered[count++] = scale;
        return Arrays.copyOf(offered, count);
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

    /** The engine's last report of its device's graphics API and why, kept in a file because «О программе» shows it
     *  from the launcher's process while the engine runs in ":game". */
    private static final String RENDER_BACKEND_FILE = "render-backend.txt";

    static void renderBackend(Context context, String backend, String reason) {
        Log.i(TAG, "render backend in use: " + backend + " (" + reason + ")");
        try {
            DurableFile.write(
                    new File(context.getFilesDir(), RENDER_BACKEND_FILE),
                    (backend.replace('\n', ' ') + "\n" + reason.replace('\n', ' ') + "\n")
                            .getBytes(StandardCharsets.UTF_8));
        } catch (IOException error) {
            Log.e(TAG, "cannot record the render backend for «О программе»", error);
        }
    }

    /** {backend, reason} of the last device the engine made, or null before the first game start (no file yet). */
    static String[] renderBackend(Context context) {
        File file = new File(context.getFilesDir(), RENDER_BACKEND_FILE);
        if (!file.isFile()) return null;
        try {
            String[] lines =
                    new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8)
                            .split("\n");
            if (lines.length >= 2) return new String[] {lines[0], lines[1]};
            Log.e(
                    TAG,
                    "render backend record " + file + " is malformed; «О программе» leaves it out");
        } catch (IOException error) {
            Log.e(TAG, "cannot read the render backend record " + file, error);
        }
        return null;
    }

    /** The values the settings screen offers for a debug override (AppNames.DEBUG_KEYS; -1 = the preset decides),
     *  or null for another key and in a release build. */
    private static int[] debugValues(Context context, String key) {
        if (!DebugBuild.on(context)) return null;
        for (int i = 0; i < AppNames.DEBUG_KEYS.length; ++i)
            if (AppNames.DEBUG_KEYS[i].equals(key)) return AppNames.DEBUG_VALUES[i];
        return null;
    }

    private static boolean known(String key) {
        for (String k : AppNames.SETTINGS) if (k.equals(key)) return true;
        return false;
    }

    /** Refresh rates of the default display's modes. */
    private static float[] refreshRates(Context context) {
        DisplayManager displays = context.getSystemService(DisplayManager.class);
        Display display = displays == null ? null : displays.getDisplay(Display.DEFAULT_DISPLAY);
        if (display == null) {
            Log.e("StormGraphics", "no default display: frame rates from 60 Hz");
            return new float[] {60f};
        }
        Display.Mode[] modes = display.getSupportedModes();
        float[] rates = new float[modes.length];
        for (int i = 0; i < modes.length; ++i) rates[i] = modes[i].getRefreshRate();
        return rates;
    }

    private static int valid(Context context, String key, int value) {
        if (key.equals(AppNames.SETTING_FRAME_RATE))
            return FrameRates.valid(value, FrameRates.offered(refreshRates(context)));
        if (key.equals(AppNames.SETTING_PIRATE_FREQUENCY)) return validPirateFrequency(value);
        if (key.equals(AppNames.SETTING_UI_SCALE))
            return step(key, value, uiScaleOffered(context), AppNames.DEFAULT_UI_SCALE);
        if (key.equals(AppNames.SETTING_HUD_TRANSPARENCY))
            return step(
                    key, value, AppNames.STEPS_HUD_TRANSPARENCY, AppNames.DEFAULT_HUD_TRANSPARENCY);
        if (key.equals(AppNames.SETTING_LEFT_HANDED))
            return step(key, value, AppNames.STEPS_LEFT_HANDED, AppNames.DEFAULT_LEFT_HANDED);
        if (key.equals(AppNames.SETTING_HAPTICS))
            return step(key, value, AppNames.STEPS_HAPTICS, AppNames.DEFAULT_HAPTICS);
        if (key.equals(AppNames.SETTING_DIAGNOSTICS_DETAILED))
            return step(
                    key,
                    value,
                    AppNames.STEPS_DIAGNOSTICS_DETAILED,
                    AppNames.DEFAULT_DIAGNOSTICS_DETAILED);
        int[] debug = debugValues(context, key);
        if (debug != null) return step(key, value, debug, -1);
        if (key.equals(AppNames.SETTING_TEXTURE_QUALITY))
            return step(key, value, TEXTURE_QUALITIES, TEXTURE_QUALITY_DEFAULT);
        if (TextureProcessing.isKey(key))
            return value == 0 || value == 1
                    ? value
                    : step(key, value, new int[] {0, 1}, TextureProcessing.DEFAULT);
        return value >= 0 && value <= 2 ? value : 0;
    }

    /** `value` when it is one of `steps`; otherwise the default, logged. */
    private static int step(String key, int value, int[] steps, int fallback) {
        for (int s : steps) if (s == value) return value;
        Log.e(
                TAG,
                key
                        + " "
                        + value
                        + " is not one of "
                        + Arrays.toString(steps)
                        + ": the default "
                        + fallback
                        + " is used");
        return fallback;
    }

    /** «Размер интерфейса» as a factor of the display density, from its file (the default before the player chose). */
    static float uiScale(Context context) {
        return get(context, AppNames.SETTING_UI_SCALE, AppNames.DEFAULT_UI_SCALE) / 100f;
    }

    /** «Подробная диагностика» (About screen): off until the player turns it on. */
    static boolean detailedDiagnostics(Context context) {
        return readFile(
                        context,
                        DIAGNOSTICS_FILE,
                        AppNames.SETTING_DIAGNOSTICS_DETAILED,
                        AppNames.DEFAULT_DIAGNOSTICS_DETAILED)
                != 0;
    }

    static void detailedDiagnostics(Context context, boolean on) {
        writeFile(context, DIAGNOSTICS_FILE, AppNames.SETTING_DIAGNOSTICS_DETAILED, on ? 1 : 0);
        Log.i(TAG, "detailed diagnostics " + (on ? "on" : "off"));
    }

    private static int readUiScale(Context context) {
        return readFile(
                context, UI_SCALE_FILE, AppNames.SETTING_UI_SCALE, AppNames.DEFAULT_UI_SCALE);
    }

    /** A setting kept in a file of its own: its value, or `fallback` before the player chose one. */
    private static int readFile(Context context, String name, String key, int fallback) {
        File file = new File(context.getFilesDir(), name);
        if (!file.isFile()) return fallback;
        try {
            return valid(
                    context,
                    key,
                    Integer.parseInt(
                            new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8)
                                    .trim()));
        } catch (IOException | NumberFormatException error) {
            Log.e(TAG, "cannot read " + file + ": the default " + key + " is used", error);
            return fallback;
        }
    }

    private static void writeFile(Context context, String name, String key, int value) {
        try {
            DurableFile.write(
                    new File(context.getFilesDir(), name),
                    String.valueOf(value).getBytes(StandardCharsets.UTF_8));
        } catch (IOException error) {
            Log.e(TAG, "cannot store " + key + " " + value, error);
        }
    }

    /** The stored texture level; before the player chose one, the level of the phone's memory class
     *  (TextureMemoryClass), not stored. */
    private static int textureQuality(Context context) {
        SharedPreferences prefs = prefs(context);
        if (prefs.contains(AppNames.SETTING_TEXTURE_QUALITY))
            return prefs.getInt(AppNames.SETTING_TEXTURE_QUALITY, TEXTURE_QUALITY_DEFAULT);
        return memoryClassTextureQuality(context);
    }

    private static int memoryClassLevel = -1;

    /** TextureMemoryClass's level for this phone's memory, logged once per process. */
    private static synchronized int memoryClassTextureQuality(Context context) {
        if (memoryClassLevel >= 0) return memoryClassLevel;
        ActivityManager activities = context.getSystemService(ActivityManager.class);
        long total = 0;
        if (activities != null) {
            ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
            activities.getMemoryInfo(info);
            total = info.totalMem;
        }
        memoryClassLevel = TextureMemoryClass.defaultLevel(total);
        if (total <= 0)
            Log.e(
                    TAG,
                    "texture quality: the phone's memory size is unknown: the default level "
                            + memoryClassLevel);
        else
            Log.i(
                    TAG,
                    String.format(
                            Locale.ROOT,
                            "texture quality: none chosen; %.2f GiB of memory: level %d",
                            total / (1024.0 * 1024.0 * 1024.0),
                            memoryClassLevel));
        return memoryClassLevel;
    }

    static int validPirateFrequency(int value) {
        for (int step : PIRATE_FREQUENCIES) if (step == value) return value;
        Log.e(
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

    /** The renderer bench asked for with «Готово» (debug key bench: 1 full, 2 quick), until the engine takes it;
     *  never stored, so the row shows «выкл» again and the next game start runs none. */
    private static volatile int benchRequest = -1;

    /** The stored value; `fallback` for a key this build does not offer and for a debug override left to the preset.
     *  The bench request is handed out once. */
    static int get(Context context, String key, int fallback) {
        if (key.equals(AppNames.DEBUG_BENCH) && debugValues(context, key) != null) {
            int request = benchRequest;
            benchRequest = -1;
            return request < 0 ? fallback : request;
        }
        if (key.equals(AppNames.FACT_FX_PRESET_AUTO))
            return PresetSource.autoKey(source(context), prefs(context).getInt(MEASURED, 0));
        if (key.equals(AppNames.FACT_FX_AUTO_RENDER_HEIGHT)) return autoRenderHeight(context);
        if (debugValues(context, key) != null) {
            int value = valid(context, key, prefs(context).getInt(key, -1));
            return value < 0 ? fallback : value;
        }
        if (key.equals(AppNames.SETTING_FRAME_RATE))
            return valid(context, key, prefs(context).getInt(key, FrameRates.DEFAULT));
        if (key.equals(AppNames.SETTING_PIRATE_FREQUENCY))
            return valid(context, key, prefs(context).getInt(key, PIRATE_FREQUENCY_DEFAULT));
        if (key.equals(AppNames.SETTING_UI_SCALE)) return readUiScale(context);
        if (key.equals(AppNames.SETTING_DIAGNOSTICS_DETAILED))
            return detailedDiagnostics(context) ? 1 : 0;
        if (key.equals(AppNames.SETTING_HUD_TRANSPARENCY))
            return valid(
                    context, key, prefs(context).getInt(key, AppNames.DEFAULT_HUD_TRANSPARENCY));
        if (key.equals(AppNames.SETTING_LEFT_HANDED))
            return valid(context, key, prefs(context).getInt(key, AppNames.DEFAULT_LEFT_HANDED));
        if (key.equals(AppNames.SETTING_HAPTICS))
            return valid(context, key, prefs(context).getInt(key, AppNames.DEFAULT_HAPTICS));
        if (key.equals(AppNames.SETTING_TEXTURE_QUALITY))
            return valid(context, key, textureQuality(context));
        if (TextureProcessing.isKey(key))
            return valid(context, key, prefs(context).getInt(key, TextureProcessing.DEFAULT));
        return known(key) ? valid(context, key, prefs(context).getInt(key, 0)) : fallback;
    }

    /** Every preference as the JSON of the "appSettings" state channel (debug overrides as stored, -1 = preset). */
    static String json(Context context) {
        JSONObject result = new JSONObject();
        try {
            for (String key : AppNames.SETTINGS) result.put(key, get(context, key, 0));
            JSONArray rates = new JSONArray();
            for (int rate : FrameRates.offered(refreshRates(context))) rates.put(rate);
            result.put(AppNames.FACT_FRAME_RATE_OPTIONS, rates);
            JSONArray scales = new JSONArray();
            for (int scale : uiScaleOffered(context)) scales.put(scale);
            result.put(AppNames.FACT_UI_SCALE_OPTIONS, scales);
            result.put(AppNames.FACT_CACHE_BYTES, cacheBytes);
            result.put(
                    AppNames.FACT_CACHE_CLEAR_PENDING,
                    GameCache.clearMarker(context.getFilesDir()).exists());
            result.put(
                    AppNames.FACT_FX_PRESET_AUTO,
                    PresetSource.AUTO.equals(source(context))
                            && prefs(context).getInt(MEASURED, 0) > 0);
            result.put(AppNames.FACT_FX_AUTO_RENDER_HEIGHT, autoRenderHeight(context));
            if (DebugBuild.on(context))
                for (String key : AppNames.DEBUG_KEYS)
                    result.put(key, valid(context, key, prefs(context).getInt(key, -1)));
        } catch (JSONException impossible) {
            throw new IllegalStateException(impossible);
        }
        return result.toString();
    }

    /** Stores the values the settings screen sent; unknown keys and malformed JSON are errors. */
    static void apply(Context context, String json) throws JSONException {
        JSONObject values = new JSONObject(json);
        SharedPreferences.Editor edit = prefs(context).edit();
        int storedPreset = get(context, AppNames.SETTING_FX_PRESET, 0);
        String source = source(context);
        String after =
                PresetSource.afterSettings(
                        source,
                        storedPreset,
                        values.has(AppNames.SETTING_FX_PRESET)
                                ? valid(
                                        context,
                                        AppNames.SETTING_FX_PRESET,
                                        values.getInt(AppNames.SETTING_FX_PRESET))
                                : null);
        if (!after.equals(source)) {
            edit.putString(SOURCE, after)
                    .remove(HEIGHT); // the measured height belongs to the automatic choice
            Log.i(
                    TAG,
                    "graphics preset: the player chose "
                            + values.getInt(AppNames.SETTING_FX_PRESET)
                            + " (was "
                            + storedPreset
                            + ", "
                            + source
                            + "); no measurement replaces it");
        }
        Iterator<String> keys = values.keys();
        StringBuilder overrides = new StringBuilder();
        while (keys.hasNext()) {
            String key = keys.next();
            if (!known(key) && debugValues(context, key) == null)
                throw new JSONException("unknown graphics setting " + key);
            int value = valid(context, key, values.getInt(key));
            if (key.equals(AppNames.DEBUG_BENCH)) {
                benchRequest = value;
                continue;
            }
            if (key.equals(AppNames.SETTING_UI_SCALE)) {
                writeFile(context, UI_SCALE_FILE, key, value);
                continue;
            }
            edit.putInt(key, value);
            if (debugValues(context, key) != null && value >= 0)
                overrides.append(' ').append(key).append('=').append(value);
        }
        edit.apply();
        if (DebugBuild.on(context))
            Log.i(
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
            Log.w(
                    TAG,
                    "graphics preset: the measurement chose "
                            + preset
                            + " (scene height "
                            + renderHeight
                            + ", version "
                            + version
                            + ") but the player chose "
                            + get(context, AppNames.SETTING_FX_PRESET, 0)
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
                        .putInt(
                                AppNames.SETTING_FX_PRESET,
                                valid(context, AppNames.SETTING_FX_PRESET, preset))
                        .putString(SOURCE, PresetSource.AUTO)
                        .putInt(HEIGHT, renderHeight)
                        .putInt(MEASURED, version)
                        .putString(MEASUREMENT, report)
                        .commit();
        if (stored)
            Log.i(
                    TAG,
                    "graphics preset: measured "
                            + preset
                            + (renderHeight > 0 ? " at a " + renderHeight + "-line scene" : "")
                            + " (version "
                            + version
                            + ") stored; "
                            + report);
        else
            Log.e(
                    TAG,
                    "graphics preset: the measured preset "
                            + preset
                            + " could not be stored; the next start measures again");
        return stored;
    }

    private static int autoRenderHeight(Context context) {
        return PresetSource.autoRenderHeight(
                source(context),
                get(context, AppNames.SETTING_FX_PRESET, 0),
                prefs(context).getInt(HEIGHT, 0));
    }

    /** "texture_normal_maps on (chosen)" or "... off (default)". */
    private static String textureProcessingFact(Context context, String key) {
        return key
                + (get(context, key, 0) != 0 ? " on" : " off")
                + (prefs(context).contains(key) ? " (chosen)" : " (default)");
    }

    /** The graphics choice for the diagnostics export. */
    static void describe(Context context, DiagnosticsReport report) {
        report.fact("preset", get(context, AppNames.SETTING_FX_PRESET, 0))
                .fact("source", source(context))
                .fact("scene height", autoRenderHeight(context))
                .fact("measurement version", prefs(context).getInt(MEASURED, 0))
                .fact("measurement", prefs(context).getString(MEASUREMENT, "none"))
                .fact(
                        AppNames.SETTING_TEXTURE_QUALITY,
                        get(context, AppNames.SETTING_TEXTURE_QUALITY, TEXTURE_QUALITY_DEFAULT))
                .fact(
                        "texture processing",
                        textureProcessingFact(context, TextureProcessing.NORMAL_MAPS)
                                + ", "
                                + textureProcessingFact(context, TextureProcessing.HEIGHT_MAPS)
                                + ", "
                                + textureProcessingFact(context, TextureProcessing.UPSCALE))
                .fact(
                        AppNames.SETTING_FRAME_RATE,
                        get(context, AppNames.SETTING_FRAME_RATE, FrameRates.DEFAULT))
                .fact(
                        AppNames.SETTING_PIRATE_FREQUENCY,
                        get(context, AppNames.SETTING_PIRATE_FREQUENCY, PIRATE_FREQUENCY_DEFAULT));
    }
}
