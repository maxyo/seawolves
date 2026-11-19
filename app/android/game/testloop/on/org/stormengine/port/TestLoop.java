package org.stormengine.port;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import android.view.PixelCopy;
import android.view.SurfaceView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Scenario driver for automated runs on an emulator or a phone (debug builds made with build_apk.py --test-loop
 * only; every other build gets android/game/testloop/off). A runner starts the launcher with the Game Loop intent
 * com.google.intent.action.TEST_LOOP, the integer extra "scenario" and, optionally, a result file as the
 * intent's data: tools/android/emu_smoke.py does, as does
 * {@code adb shell am start -a com.google.intent.action.TEST_LOOP
 * -n <package>/org.stormengine.port.GameLauncherActivity --ei scenario <n>}.
 * Everything the run produces goes to {@link #results} (the app's external files "testloop": screenshots,
 * events.log, the game's log, the diagnostics report, summary-<scenario>.json); the summary JSON also goes to the
 * result file when the intent names one. Game scenarios need the game subset pushed to the app's OBB directory
 * (tools/android/game_subset.py makes it from the tester's own copy; android/game/TESTING.md).
 *
 * Scenarios:
 *  1 launcher: the launcher screen, its game files screen, the system folder picker it opens;
 *    no game data needed.
 *  2 menu scene: installs the pushed game subset (OBB), starts the game and measures the main menu's
 *    3D scene under each preset.
 *  3 save scene: as 2, and the title's «Продолжить» loads the newest save (the one pushed with the
 *    subset); the loaded scene is measured under each preset.
 *  4 world map: as 3 with the save on the world map (the map's sea and ships, often CPU-bound).
 *  5 upscale: the town save on Высокая with the texture quality «Ультра» (texture_quality=3) held HOLD_MS (relief and ×2
 *    made meanwhile), then Высокое (2) and Ультра (3) measured.
 *  6 character relief: the town save, Высокая with fx_character_relief=2 (normals and parallax, its default)
 *    held CHARACTER_HOLD_MS, then fx_character_relief 0 vs 2 at Высокая and 0 vs 1 (normals) at Средняя.
 *  7 far LOD: the sea save held FAR_LOD_HOLD_MS at Высокая until no far LOD is pending, then at Низкая and Высокая:
 *    far LOD off, the preset's far LOD, far LOD off with the 50 m ceiling cull (a build with those debug keys).
 *  8 preset steps: on the title, after the automatic preset, what separates «Низкая» from «Средняя»: nine variants
 *    65 s each, «Средняя» with one of its effects off at a time and «Низкая» with one of them on (presetSteps).
 *  9 boot smoke (tools/android/emu_smoke.py): as 3 without the measurements and
 *    the intro videos (the game starts at its main menu, as after the pause menu's exit): title, automatic preset,
 *    «Продолжить», the town's first frames (one screenshot, which must not be a single colour), done. The pushed subset
 *    is extracted once per set of OBBs (a stamp in the game root), so a repeated smoke on one device starts at once.
 *  12, 13, 14 / 22, 23, 24 / 32.. / 42..: scene 2, 3 or 4 with a renderer variant set before the game starts
 *    (VARIANTS: debug keys of a debuggable build; a build without them fails loudly): 1x render_thread=0,
 *    2x render_overlap=1, 3x render_backend=1 (OpenGL ES), 4x render_backend=2 (Vulkan), 5x frame_rate=40; 3x/4x measure Низкая and
 *    Высокая after a warm-up pass of each.
 * Game scenarios start with the graphics preferences of a first install and wait on the title for the engine's
 * automatic preset ("graphics auto preset: chose", its lines and toast screenshot in the result) before anything sets
 * a preset or leaves the title.
 * Per preset (Низкая, Средняя, Высокая, with fx_gpu_profile=1) the scene renders PRESET_MS: the
 * frame pacing window is 30 s, so each preset holds at least one whole window. Scenarios 2 and 3 first hold Высокая
 * until the relief generation it starts is idle (BACKGROUND_HOLD_MS at most): the first preset that turns the relief
 * on would otherwise be measured together with the background generation's CPU and GPU work. Every step has a
 * hard limit; a missed one ends the run with status "failed" and its reason (never a silent pass).
 */
final class TestLoop {
    static final String ACTION = "com.google.intent.action.TEST_LOOP";
    private static final String TAG = "StormTestLoop";
    private static final long PRESET_MS = 65_000, SETTLE_MS = 8_000;
    private static final long MENU_LIMIT_MS = 240_000,
            SCENE_LIMIT_MS = 240_000,
            RUN_LIMIT_MS = 840_000;
    private static final long AUTO_LIMIT_MS = 240_000;

    /** The saves of the pushed subset (tools/android/game_subset.py --save): the town scene, the world map and the sea. */
    private static final String SCENE_SAVE = "Сен-Пьер улицы",
            MAP_SAVE = "Глобальная карта",
            SEA_SAVE = "Мартиника - море";

    private static final int SHOT_ATTEMPTS = 4;
    private static final long SHOT_RETRY_MS = 3_000;
    private static final String[] PRESETS = {"Низкая", "Средняя", "Высокая"};

    /** Renderer variants by the scenario's tens digit (debug keys of GraphicsSettings; every scenario starts from
     *  cleared graphics preferences, so 0 is the build's default). 3 and 4 choose the render backend, 5 the
     *  game's frame rate 40 (the display mode check; GraphicsSettings keeps it to the rates the display offers,
     *  and the summary's "frame pacing:" line shows the rate it took). */
    private static final String[] VARIANTS = {
        "{}",
        "{\"render_thread\":0}",
        "{\"render_overlap\":1}",
        "{\"render_backend\":1}",
        "{\"render_backend\":2}",
        "{\"frame_rate\":40}"
    };

    /** A backend switch clears the other backend's program cache: the backend variants measure Низкая and Высокая
     *  after a warm-up pass of each (its frames are reported as warm-up, not as the preset's numbers). */
    private static final long WARMUP_MS = 30_000;

    /** Scenario 5 holds the town scene on Высокая with the ×2 upscale on this long first: the relief and ×2 textures are
     *  made in the background meanwhile (their logcat timings are the measurement), then texture_quality 2 vs 3. */
    private static final long HOLD_MS = 240_000;

    /** Scenario 6 (the characters' relief) holds the town on Высокая with it on before its 0 vs 1 at Высокая and Средняя. */
    private static final long CHARACTER_HOLD_MS = 120_000;

    /** Scenario 7 holds the sea on Высокая this long, then waits up to FAR_LOD_PENDING_LIMIT_MS for far_lod_pending=0. */
    private static final long FAR_LOD_HOLD_MS = 90_000, FAR_LOD_PENDING_LIMIT_MS = 180_000;

    /** Scenarios 2 and 3 hold Высокая before the measured presets until the engine logs its relief generation idle
     *  ("relief generation (idle)"), at least BACKGROUND_MIN_MS, at most BACKGROUND_HOLD_MS (then the result says so). */
    private static final long BACKGROUND_MIN_MS = 20_000, BACKGROUND_HOLD_MS = 180_000;

    private static final String RELIEF_IDLE = "relief generation (idle)";

    private TestLoop() {}

    /** Scenario 9: the game starts at its main menu (GameActivity.EXTRA_MAIN_MENU), without the intro videos. */
    private static volatile boolean mainMenuStart;

    static boolean startAtMainMenu() {
        return mainMenuStart;
    }

    private static volatile String screen = "?";
    private static volatile String mode = "unknown";
    private static volatile long screenAt, modeAt;

    static void screen(String name) {
        screen = name;
        screenAt = SystemClock.elapsedRealtime();
        Log.i(TAG, "screen '" + name + "'");
    }

    static void gameplay(String next) {
        mode = next;
        modeAt = SystemClock.elapsedRealtime();
    }

    static File results(Context context) {
        File directory = context.getExternalFilesDir("testloop");
        if (directory == null)
            throw new IllegalStateException("No external app storage for the test loop results");
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IllegalStateException("Cannot create " + directory);
        return directory;
    }

    private static File scenarioFile(Context context) {
        return new File(context.getFilesDir(), "testloop-scenario.json");
    }

    private static File doneFile(Context context) {
        return new File(results(context), "game-done.json");
    }

    private static void note(Context context, String text) {
        Log.i(TAG, text);
        try (OutputStream out =
                new FileOutputStream(new File(results(context), "events.log"), true)) {
            out.write(
                    (SystemClock.elapsedRealtime()
                                    + " "
                                    + android.os.Process.myPid()
                                    + " "
                                    + text
                                    + "\n")
                            .getBytes(StandardCharsets.UTF_8));
        } catch (IOException error) {
            Log.e(TAG, "Cannot write events.log", error);
        }
    }

    // ---- launcher process -------------------------------------------------------------------

    static void launcher(GameLauncherActivity launcher, Intent intent) {
        if (intent == null || !ACTION.equals(intent.getAction())) return;
        if (launcher == endingLauncher || launcher.isFinishing()) {
            // The next scenario reached the launcher that is ending the previous one: a new
            // launcher runs it.
            Log.i(
                    TAG,
                    "scenario "
                            + intent.getIntExtra("scenario", 0)
                            + " arrived at an ending launcher: starting a new one for it");
            launcher.startActivity(
                    new Intent(intent)
                            .setClass(launcher, GameLauncherActivity.class)
                            .addFlags(
                                    Intent.FLAG_ACTIVITY_NEW_TASK
                                            | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                                            | Intent.FLAG_GRANT_READ_URI_PERMISSION
                                            | Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
            intent.setAction(null);
            return;
        }
        final int scenario = intent.getIntExtra("scenario", 0);
        final Uri resultFile = intent.getData();
        intent.setAction(null); // a recreated launcher must not start the loop again
        final Context context = launcher.getApplicationContext();
        final JSONObject summary = new JSONObject();
        final long started = SystemClock.elapsedRealtime();
        note(
                context,
                "scenario "
                        + scenario
                        + " started, results "
                        + results(context)
                        + ", result file "
                        + resultFile);
        HandlerThread thread = new HandlerThread("StormTestLoop");
        thread.start();
        new Handler(thread.getLooper())
                .post(
                        () -> {
                            put(summary, "scenario", scenario);
                            put(
                                    summary,
                                    "device",
                                    android.os.Build.MANUFACTURER
                                            + " "
                                            + android.os.Build.MODEL
                                            + " ("
                                            + android.os.Build.DEVICE
                                            + ")");
                            put(summary, "sdk", android.os.Build.VERSION.SDK_INT);
                            try {
                                if (scenario == 1) launcherScenario(launcher, summary);
                                else if ((scenario % 10 >= 2
                                                && scenario % 10 <= 8
                                                && scenario / 10 < VARIANTS.length)
                                        || scenario == 9) {
                                    if (gameScenario(
                                            launcher, scenario, summary, resultFile, started))
                                        return;
                                } else
                                    throw new IllegalStateException(
                                            "unknown scenario "
                                                    + scenario
                                                    + " (the header of TestLoop.java lists them)");
                                finish(launcher, summary, resultFile, started, "passed", "");
                            } catch (Throwable failure) {
                                Log.e(TAG, "scenario " + scenario + " failed", failure);
                                finish(
                                        launcher,
                                        summary,
                                        resultFile,
                                        started,
                                        "failed",
                                        String.valueOf(failure.getMessage()));
                            }
                        });
    }

    /** The loop's result: the summary into the results and the intent's result file, and the app's diagnostics. */
    private static void report(
            Context context,
            JSONObject summary,
            Uri resultFile,
            long started,
            String status,
            String reason) {
        int scenario = summary.optInt("scenario");
        put(summary, "status", status);
        put(summary, "reason", reason);
        put(summary, "seconds", (SystemClock.elapsedRealtime() - started) / 1000.0);
        diagnostics(context);
        note(
                context,
                "scenario " + scenario + " " + status + (reason.isEmpty() ? "" : ": " + reason));
        byte[] json = summary.toString().getBytes(StandardCharsets.UTF_8);
        write(new File(results(context), "summary-" + scenario + ".json"), json);
        if (resultFile != null) {
            try (OutputStream out = context.getContentResolver().openOutputStream(resultFile)) {
                out.write(json);
            } catch (IOException | RuntimeException error) {
                Log.e(TAG, "Cannot write the result file " + resultFile, error);
            }
        }
    }

    /** The launcher ending its loop; the runner may send the next scenario's intent to it meanwhile. */
    private static volatile GameLauncherActivity endingLauncher;

    /**
     * The launcher ends the loop by finishing, as the Game Loop convention expects: a runner that started it from
     * an Activity of its own (without FLAG_ACTIVITY_NEW_TASK) waits for this Activity to finish before it starts the
     * next scenario, and takes a launcher process that dies for a failed start. Only the launcher finishes, never
     * its task, which may be the runner's. A game process still there afterwards (the game finished its Activity
     * after its report, so nothing of it is on top) is ended. The launcher's own process is not killed: the next
     * scenario may already have been sent, and runs in a new launcher of this process.
     */
    private static void endLauncher(GameLauncherActivity launcher) {
        endingLauncher = launcher;
        launcher.runOnUiThread(launcher::finish);
        SystemClock.sleep(1_500);
        android.app.ActivityManager manager =
                launcher.getSystemService(android.app.ActivityManager.class);
        if (manager != null && manager.getRunningAppProcesses() != null)
            for (android.app.ActivityManager.RunningAppProcessInfo process :
                    manager.getRunningAppProcesses())
                if (process.pid != android.os.Process.myPid()
                        && process.uid == android.os.Process.myUid()) {
                    Log.i(TAG, "ending " + process.processName + " (" + process.pid + ")");
                    android.os.Process.killProcess(process.pid);
                }
        // This process stays: the next scenario's launcher may already run in it.
        Log.i(TAG, "test loop finished: launcher finished");
    }

    private static void finish(
            GameLauncherActivity launcher,
            JSONObject summary,
            Uri resultFile,
            long started,
            String status,
            String reason) {
        report(launcher.getApplicationContext(), summary, resultFile, started, status, reason);
        endLauncher(launcher);
    }

    private static void launcherScenario(GameLauncherActivity launcher, JSONObject summary)
            throws Exception {
        waitFor("launcher resumed", 20_000, () -> launcher.hasWindowFocus());
        SystemClock.sleep(3_000); // the portable UI draws its first frames
        shot(launcher, launcher.ui, "01-launcher");
        onUi(launcher, () -> launcher.onAction("files", ""));
        SystemClock.sleep(2_500);
        shot(launcher, launcher.ui, "02-files");
        onUi(launcher, () -> launcher.onAction("folder", ""));
        waitFor("system folder picker over the launcher", 15_000, () -> !launcher.hasWindowFocus());
        note(launcher, "system folder picker opened (import from a folder is reachable)");
        SystemClock.sleep(3_000); // long enough to be seen in a screen recording of the run
        // The picker is in the launcher's task, which may be the runner's (endLauncher): closed, so
        // that the
        // runner is on top again when the launcher finishes.
        onUi(launcher, () -> launcher.finishActivity(GameLauncherActivity.IMPORT_FOLDER));
        waitFor(
                "launcher on top again after closing the folder picker",
                15_000,
                () -> launcher.hasWindowFocus());
        put(summary, "launcher", "files screen and folder picker reached");
    }

    /**
     * Installs the subset and starts the game, which finishes the loop itself: while the game is in front
     * Android freezes this background process (cached app freezer), so nothing here may be needed to end
     * the run. The game process reports and finishes its Activity; the launcher, on top again, ends the
     * loop. It reports itself only when the game process ended without a result or the run limit
     * passed. True when the game process reported.
     */
    private static boolean gameScenario(
            GameLauncherActivity launcher,
            int scenario,
            JSONObject summary,
            Uri resultFile,
            long started)
            throws Exception {
        installPushedGame(launcher, summary);
        // Each scenario starts as a first install for the graphics choice: no stored preset, no
        // measurement, no
        // player's choice (the previous scenario's preset steps made one), so the automatic preset
        // measures again.
        if (!launcher.getSharedPreferences("graphics", Context.MODE_PRIVATE)
                .edit()
                .clear()
                .commit()) throw new IOException("Cannot clear the graphics preferences");
        note(launcher, "graphics preferences cleared (first-install state)");
        mainMenuStart = scenario == 9;
        if (scenario % 10 >= 3 && scenario % 10 != 8) {
            // «Продолжить» loads the newest record: this scenario's own save is made the newest.
            File save =
                    new File(
                            launcher.getFilesDir(),
                            "storm/SaveData/player/"
                                    + (scenario % 10 == 4
                                            ? MAP_SAVE
                                            : scenario % 10 == 7 ? SEA_SAVE : SCENE_SAVE));
            if (!save.isFile() || !save.setLastModified(System.currentTimeMillis()))
                throw new IOException(
                        "the scenario's save "
                                + save
                                + " is missing from the pushed subset or cannot be touched");
            note(launcher, "newest save: " + save.getName());
        }
        // The renderer variant is read when the engine starts: stored before Play. A build without
        // the variant's key
        // fails here loudly (GraphicsSettings: unknown graphics setting).
        String variant = VARIANTS[scenario / 10];
        if (scenario / 10 > 0) GraphicsSettings.apply(launcher, variant);
        put(summary, "variant", variant);
        note(launcher, "renderer variant " + variant);
        File done = doneFile(launcher);
        if (done.exists() && !done.delete()) throw new IOException("Cannot delete " + done);
        JSONObject plan =
                new JSONObject()
                        .put("scenario", scenario)
                        .put("started", started)
                        .put("summary", summary)
                        .put("result", resultFile == null ? "" : resultFile.toString());
        write(scenarioFile(launcher), plan.toString().getBytes(StandardCharsets.UTF_8));
        waitFor("launcher resumed", 20_000, () -> launcher.hasWindowFocus());
        SystemClock.sleep(2_000);
        JSONArray launcherShot =
                new JSONArray(); // retried: a slow device may not have drawn the launcher yet
        shot(launcher, launcher.ui, "01-launcher", launcherShot);
        // The boot smoke judges the game's frame, not the launcher's: its failed screenshot is
        // reported, not fatal.
        if (launcherShot.length() > 0 && scenario == 9)
            put(summary, "launcherScreenshotFailure", launcherShot);
        else if (launcherShot.length() > 0)
            throw new IllegalStateException("screenshot failed: " + launcherShot);
        onUi(launcher, () -> launcher.onAction("play", ""));
        long played = SystemClock.elapsedRealtime();
        note(launcher, "Play pressed");
        while (!done.isFile()) {
            if (SystemClock.elapsedRealtime() - played > RUN_LIMIT_MS)
                throw new IllegalStateException(
                        "the game process gave no result within " + RUN_LIMIT_MS / 1000 + " s");
            if (SystemClock.elapsedRealtime() - played > 30_000
                    && !gameProcessAlive(launcher)
                    && !done.isFile())
                throw new IllegalStateException(
                        "the game process ended without a result (see logcat: StormExit, crash)");
            SystemClock.sleep(1_000);
        }
        // The game process finishes its Activity after its report: the launcher, back on top, ends
        // the loop.
        long reported = SystemClock.elapsedRealtime();
        while (gameProcessAlive(launcher) && SystemClock.elapsedRealtime() - reported < 15_000)
            SystemClock.sleep(250);
        note(
                launcher,
                "the game process reported"
                        + (gameProcessAlive(launcher)
                                ? " (still running after 15 s)"
                                : " and ended")
                        + "; the launcher ends the loop");
        endLauncher(launcher);
        return true;
    }

    private static boolean gameProcessAlive(Context context) {
        android.app.ActivityManager manager =
                context.getSystemService(android.app.ActivityManager.class);
        if (manager == null || manager.getRunningAppProcesses() == null) return false;
        for (android.app.ActivityManager.RunningAppProcessInfo process :
                manager.getRunningAppProcesses())
            if (process.processName.endsWith(":game")) return true;
        return false;
    }

    /**
     * Game subset pushed as OBBs (ZIPs: game/ = the game root, storm/ = saves; at most two,
     * main.<vc>.<package>.obb and patch.<vc>.<package>.obb): extracted into the app's own storage, main first.
     */
    private static void installPushedGame(Context context, JSONObject summary) throws IOException {
        File[] obbs =
                context.getObbDir() == null
                        ? null
                        : context.getObbDir().listFiles((dir, name) -> name.endsWith(".obb"));
        if (obbs == null || obbs.length == 0 || obbs.length > 2)
            throw new IOException(
                    "expected one or two pushed game OBBs in "
                            + context.getObbDir()
                            + ", found "
                            + (obbs == null ? 0 : obbs.length));
        java.util.Arrays.sort(
                obbs,
                (a, b) ->
                        Boolean.compare(
                                !a.getName().startsWith("main."),
                                !b.getName().startsWith("main.")));
        if (!obbs[0].getName().startsWith("main."))
            throw new IOException("no main.*.obb among the pushed OBBs");
        File game = GameStorage.imported(context), storm = new File(context.getFilesDir(), "storm");
        // The same OBBs extracted before on this device (a local emulator): nothing to do.
        StringBuilder set = new StringBuilder();
        for (File obb : obbs)
            set.append(obb.getName())
                    .append(' ')
                    .append(obb.length())
                    .append(' ')
                    .append(obb.lastModified())
                    .append('\n');
        File stamp = new File(game, ".testloop-obbs");
        if (stamp.isFile()
                && new String(read(stamp), StandardCharsets.UTF_8).equals(set.toString())) {
            GameStorage.validate(game);
            note(context, "the pushed game subset is installed already (" + stamp + ")");
            put(summary, "installMs", 0);
            return;
        }
        if (stamp.exists() && !stamp.delete()) throw new IOException("Cannot delete " + stamp);
        long start = SystemClock.elapsedRealtime(), bytes = 0;
        int files = 0;
        for (File obb : obbs) {
            try (ZipInputStream zip =
                    new ZipInputStream(
                            new java.io.BufferedInputStream(new FileInputStream(obb), 1 << 20))) {
                byte[] buffer = new byte[1 << 20];
                for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                    String name = entry.getName();
                    if (name.contains("..")) throw new IOException("unsafe entry " + name);
                    File target =
                            name.startsWith("game/")
                                    ? new File(game, name.substring(5))
                                    : name.startsWith("storm/")
                                            ? new File(storm, name.substring(6))
                                            : null;
                    if (target == null)
                        throw new IOException("entry outside game/ and storm/: " + name);
                    if (entry.isDirectory()) {
                        if (!target.isDirectory() && !target.mkdirs())
                            throw new IOException("Cannot create " + target);
                        continue;
                    }
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs())
                        throw new IOException("Cannot create " + parent);
                    try (OutputStream out = new FileOutputStream(target)) {
                        for (int n; (n = zip.read(buffer)) != -1; ) {
                            out.write(buffer, 0, n);
                            bytes += n;
                        }
                    }
                    ++files;
                }
            }
        }
        GameStorage.validate(game);
        write(stamp, set.toString().getBytes(StandardCharsets.UTF_8));
        long ms = SystemClock.elapsedRealtime() - start;
        StringBuilder names = new StringBuilder();
        for (File obb : obbs) names.append(' ').append(obb.getName());
        note(
                context,
                "installed the pushed game subset"
                        + names
                        + ": "
                        + files
                        + " files, "
                        + bytes
                        + " bytes in "
                        + ms
                        + " ms");
        put(summary, "installMs", ms);
        put(summary, "installBytes", bytes);
    }

    /** The app's own diagnostics report (as «Поделиться отчётом» writes it), copied into the results. */
    private static void diagnostics(Context context) {
        try {
            JSONObject info;
            try (InputStream in = context.getAssets().open("build-info.json")) {
                info = new JSONObject(new String(readAll(in), StandardCharsets.UTF_8));
            }
            File report = DiagnosticsExport.write(context, info);
            write(new File(results(context), "diagnostics.txt"), read(report));
        } catch (IOException | JSONException error) {
            Log.e(TAG, "Diagnostics report failed", error);
        }
    }

    // ---- game process -----------------------------------------------------------------------

    static void game(GameActivity game, EngineUi ui, SurfaceView surface) {
        File planFile = scenarioFile(game);
        if (!planFile.isFile()) return;
        final JSONObject plan;
        try {
            plan = new JSONObject(new String(read(planFile), StandardCharsets.UTF_8));
        } catch (IOException | JSONException error) {
            throw new IllegalStateException("Unreadable test loop scenario " + planFile, error);
        }
        if (!planFile.delete()) Log.e(TAG, "Cannot delete " + planFile);
        final int scenario = plan.optInt("scenario");
        final JSONObject summary =
                plan.optJSONObject("summary") == null
                        ? new JSONObject()
                        : plan.optJSONObject("summary");
        final Uri resultFile =
                plan.optString("result").isEmpty() ? null : Uri.parse(plan.optString("result"));
        final long started = plan.optLong("started", SystemClock.elapsedRealtime());
        new Thread(
                        () -> {
                            JSONObject result = new JSONObject();
                            JSONArray problems =
                                    new JSONArray(); // screenshots that failed: loud, but the
                            // timings stay
                            String status = "passed", reason = "";
                            long start = SystemClock.elapsedRealtime();
                            try {
                                note(game, "game scenario " + scenario + " in the game process");
                                waitFor("the engine thread", 20_000, GameActivity::isEngineAlive);
                                watchEngine = true;
                                waitFor(
                                        "the title screen (interface mainmenu)",
                                        MENU_LIMIT_MS,
                                        () -> screen.equals("mainmenu"));
                                put(
                                        result,
                                        "mainMenuSeconds",
                                        (SystemClock.elapsedRealtime() - start) / 1000.0);
                                note(game, "title screen up");
                                // A first start measures the title scene and picks the preset
                                // (engine auto_preset.hpp): nothing may
                                // set a preset or leave the title before it has chosen, or the pick
                                // never happens.
                                // The measurement's outcome: it chose, or it ended without a choice
                                // (aborted, off, already measured).
                                // An outcome without a choice is recorded and shown in the summary;
                                // the scenario's own steps go on.
                                waitFor(
                                        "the automatic graphics preset's outcome"
                                            + " (AutoPresetOutcome: chose / aborted / not measured"
                                            + " / off / already measured)",
                                        AUTO_LIMIT_MS,
                                        () ->
                                                !autoPresetLines(game, AutoPresetOutcome.NEEDLES)
                                                        .isEmpty());
                                put(
                                        result,
                                        "autoPresetSeconds",
                                        (SystemClock.elapsedRealtime() - start) / 1000.0);
                                java.util.List<String> outcome =
                                        autoPresetLines(game, AutoPresetOutcome.NEEDLES);
                                if (autoPresetLines(game, AutoPresetOutcome.CHOSE).isEmpty()) {
                                    put(result, "autoPresetProblem", outcome.get(0));
                                    Log.e(
                                            TAG,
                                            "the automatic preset did not choose: "
                                                    + outcome.get(0));
                                }
                                note(game, "automatic preset outcome: " + outcome.get(0));
                                SystemClock.sleep(1_500); // the toast that tells the player
                                shot(game, surface, "11-auto-preset-toast", problems);
                                put(
                                        result,
                                        "autoPreset",
                                        new JSONArray(
                                                autoPresetLines(
                                                        game,
                                                        "graphics auto preset:",
                                                        "graphics preset:")));
                                if (scenario % 10 >= 3 && scenario % 10 != 8) {
                                    // The title's «Продолжить» (engine UI state channel
                                    // titleIntent), now that the preset is chosen.
                                    onUi(
                                            game,
                                            () ->
                                                    ui.setState(
                                                            "titleIntent",
                                                            "{\"intent\":\"Continue\"}"
                                                                    .getBytes(
                                                                            StandardCharsets
                                                                                    .UTF_8)));
                                    final String wanted =
                                            scenario % 10 == 4
                                                    ? "worldmap"
                                                    : scenario % 10 == 7
                                                            ? "sailing"
                                                            : null; // 3, 5, 6: the town
                                    waitFor(
                                            wanted == null
                                                    ? "the saved scene (gameplay without an"
                                                            + " interface)"
                                                    : "gameplay mode " + wanted,
                                            SCENE_LIMIT_MS,
                                            () ->
                                                    !mode.equals("unknown")
                                                            && screen.isEmpty()
                                                            && (wanted == null
                                                                    || mode.equals(wanted)));
                                    put(
                                            result,
                                            "sceneSeconds",
                                            (SystemClock.elapsedRealtime() - start) / 1000.0);
                                    put(result, "mode", mode);
                                    note(game, "saved scene up, mode " + mode);
                                }
                                SystemClock.sleep(SETTLE_MS);
                                shot(game, surface, "10-scene", problems);
                                if (scenario == 9) {
                                    String blank =
                                            singleColour(new File(results(game), "10-scene.png"));
                                    if (!blank.isEmpty())
                                        throw new IllegalStateException(
                                                "the saved scene's frame is " + blank);
                                    note(game, "boot smoke: the saved scene draws");
                                }
                                JSONArray presets = new JSONArray();
                                result.put("presets", presets);
                                final boolean backendVariant = scenario / 10 >= 3;
                                // Steps: {preset, kind (0 measured, 1 warm-up, 2 hold), the step's
                                // own debug keys (JSON members, "" none)}.
                                // 5: texture_quality (Ультра = the ×2 textures); 6:
                                // fx_character_relief; 7: the far LODs (fx_far_lod: 0 off, -1 the
                                // preset's)
                                // and the far-LOD ceiling cull (fx_farlod_cull m), after a hold
                                // that lets background generation finish.
                                final long holdMs =
                                        scenario % 10 == 6
                                                ? CHARACTER_HOLD_MS
                                                : scenario % 10 == 7 ? FAR_LOD_HOLD_MS : HOLD_MS;
                                final String lodOff = "\"fx_far_lod\":0,\"fx_farlod_cull\":0",
                                        lodPreset = "\"fx_far_lod\":-1,\"fx_farlod_cull\":0",
                                        lodCull = "\"fx_far_lod\":0,\"fx_farlod_cull\":50";
                                final Object[][] steps =
                                        scenario % 10 == 5
                                                ? new Object[][] {
                                                    {2, 2, "\"texture_quality\":3"},
                                                    {2, 0, "\"texture_quality\":2"},
                                                    {2, 0, "\"texture_quality\":3"}
                                                }
                                                : scenario % 10 == 6
                                                        ? new Object[][] {
                                                            {2, 2, "\"fx_character_relief\":2"},
                                                            {2, 0, "\"fx_character_relief\":0"},
                                                            {2, 0, "\"fx_character_relief\":2"},
                                                            {1, 0, "\"fx_character_relief\":0"},
                                                            {1, 0, "\"fx_character_relief\":1"}
                                                        }
                                                        : scenario % 10 == 7
                                                                ? new Object[][] {
                                                                    {2, 2, lodPreset},
                                                                    {0, 0, lodOff},
                                                                    {0, 0, lodPreset},
                                                                    {0, 0, lodCull},
                                                                    {2, 0, lodOff},
                                                                    {2, 0, lodPreset},
                                                                    {2, 0, lodCull}
                                                                }
                                                                : scenario == 9
                                                                        ? new Object[0][]
                                                                        : scenario % 10 == 8
                                                                                ? presetSteps()
                                                                                : backendVariant
                                                                                        ? new Object
                                                                                                []
                                                                                                [] {
                                                                                            {
                                                                                                0,
                                                                                                1,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                2,
                                                                                                1,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                0,
                                                                                                0,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                2,
                                                                                                0,
                                                                                                ""
                                                                                            }
                                                                                        }
                                                                                        : new Object
                                                                                                []
                                                                                                [] {
                                                                                            {
                                                                                                2,
                                                                                                3,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                0,
                                                                                                0,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                1,
                                                                                                0,
                                                                                                ""
                                                                                            },
                                                                                            {
                                                                                                2,
                                                                                                0,
                                                                                                ""
                                                                                            }
                                                                                        };
                                int stepIndex = 0;
                                for (Object[] step : steps) {
                                    final int preset = (Integer) step[0], kind = (Integer) step[1];
                                    final String keys = (String) step[2];
                                    // kind 3: a hold that ends when the background relief
                                    // generation is idle (BACKGROUND_HOLD_MS at most).
                                    final boolean warmup = kind == 1,
                                            background = kind == 3,
                                            hold = kind == 2 || background;
                                    final long stepMs =
                                            background
                                                    ? BACKGROUND_MIN_MS
                                                    : hold
                                                            ? holdMs
                                                            : warmup ? WARMUP_MS : PRESET_MS;
                                    // The step's name lists what it sets (a key at -1 is the
                                    // preset's value: left out of the name).
                                    final String shown =
                                            keys.replaceAll("\"[a-z_]+\":-1,?", "")
                                                    .replaceAll(",$", "")
                                                    .replace("\"", "")
                                                    .replace("fx_", "");
                                    final String name =
                                            PRESETS[preset] + (shown.isEmpty() ? "" : " " + shown);
                                    final String label =
                                            String.format(
                                                            java.util.Locale.ROOT,
                                                            "2%d-step-%02d-preset-%d",
                                                            preset,
                                                            stepIndex++,
                                                            preset)
                                                    + (warmup ? "-warmup" : hold ? "-hold" : "");
                                    if (scenario % 10 >= 3
                                            && scenario % 10 != 8
                                            && (mode.equals("unknown") || !screen.isEmpty()))
                                        throw new IllegalStateException(
                                                "the scene was left before "
                                                        + name
                                                        + " (screen '"
                                                        + screen
                                                        + "', mode "
                                                        + mode
                                                        + ")");
                                    final String json =
                                            "{\"fx_preset\":"
                                                    + preset
                                                    + ",\"fx_gpu_profile\":1"
                                                    + (keys.isEmpty() ? "" : "," + keys)
                                                    + "}";
                                    onUi(
                                            game,
                                            () -> {
                                                try {
                                                    GraphicsSettings.apply(game, json);
                                                } catch (JSONException impossible) {
                                                    throw new IllegalStateException(impossible);
                                                }
                                                game.queueMobileAction("graphics:reload");
                                            });
                                    long from = System.currentTimeMillis(),
                                            applied = SystemClock.elapsedRealtime();
                                    note(
                                            game,
                                            name
                                                    + " applied"
                                                    + (warmup
                                                            ? " (warm-up)"
                                                            : background
                                                                    ? " (hold until the background"
                                                                          + " relief generation is"
                                                                          + " idle)"
                                                                    : hold
                                                                            ? " (hold "
                                                                                    + holdMs / 1000
                                                                                    + " s: the"
                                                                                    + " background"
                                                                                    + " texture"
                                                                                    + " generation)"
                                                                            : ""));
                                    SystemClock.sleep(stepMs / 2);
                                    shot(game, surface, label, problems);
                                    // The screenshot's retries must not shorten the step's measured
                                    // time.
                                    long left = stepMs - (SystemClock.elapsedRealtime() - applied);
                                    if (left > 0) SystemClock.sleep(left);
                                    if (background) {
                                        long until =
                                                SystemClock.elapsedRealtime()
                                                        + BACKGROUND_HOLD_MS
                                                        - BACKGROUND_MIN_MS;
                                        while (latestEpochMs(game, RELIEF_IDLE) < from
                                                && SystemClock.elapsedRealtime() < until)
                                            SystemClock.sleep(5_000);
                                        boolean idle = latestEpochMs(game, RELIEF_IDLE) >= from;
                                        put(
                                                result,
                                                "backgroundIdleSeconds",
                                                idle
                                                        ? (System.currentTimeMillis() - from)
                                                                / 1000.0
                                                        : -1);
                                        // Busy after the hold (a slow GPU): said in the result and
                                        // the events, not a failure.
                                        note(
                                                game,
                                                idle
                                                        ? "background relief generation idle: the"
                                                                + " measured presets start"
                                                        : "background relief generation still busy"
                                                                + " after "
                                                                + BACKGROUND_HOLD_MS / 1000
                                                                + " s on Высокая: the presets are"
                                                                + " measured with it");
                                    }
                                    if (hold && scenario % 10 == 7) {
                                        // The far LODs are made in the background on the first
                                        // load: the measured steps start when the
                                        // profile reports none pending (bounded; what is still
                                        // pending then goes into the result).
                                        long until =
                                                SystemClock.elapsedRealtime()
                                                        + FAR_LOD_PENDING_LIMIT_MS;
                                        while (farLodPending(game) != 0
                                                && SystemClock.elapsedRealtime() < until)
                                            SystemClock.sleep(5_000);
                                        int pending = farLodPending(game);
                                        put(result, "farLodPendingAtStart", pending);
                                        if (pending != 0)
                                            problems.put(
                                                    "far LODs still pending when the measurement"
                                                            + " started: "
                                                            + pending);
                                        note(
                                                game,
                                                "far LODs pending at the measurement's start: "
                                                        + pending);
                                    }
                                    presets.put(
                                            new JSONObject()
                                                    .put("preset", preset)
                                                    .put("name", name)
                                                    .put("warmup", warmup || hold)
                                                    .put(
                                                            "kind",
                                                            hold
                                                                    ? "hold"
                                                                    : warmup
                                                                            ? "warmup"
                                                                            : "measured")
                                                    .put("fromMs", from)
                                                    .put("toMs", System.currentTimeMillis()));
                                }
                            } catch (Throwable failure) {
                                status = "failed";
                                reason = String.valueOf(failure.getMessage());
                                Log.e(TAG, "game scenario " + scenario + " failed", failure);
                                shot(game, surface, "99-failure", problems);
                            }
                            if (problems.length() > 0) {
                                put(result, "screenshotFailures", problems);
                                if (status.equals("passed")) {
                                    status = "failed";
                                    reason = "screenshots failed: " + problems;
                                } else reason += "; screenshots failed: " + problems;
                            }
                            put(result, "status", status);
                            put(result, "reason", reason);
                            put(result, "screen", screen);
                            put(result, "lastMode", mode);
                            ownLog(game, new File(results(game), "game-logcat.txt"));
                            put(summary, "game", result);
                            note(game, "game scenario " + scenario + " " + status);
                            report(
                                    game,
                                    summary,
                                    resultFile,
                                    started,
                                    status,
                                    reason.isEmpty() ? "" : "game: " + reason);
                            // Last: the launcher ends the loop (and this process) as soon as it
                            // sees this file.
                            write(
                                    doneFile(game),
                                    result.toString().getBytes(StandardCharsets.UTF_8));
                            // The game Activity ends (its process with it): the launcher below
                            // comes back on top, no longer
                            // frozen, sees the done marker and ends the loop.
                            game.runOnUiThread(game::finish);
                        },
                        "StormTestLoopGame")
                .start();
    }

    /** Scenario 8: «Низкая» (0) and «Средняя» (1) on the title with single effects changed. Each variant sets
     *  every key, -1 = the preset's value, so nothing carries over from the variant before. */
    private static Object[][] presetSteps() {
        final String[] keys = {
            "fx_msaa",
            "fx_sea_waves",
            "fx_shadows",
            "fx_light_per_pixel",
            "fx_alpha_to_coverage",
            "fx_render_height"
        };
        final Object[][] variants = {
            {0, ""},
            {1, ""},
            {1, "fx_msaa=1"},
            {1, "fx_sea_waves=0"},
            {1, "fx_shadows=0"},
            {1, "fx_light_per_pixel=0"},
            {1, "fx_alpha_to_coverage=0"},
            {0, "fx_msaa=4"},
            {0, "fx_render_height=720"}
        };
        Object[][] steps = new Object[variants.length][];
        for (int i = 0; i < variants.length; ++i) {
            StringBuilder json = new StringBuilder();
            String set = (String) variants[i][1];
            for (String key : keys) {
                String value = set.startsWith(key + "=") ? set.substring(key.length() + 1) : "-1";
                json.append(json.length() == 0 ? "" : ",")
                        .append('"')
                        .append(key)
                        .append("\":")
                        .append(value);
            }
            steps[i] = new Object[] {variants[i][0], 0, json.toString()};
        }
        return steps;
    }

    /** The wall-clock time (epoch ms) of the last line of this process's log containing `needle`; 0 when none. */
    private static long latestEpochMs(Context context, String needle) {
        java.util.List<String> lines = autoPresetLines(context, needle);
        if (lines.isEmpty()) return 0;
        try {
            return (long)
                    (Double.parseDouble(lines.get(lines.size() - 1).split("\\s+", 2)[0]) * 1000.0);
        } catch (NumberFormatException unexpected) {
            return 0;
        }
    }

    /** The last far_lod_pending=N of the effects profile in this process's log; -1 when none was logged yet. */
    private static int farLodPending(Context context) {
        java.util.List<String> lines = autoPresetLines(context, "far_lod_pending=");
        if (lines.isEmpty()) return -1;
        java.util.regex.Matcher m =
                java.util.regex.Pattern.compile("far_lod_pending=(\\d+)")
                        .matcher(lines.get(lines.size() - 1));
        return m.find() ? Integer.parseInt(m.group(1)) : -1;
    }

    /** The lines of this process's log that contain any of `needles` (the engine logs through logcat). */
    private static java.util.List<String> autoPresetLines(Context context, String... needles) {
        java.util.List<String> lines = new java.util.ArrayList<>();
        try {
            Process logcat =
                    new ProcessBuilder(
                                    "logcat",
                                    "-d",
                                    "-v",
                                    "epoch",
                                    "--pid=" + android.os.Process.myPid())
                            .redirectErrorStream(true)
                            .start();
            try (java.io.BufferedReader in =
                    new java.io.BufferedReader(
                            new java.io.InputStreamReader(
                                    logcat.getInputStream(), StandardCharsets.UTF_8))) {
                for (String line; (line = in.readLine()) != null; )
                    for (String needle : needles)
                        if (line.contains(needle)) {
                            lines.add(line.trim());
                            break;
                        }
            }
            logcat.waitFor();
        } catch (IOException error) {
            throw new IllegalStateException("Cannot read this process's log", error);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
        return lines;
    }

    /** This process's log (logcat of its pid) for the summary: frame pacing, effects profile, errors. */
    private static void ownLog(Context context, File target) {
        try {
            Process logcat =
                    new ProcessBuilder(
                                    "logcat",
                                    "-d",
                                    "-v",
                                    "epoch",
                                    "--pid=" + android.os.Process.myPid())
                            .redirectErrorStream(true)
                            .start();
            try (InputStream in = logcat.getInputStream();
                    OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[65536];
                for (int n; (n = in.read(buffer)) != -1; ) out.write(buffer, 0, n);
            }
            logcat.waitFor();
        } catch (IOException error) {
            Log.e(TAG, "Cannot read this process's log", error);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
    }

    // ---- helpers ----------------------------------------------------------------------------

    private interface Condition {
        boolean holds();
    }

    /** In the game process the engine thread must stay alive: a fatal engine error ends the wait at once. */
    private static volatile boolean watchEngine;

    private static void waitFor(String what, long limitMs, Condition condition) {
        long end = SystemClock.elapsedRealtime() + limitMs;
        while (!condition.holds()) {
            if (watchEngine && !GameActivity.isEngineAlive())
                throw new IllegalStateException(
                        "the engine stopped while waiting for " + what + " (see game-logcat.txt)");
            if (SystemClock.elapsedRealtime() > end)
                throw new IllegalStateException(
                        "timed out after "
                                + limitMs / 1000
                                + " s waiting for "
                                + what
                                + " (screen '"
                                + screen
                                + "', mode "
                                + mode
                                + ")");
            SystemClock.sleep(250);
        }
    }

    private static void onUi(android.app.Activity activity, Runnable action)
            throws InterruptedException {
        CountDownLatch done = new CountDownLatch(1);
        RuntimeException[] failure = new RuntimeException[1];
        activity.runOnUiThread(
                () -> {
                    try {
                        action.run();
                    } catch (RuntimeException e) {
                        failure[0] = e;
                    } finally {
                        done.countDown();
                    }
                });
        if (!done.await(10, TimeUnit.SECONDS))
            throw new IllegalStateException("UI thread did not run a test loop step within 10 s");
        if (failure[0] != null) throw failure[0];
    }

    /**
     * A game screenshot that does not end the run: PixelCopy waits a bounded time for the surface's
     * frame (ERROR_TIMEOUT = 2 when a frame takes longer, as on a software renderer at Высокая), so it is
     * tried SHOT_ATTEMPTS times; a screenshot that still fails is logged as an error and listed in the
     * result, which then fails, while the measurements go on.
     */
    private static void shot(Context context, SurfaceView view, String name, JSONArray problems) {
        String last = "";
        for (int attempt = 1; attempt <= SHOT_ATTEMPTS; ++attempt) {
            try {
                shot(context, view, name);
                return;
            } catch (Exception failure) {
                last = String.valueOf(failure.getMessage());
                Log.w(TAG, "attempt " + attempt + ": " + last);
            }
            SystemClock.sleep(SHOT_RETRY_MS);
        }
        Log.e(TAG, "screenshot " + name + " failed " + SHOT_ATTEMPTS + " times: " + last);
        note(context, "ERROR screenshot " + name + " failed " + SHOT_ATTEMPTS + " times: " + last);
        problems.put(name + ": " + last);
    }

    /** A PNG of the surface (GL content included) into the results. */
    private static void shot(Context context, SurfaceView view, String name) throws Exception {
        if (view.getWidth() <= 0 || view.getHeight() <= 0)
            throw new IllegalStateException("no surface for screenshot " + name);
        Bitmap bitmap =
                Bitmap.createBitmap(view.getWidth(), view.getHeight(), Bitmap.Config.ARGB_8888);
        CountDownLatch done = new CountDownLatch(1);
        int[] code = {-1};
        HandlerThread thread = new HandlerThread("StormTestLoopShot");
        thread.start();
        try {
            PixelCopy.request(
                    view,
                    bitmap,
                    result -> {
                        code[0] = result;
                        done.countDown();
                    },
                    new Handler(thread.getLooper()));
            if (!done.await(10, TimeUnit.SECONDS))
                throw new IllegalStateException("screenshot " + name + " timed out");
        } finally {
            thread.quitSafely();
        }
        if (code[0] != PixelCopy.SUCCESS)
            throw new IllegalStateException("screenshot " + name + " failed: PixelCopy " + code[0]);
        // The window is composited opaque: its alpha channel is not part of the picture (the
        // engine's final passes
        // write colour only). Kept, it showed the night title's sky as white and black bands in a
        // PNG viewer.
        bitmap.setHasAlpha(false);
        File file = new File(results(context), name + ".png");
        try (OutputStream out = new FileOutputStream(file)) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out);
        }
        note(
                context,
                "screenshot "
                        + file.getName()
                        + " "
                        + bitmap.getWidth()
                        + "x"
                        + bitmap.getHeight());
    }

    /** "" when the screenshot shows a picture; otherwise what it is (missing, a single colour). Sampled on a grid. */
    private static String singleColour(File png) {
        Bitmap bitmap = android.graphics.BitmapFactory.decodeFile(png.getPath());
        if (bitmap == null) return "missing (" + png.getName() + " not written)";
        int first = bitmap.getPixel(0, 0), differing = 0, samples = 0;
        for (int y = 0; y < bitmap.getHeight(); y += Math.max(1, bitmap.getHeight() / 32))
            for (int x = 0;
                    x < bitmap.getWidth();
                    x += Math.max(1, bitmap.getWidth() / 32), ++samples)
                if (Math.abs(brightness(bitmap.getPixel(x, y)) - brightness(first)) > 24)
                    ++differing;
        return differing * 50 < samples
                ? String.format(java.util.Locale.ROOT, "a single colour (#%06x)", first & 0xffffff)
                : "";
    }

    private static int brightness(int argb) {
        return ((argb >> 16) & 0xff) + ((argb >> 8) & 0xff) + (argb & 0xff);
    }

    private static void put(JSONObject object, String key, Object value) {
        try {
            object.put(key, value);
        } catch (JSONException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    private static void write(File file, byte[] bytes) {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(bytes);
        } catch (IOException error) {
            throw new IllegalStateException("Cannot write " + file, error);
        }
    }

    private static byte[] read(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            return readAll(in);
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        for (int n; (n = in.read(buffer)) != -1; ) out.write(buffer, 0, n);
        return out.toByteArray();
    }
}
