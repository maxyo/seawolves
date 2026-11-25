package org.stormengine.port;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import android.view.PixelCopy;
import android.view.SurfaceView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Scenario driver for automated runs on an emulator or a phone (debug builds made with build_apk.py --test-loop;
 * every other build gets android/game/testloop/off). A runner starts the launcher with the Game Loop intent
 * com.google.intent.action.TEST_LOOP, the integer extra "scenario" and, optionally, a result file as the intent's
 * data (tools/android/emu_smoke.py does). Results go to {@link #results}; android/game/TESTING.md lists them.
 *
 * Scenarios:
 *  1 launcher: the launcher screen, its game files screen, the system folder picker it opens; no game data needed.
 *  9 boot smoke: installs the pushed game subset (OBB), starts the game at its main menu, waits for the automatic
 *    graphics preset, «Продолжить» loads the town save, one screenshot of the town that must not be a single colour.
 * Every step has a hard limit; a missed one ends the run with status "failed" and its reason.
 */
final class TestLoop {
    static final String ACTION = "com.google.intent.action.TEST_LOOP";
    private static final String TAG = "StormTestLoop";
    private static final long SETTLE_MS = 8_000;
    private static final long MENU_LIMIT_MS = 240_000,
            SCENE_LIMIT_MS = 240_000,
            RUN_LIMIT_MS = 840_000;
    private static final long AUTO_LIMIT_MS = 240_000;

    /** The town save of the pushed subset (tools/android/game_subset.py --save). */
    private static final String SCENE_SAVE = "Сен-Пьер улицы";

    private static final int SHOT_ATTEMPTS = 4;
    private static final long SHOT_RETRY_MS = 3_000;

    /**
     * The engine's final lines of the automatic graphics preset: a choice or why there is none. A rung's result and
     * "measuring ..." are progress, not an outcome: leaving the title before the outcome aborts the measurement.
     */
    private static final String AUTO_CHOSE = "graphics auto preset: chose ";

    private static final String[] AUTO_OUTCOMES = {
        AUTO_CHOSE,
        "graphics auto preset: measurement aborted: ",
        "graphics auto preset: not measured this time: ",
        "graphics auto preset: off",
        " was measured (version ",
        "graphics auto preset: the app reports no fx_preset_auto",
    };

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
                                    Build.MANUFACTURER
                                            + " "
                                            + Build.MODEL
                                            + " ("
                                            + Build.DEVICE
                                            + ")");
                            put(summary, "sdk", Build.VERSION.SDK_INT);
                            try {
                                if (scenario == 1) launcherScenario(launcher, summary);
                                else if (scenario == 9) {
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
        ActivityManager manager = launcher.getSystemService(ActivityManager.class);
        if (manager != null && manager.getRunningAppProcesses() != null)
            for (ActivityManager.RunningAppProcessInfo process : manager.getRunningAppProcesses())
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
        // that the runner is on top again when the launcher finishes.
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
        // A first install for the graphics choice: no stored preset, measurement or player's
        // choice, so the automatic preset measures again.
        if (!launcher.getSharedPreferences("graphics", Context.MODE_PRIVATE)
                .edit()
                .clear()
                .commit()) throw new IOException("Cannot clear the graphics preferences");
        note(launcher, "graphics preferences cleared (first-install state)");
        mainMenuStart = true;
        // «Продолжить» loads the newest record: the scenario's save is made the newest.
        File save = new File(launcher.getFilesDir(), "storm/SaveData/player/" + SCENE_SAVE);
        if (!save.isFile() || !save.setLastModified(System.currentTimeMillis()))
            throw new IOException(
                    "the scenario's save "
                            + save
                            + " is missing from the pushed subset or cannot be touched");
        note(launcher, "newest save: " + save.getName());
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
        // Retried: a slow device may not have drawn the launcher yet. The boot smoke judges the
        // game's frame, not the launcher's: a failed launcher screenshot is reported, not fatal.
        JSONArray launcherShot = new JSONArray();
        shot(launcher, launcher.ui, "01-launcher", launcherShot);
        if (launcherShot.length() > 0) put(summary, "launcherScreenshotFailure", launcherShot);
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
        // it.
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
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        if (manager == null || manager.getRunningAppProcesses() == null) return false;
        for (ActivityManager.RunningAppProcessInfo process : manager.getRunningAppProcesses())
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
        Arrays.sort(
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
                            new BufferedInputStream(new FileInputStream(obb), 1 << 20))) {
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
                            // Screenshots that failed: the run fails, but the timings stay.
                            JSONArray problems = new JSONArray();
                            String status = "passed", reason = "";
                            long start = SystemClock.elapsedRealtime();
                            try {
                                note(game, "game scenario " + scenario + " in the game process");
                                waitFor(
                                        "the engine thread",
                                        20_000,
                                        GameActivity::engineThreadAlive);
                                watchEngine = true;
                                waitFor(
                                        "the title screen (interface mainmenu)",
                                        MENU_LIMIT_MS,
                                        () -> screen.equals("mainmenu"));
                                put(result, "mainMenuSeconds", secondsSince(start));
                                note(game, "title screen up");
                                // A first start measures the title scene and picks the preset:
                                // nothing may leave the title before the outcome, or the pick never
                                // happens.
                                waitFor(
                                        "the automatic graphics preset's outcome",
                                        AUTO_LIMIT_MS,
                                        () -> !logLines(game, AUTO_OUTCOMES).isEmpty());
                                put(result, "autoPresetSeconds", secondsSince(start));
                                List<String> outcome = logLines(game, AUTO_OUTCOMES);
                                if (logLines(game, AUTO_CHOSE).isEmpty()) {
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
                                                logLines(
                                                        game,
                                                        "graphics auto preset:",
                                                        "graphics preset:")));
                                // The title's «Продолжить» (engine UI state channel titleIntent).
                                onUi(
                                        game,
                                        () ->
                                                ui.setState(
                                                        "titleIntent",
                                                        "{\"intent\":\"Continue\"}"
                                                                .getBytes(StandardCharsets.UTF_8)));
                                waitFor(
                                        "the saved scene (gameplay without an interface)",
                                        SCENE_LIMIT_MS,
                                        () -> !mode.equals("unknown") && screen.isEmpty());
                                put(result, "sceneSeconds", secondsSince(start));
                                put(result, "mode", mode);
                                note(game, "saved scene up, mode " + mode);
                                SystemClock.sleep(SETTLE_MS);
                                shot(game, surface, "10-scene", problems);
                                String blank =
                                        singleColour(new File(results(game), "10-scene.png"));
                                if (!blank.isEmpty())
                                    throw new IllegalStateException(
                                            "the saved scene's frame is " + blank);
                                note(game, "boot smoke: the saved scene draws");
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
                            // Last: the launcher ends the loop as soon as it sees this file. The
                            // game Activity then ends; the launcher, back on top and no longer
                            // frozen, sees it.
                            write(
                                    doneFile(game),
                                    result.toString().getBytes(StandardCharsets.UTF_8));
                            game.runOnUiThread(game::finish);
                        },
                        "StormTestLoopGame")
                .start();
    }

    private static double secondsSince(long start) {
        return (SystemClock.elapsedRealtime() - start) / 1000.0;
    }

    private static Process logcat() throws IOException {
        return new ProcessBuilder(
                        "logcat", "-d", "-v", "epoch", "--pid=" + android.os.Process.myPid())
                .redirectErrorStream(true)
                .start();
    }

    /** The lines of this process's log that contain any of `needles` (the engine logs through logcat). */
    private static List<String> logLines(Context context, String... needles) {
        List<String> lines = new ArrayList<>();
        try {
            Process logcat = logcat();
            try (BufferedReader in =
                    new BufferedReader(
                            new InputStreamReader(
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

    /** This process's log for the results: frame pacing, effects profile, errors. */
    private static void ownLog(Context context, File target) {
        try {
            Process logcat = logcat();
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
            if (watchEngine && !GameActivity.engineThreadAlive())
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

    private static void onUi(Activity activity, Runnable action) throws InterruptedException {
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
        // The window is composited opaque: the engine's final passes write colour only, so the
        // alpha channel is not part of the picture.
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
        Bitmap bitmap = BitmapFactory.decodeFile(png.getPath());
        if (bitmap == null) return "missing (" + png.getName() + " not written)";
        int first = bitmap.getPixel(0, 0), differing = 0, samples = 0;
        for (int y = 0; y < bitmap.getHeight(); y += Math.max(1, bitmap.getHeight() / 32))
            for (int x = 0;
                    x < bitmap.getWidth();
                    x += Math.max(1, bitmap.getWidth() / 32), ++samples)
                if (Math.abs(brightness(bitmap.getPixel(x, y)) - brightness(first)) > 24)
                    ++differing;
        return differing * 50 < samples
                ? String.format(Locale.ROOT, "a single colour (#%06x)", first & 0xffffff)
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
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        for (int n; (n = in.read(buffer)) != -1; ) out.write(buffer, 0, n);
        return out.toByteArray();
    }
}
