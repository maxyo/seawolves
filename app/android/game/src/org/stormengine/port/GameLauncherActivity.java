package org.stormengine.port;

import android.app.ActivityManager;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.WindowManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The launcher: game files are imported through the Storage Access Framework (a folder,
 * or any mix of archives, installers and their volumes, also nested) into app-specific
 * storage; then «Играть». No shared-storage permission is requested. Drawn by the portable
 * UI (screen "launcher", and "about" in the same host); this activity publishes the import
 * snapshot and the storage state and acts on the player's verbs. The game's title screen brings
 * it to the front with {@link #EXTRA_ABOUT} for about / licences alone; its ← brings the game
 * back (the game runs in its own process: one UI host per process, and one launcher).
 */
public final class GameLauncherActivity extends UiScreenActivity {
    static final String EXTRA_ABOUT = "about";

    /** The game activity did not start on a full storage: the bytes still to free (launcher result start_no_space). */
    static final String EXTRA_START_NO_SPACE = "org.stormengine.port.START_NO_SPACE";

    /** Pid of the game process that ends after the pause menu's exit: start the game at its main menu after it. */
    static final String EXTRA_MAIN_MENU_AFTER = "org.stormengine.port.MAIN_MENU_AFTER";

    private final Handler restartHandler = new Handler(Looper.getMainLooper());
    private long restartRequested;
    private final GameRestart restart =
            new GameRestart(
                    new GameRestart.Host() {
                        @Override
                        public boolean alive(int pid) {
                            ActivityManager manager = getSystemService(ActivityManager.class);
                            List<ActivityManager.RunningAppProcessInfo> running =
                                    manager.getRunningAppProcesses();
                            if (running != null)
                                for (ActivityManager.RunningAppProcessInfo process : running)
                                    if (process.pid == pid) return true;
                            return false;
                        }

                        @Override
                        public void start() {
                            Log.i(
                                    "StormLauncher",
                                    "Game process ended "
                                            + (SystemClock.elapsedRealtime() - restartRequested)
                                            + " ms after the exit; starting the game at its main menu");
                            startActivity(
                                    new Intent(GameLauncherActivity.this, GameActivity.class)
                                            .putExtra(GameActivity.EXTRA_MAIN_MENU, true));
                        }

                        @Override
                        public void failed(String reason) {
                            Log.e(
                                    "StormLauncher",
                                    "Cannot start the game at its main menu: " + reason);
                            messageKey = "start_failed";
                            publish();
                        }

                        @Override
                        public void later(Runnable step, long millis) {
                            restartHandler.postDelayed(step, millis);
                        }

                        @Override
                        public long now() {
                            return SystemClock.elapsedRealtime();
                        }
                    });

    /** Set id of a finished download (github library), imported with ImportCoordinator.startTree(File). */
    static final String EXTRA_IMPORT_DOWNLOAD = "org.stormengine.port.IMPORT_DOWNLOAD";

    static final int IMPORT = 1,
            IMPORT_FOLDER = 3; // IMPORT_FOLDER: the test loop closes its picker (TestLoop)
    private AboutScreen about;

    /** About is shown; `aboutOnly`: opened from the game for about alone, its ← finishes. */
    private boolean showingAbout, aboutOnly;

    private ImportCoordinator imports;

    /** The files of a ready game are being changed (setup over a ready game, with ←). */
    private boolean changingFiles, wasBusy, cancelRequested;

    /** The last readyPath failure written to the log; "" once the game is ready. */
    private String loggedProblem = "";

    /** A launcher message (picker missing, game still running, storage problem) until the next action. */
    private String messageKey = "";

    /** The megabytes still to free with messageKey start_no_space; 0 otherwise. */
    private long messageMegabytes;

    /** Play was tapped and the game is being started (play()): later taps start nothing, «Играть» shows it. */
    private boolean starting;

    @Override
    protected boolean gameFilesChanging() {
        return imports.busy();
    }

    /** The installed game as the import describes a copy, read once per game files screen (off the main thread). */
    private JSONObject installed;

    private boolean describing;
    private String[] exitNotice;

    /** The phone misses the minimum requirements (MinimumRequirements, checked at each resume) and the player has not
     *  closed the notice (MIN_REQ_CLOSED, kept for good). */
    private boolean belowMinimum;

    private String minimumLogged;
    private static final String MIN_REQ_CLOSED = "below_minimum_closed";
    private String passwordFor;
    private final ImportCoordinator.Listener importListener = this::publish;

    @Override
    protected String screen() {
        return showingAbout ? "about" : "launcher";
    }

    @Override
    protected boolean bringsTaskBack() {
        boolean back =
                LauncherEntry.bringsTaskBack(
                        isTaskRoot(), getIntent().getAction(), getIntent().getCategories());
        if (back)
            Log.i(
                    "StormLauncher",
                    "Home screen start over the running task: the task is brought back, no second launcher");
        return back;
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        if (isFinishing()) return; // bringsTaskBack
        imports = ImportCoordinator.get(this);
        about = new AboutScreen(this, ui, this::publish);
        aboutOnly = getIntent().getBooleanExtra(EXTRA_ABOUT, false);
        showingAbout = aboutOnly;
        if (state != null) {
            changingFiles = state.getBoolean("changingFiles");
            showingAbout = state.getBoolean("showingAbout", aboutOnly);
            messageKey = state.getString("messageKey", "");
            messageMegabytes = state.getLong("messageMegabytes");
            exitNotice = state.getStringArray("exitNotice");
        }
        restartAtMainMenu(getIntent());
        startRefused(getIntent());
        // The scenario driver of a --test-loop debug build (a no-op otherwise).
        TestLoop.launcher(this, getIntent());
    }

    /** The game activity refused to start on a full storage (EXTRA_START_NO_SPACE): said until the next action. */
    private boolean startRefused(Intent intent) {
        long bytes = intent.getLongExtra(EXTRA_START_NO_SPACE, -1);
        if (bytes < 0) return false;
        intent.removeExtra(EXTRA_START_NO_SPACE);
        messageKey = "start_no_space";
        messageMegabytes = Math.max(1, (bytes + (1 << 20) - 1) >> 20);
        return true;
    }

    /** The pause menu's exit: the game comes back at its main menu in a fresh process. */
    private void restartAtMainMenu(Intent intent) {
        int pid = intent.getIntExtra(EXTRA_MAIN_MENU_AFTER, -1);
        if (pid < 0) return;
        intent.removeExtra(EXTRA_MAIN_MENU_AFTER);
        restartRequested = SystemClock.elapsedRealtime();
        if (!restart.request(pid))
            Log.w(
                    "StormLauncher",
                    "Restart at the main menu already waiting; request for " + pid + " dropped");
    }

    @Override
    protected void onStart() {
        super.onStart();
        imports.add(importListener);
        importDownload(getIntent());
    }

    @Override
    protected void onStop() {
        imports.remove(importListener);
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        starting = false;
        // The game runs in its own process: after Play and a crash the player comes back to this
        // launcher (resumed, not created), so the notice of an exit nobody has seen yet is taken
        // here, each time (logged); shown in debug builds only, release builds keep it in logcat
        // and the diagnostics export.
        String[] notice = ExitDiagnostics.takeNotice(this);
        if (notice != null && DebugBuild.on(this)) exitNotice = notice;
        belowMinimum = checkMinimum();
        publish();
    }

    /** The minimum requirements check (logged once per verdict); true when the notice is to be shown. */
    private boolean checkMinimum() {
        ActivityManager manager = getSystemService(ActivityManager.class);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(memory);
        int vulkan = 0;
        for (FeatureInfo feature : getPackageManager().getSystemAvailableFeatures())
            if (PackageManager.FEATURE_VULKAN_HARDWARE_VERSION.equals(feature.name))
                vulkan = Math.max(vulkan, feature.version);
        int gles = manager.getDeviceConfigurationInfo().reqGlEsVersion;
        double lowFrame;
        try {
            lowFrame = MinimumRequirements.recordedLowFrame(getFilesDir());
        } catch (IOException error) {
            Log.e(
                    "StormLauncher",
                    "minimum requirements: the «Низкая» frame time is left out",
                    error);
            lowFrame = -1;
        }
        boolean below =
                !MinimumRequirements.misses(memory.totalMem, vulkan, gles, lowFrame).isEmpty();
        boolean closed =
                getSharedPreferences("launcher", MODE_PRIVATE).getBoolean(MIN_REQ_CLOSED, false);
        String line =
                MinimumRequirements.describe(memory.totalMem, vulkan, gles, lowFrame)
                        + (below
                                ? closed ? "; the player closed the notice" : "; notice shown"
                                : "");
        if (!line.equals(minimumLogged)) {
            if (below) Log.w("StormLauncher", line);
            else Log.i("StormLauncher", line);
            minimumLogged = line;
        }
        return below && !closed;
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        state.putBoolean("changingFiles", changingFiles);
        state.putBoolean("showingAbout", showingAbout);
        state.putString("messageKey", messageKey);
        state.putLong("messageMegabytes", messageMegabytes);
        state.putStringArray("exitNotice", exitNotice);
        super.onSaveInstanceState(state);
    }

    @Override
    protected JSONObject screenState() throws JSONException {
        if (showingAbout) return about.state();
        ImportCoordinator.Snapshot snapshot = imports.snapshot();
        boolean busy = snapshot.busy;
        // Large archives take many minutes; the import continues only while the app stays open.
        if (snapshot.busy) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (!snapshot.busy) cancelRequested = false;
        String path = null, problem = "";
        if (!busy) {
            try {
                path = GameStorage.readyPath(this);
                loggedProblem = "";
            } catch (IOException error) {
                problem = storageProblem(error);
                // The state is rebuilt on every publish: log each new cause once.
                if (!problem.equals("no_game") && !error.toString().equals(loggedProblem)) {
                    loggedProblem = error.toString();
                    Log.e("StormLauncher", "The installed game is not ready: " + problem, error);
                }
            }
            if (wasBusy && path != null)
                changingFiles = false; // a finished import returns to «Играть»
        }
        if (busy || path == null) installed = null;
        else if (changingFiles && installed == null && !describing) describeInstalled();
        wasBusy = busy;
        JSONObject state = new JSONObject();
        state.put("phase", busy ? "busy" : path != null && !changingFiles ? "ready" : "setup");
        state.put("starting", starting);
        state.put("files", changingFiles && path != null);
        state.put("hasGame", path != null);
        state.put("problem", problem);
        if (installed != null && changingFiles) state.put("installed", installed);
        if (belowMinimum) state.put("belowMinimum", true);
        if (exitNotice != null)
            state.put(
                    "exit",
                    new JSONObject()
                            .put("reason", exitNotice[0])
                            .put("when", ExitDiagnostics.when(exitNotice[1], language())));
        if (!messageKey.isEmpty())
            state.put(
                    "result",
                    new JSONObject()
                            .put("key", messageKey)
                            .put(
                                    "args",
                                    messageKey.equals("start_no_space")
                                            ? new JSONArray().put(messageMegabytes)
                                            : new JSONArray()));
        else if (!snapshot.resultKey.isEmpty()) {
            JSONArray args = new JSONArray();
            for (Object arg : snapshot.resultArgs) args.put(arg);
            state.put("result", new JSONObject().put("key", snapshot.resultKey).put("args", args));
        }
        if (snapshot.busy) state.put("import", importState(snapshot));
        return state;
    }

    private void describeInstalled() {
        describing = true;
        File game = GameStorage.imported(this), scratch = new File(getCacheDir(), "installed-scan");
        new Thread(
                        () -> {
                            JSONObject found = null;
                            try {
                                ArchiveInstaller.GameRoot root =
                                        ArchiveInstaller.describeInstalled(
                                                game, scratch, getAssets());
                                found = copyState(root);
                            } catch (Exception error) {
                                Log.e("StormLauncher", "Cannot describe the installed game", error);
                            }
                            final JSONObject result = found;
                            runOnUiThread(
                                    () -> {
                                        describing = false;
                                        installed = result;
                                        if (result != null) publish();
                                    });
                        },
                        "StormInstalledGame")
                .start();
    }

    /**
     * A game copy as the launcher screen reads one (engine launcher.cpp: the installed game and each choice), with the
     * script patch identification the archive reader made (identified, patchSet, testedVersions).
     */
    static JSONObject copyState(ArchiveInstaller.GameRoot root) throws JSONException {
        ArchiveInstaller.Identification identity = root.identification;
        return new JSONObject()
                .put("id", root.id)
                .put("name", root.name())
                .put("version", root.version)
                .put("languages", new JSONArray(root.languages))
                .put("defaultLanguage", root.defaultLanguage)
                .put("bytes", root.bytes)
                .put("files", root.files)
                .put("patches", new JSONArray(root.patchMarkers))
                .put("place", root.place())
                .put("missing", new JSONArray(root.missingVolumes))
                .put("identified", identity.identified)
                .put(
                        "patchSet",
                        identity.setId.isEmpty()
                                ? JSONObject.NULL
                                : new JSONObject()
                                        .put("id", identity.setId)
                                        .put("version", identity.setVersion))
                .put("testedVersions", new JSONArray(identity.testedVersions));
    }

    private JSONObject importState(ImportCoordinator.Snapshot snapshot) throws JSONException {
        JSONObject state = new JSONObject();
        state.put("recovering", snapshot.recovering);
        state.put("cancelling", cancelRequested);
        state.put("canCancel", snapshot.canCancel);
        state.put(
                "stage",
                snapshot.stage == null ? "" : snapshot.stage.name().toLowerCase(Locale.ROOT));
        state.put("bytes", snapshot.bytes);
        state.put("total", snapshot.totalBytes);
        state.put("files", snapshot.files);
        JSONArray choices = new JSONArray();
        for (ArchiveInstaller.GameRoot root : snapshot.choices) choices.put(copyState(root));
        state.put("choices", choices);
        passwordFor = snapshot.passwordFor;
        if (snapshot.passwordFor != null)
            state.put(
                    "password",
                    new JSONObject()
                            .put("for", snapshot.passwordFor)
                            .put("retry", snapshot.passwordRetry));
        return state;
    }

    @Override
    protected void onAction(String verb, String value) {
        if (showingAbout) {
            if (!about.onAction(verb, value)) closeAbout();
            publish();
            return;
        }
        switch (verb) {
            case "play":
                play();
                break;
            case "files":
                clearMessages();
                changingFiles = true;
                break;
            case "files-close":
                clearMessages();
                changingFiles = false;
                break;
            case "delete-game":
                clearMessages();
                if (canChangeSource()) imports.deleteGame();
                break;
            case "folder":
                pick(true);
                break;
            case "archive":
                pick(false);
                break;
            case "library":
                clearMessages();
                startActivity(new Intent(this, DownloadLibraryActivity.class));
                break;
            case "about":
                showingAbout = true;
                break;
            case "cancel":
                cancelRequested = imports.snapshot().canCancel;
                imports.cancel();
                break;
            case "choose":
                choose(value);
                break;
            case "password-cancel":
                imports.answerPassword(null);
                break;
            case "below-minimum-dismiss":
                belowMinimum = false;
                if (!getSharedPreferences("launcher", MODE_PRIVATE)
                        .edit()
                        .putBoolean(MIN_REQ_CLOSED, true)
                        .commit())
                    Log.e(
                            "StormLauncher",
                            "minimum requirements: the closed notice could not be stored; it comes back at the next start");
                Log.i("StormLauncher", "minimum requirements: the player closed the notice");
                break;
            default:
                Log.e("StormLauncher", "Unknown launcher action " + verb + " " + value);
        }
        publish();
    }

    /** The masked field: Done is the answer (an empty answer keeps the question open). */
    @Override
    protected void onEdited(String field, String text) {
        if (field.equals("PASSWORD") && passwordFor != null && !text.isEmpty())
            imports.answerPassword(text);
        else if (!field.equals("PASSWORD")) super.onEdited(field, text);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent.getBooleanExtra(EXTRA_ABOUT, false)) {
            aboutOnly = true;
            showingAbout = true;
            publish();
        }
        restartAtMainMenu(intent);
        if (startRefused(intent)) publish();
        importDownload(intent);
        TestLoop.launcher(this, intent);
    }

    /** The download library offers a finished set: its files are imported in place. */
    private void importDownload(Intent intent) {
        String id = intent.getStringExtra(EXTRA_IMPORT_DOWNLOAD);
        if (id == null) return;
        intent.removeExtra(EXTRA_IMPORT_DOWNLOAD);
        clearMessages();
        if (canChangeSource()) {
            try {
                File source = TorrentDownloadService.catalog(this).importSource(id);
                changingFiles = true;
                Context app = getApplicationContext();
                // The set is used up: removed by the download process, which owns the library.
                imports.startTree(
                        source,
                        () ->
                                TorrentDownloadService.command(
                                        app, TorrentDownloadService.DELETE, id, null));
            } catch (IOException error) {
                Log.e("StormLauncher", "Cannot import download set " + id, error);
                messageKey = "import_failed";
            }
        }
        publish();
    }

    private void closeAbout() {
        showingAbout = false;
        if (!aboutOnly) return;
        aboutOnly = false;
        if (GameActivity.isEngineAlive(this))
            startActivity(
                    new Intent(this, GameActivity.class)
                            .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
    }

    @Override
    protected void onBack() {
        if (showingAbout) {
            closeAbout();
            publish();
        } else if (changingFiles && !imports.busy()) {
            changingFiles = false;
            publish();
        } else finish();
    }

    private void clearMessages() {
        messageKey = "";
        imports.clearResult();
    }

    /** The engine cache left on the other cache volume (GameCache.stale); logged, never silent. */
    private void removeOldCaches() throws IOException {
        File external = getExternalCacheDir();
        for (GameCache.Removed removed :
                GameCache.remove(
                        GameCache.stale(
                                getCacheDir(), external, GameCache.root(external, getCacheDir()))))
            Log.i("StormCache", "Removed the old cache " + removed);
    }

    /**
     * Checks the imported game and removes an old cache off the main thread: both walk files on disk and would hold
     * the launcher's frames.
     * A second tap while it runs starts nothing more.
     */
    private void play() {
        clearMessages();
        if (imports.busy() || starting) return;
        if (restart.pending()) {
            Log.i("StormLauncher", "Play ignored: the game is restarting at its main menu");
            return;
        }
        starting = true;
        publish(); // «Играть» shows that it is starting
        final long started = SystemClock.elapsedRealtime();
        new Thread(
                        () -> {
                            Exception failure = null;
                            try {
                                GameStorage.readyPath(this);
                                removeOldCaches();
                            } catch (Exception error) {
                                failure = error;
                            }
                            final Exception failed = failure;
                            runOnUiThread(
                                    () -> {
                                        Log.i(
                                                "StormLauncher",
                                                "Game files checked in "
                                                        + (SystemClock.elapsedRealtime() - started)
                                                        + " ms");
                                        if (failed == null && !isFinishing()) {
                                            // Test loop scenario 9 starts at the main menu, as
                                            // after the pause menu's exit (no intro videos).
                                            startActivity(
                                                    new Intent(this, GameActivity.class)
                                                            .putExtra(
                                                                    GameActivity.EXTRA_MAIN_MENU,
                                                                    TestLoop.startAtMainMenu()));
                                            return;
                                        }
                                        starting = false;
                                        publish();
                                        if (failed == null) return;
                                        Log.e(
                                                "StormLauncher",
                                                "Cannot start the imported game",
                                                failed);
                                        messageKey =
                                                failed
                                                                instanceof
                                                                ResourceImporter
                                                                        .RecoveryRequiredException
                                                        ? "recovery_required"
                                                        : "start_failed";
                                        publish();
                                    });
                        },
                        "StormLauncherPlay")
                .start();
    }

    private void choose(String id) {
        for (ArchiveInstaller.GameRoot root : imports.snapshot().choices)
            if (root.id.equals(id)) {
                imports.choose(root);
                return;
            }
        Log.e("StormLauncher", "Chosen game copy is no longer offered: " + id);
    }

    private void pick(boolean folder) {
        clearMessages();
        if (!canChangeSource()) return;
        // Multiple selection: archives, installers with their .bin volumes, multi-volume parts, in
        // any mix.
        Intent intent = folder ? new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE) : openDocument(true);
        try {
            startActivityForResult(intent, folder ? IMPORT_FOLDER : IMPORT);
        } catch (ActivityNotFoundException unavailable) {
            messageKey = "picker_unavailable";
        }
    }

    /** The system picker for any openable file (game import; About's save restore with one file). */
    static Intent openDocument(boolean multiple) {
        Intent intent =
                new Intent(Intent.ACTION_OPEN_DOCUMENT)
                        .addCategory(Intent.CATEGORY_OPENABLE)
                        .setType("*/*");
        if (multiple) intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        return intent;
    }

    private boolean canChangeSource() {
        if (imports.busy()) return false;
        if (GameActivity.isEngineAlive(this)) {
            messageKey = "exit_game_first";
            return false;
        }
        return true;
    }

    private static String storageProblem(Exception error) {
        if (error instanceof ResourceImporter.RecoveryRequiredException) return "recovery_required";
        if (error instanceof ResourceImporter.MissingGameRootException) return "no_game";
        return "start_failed";
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == AboutScreen.RESTORE) {
            if (result == RESULT_OK && data != null && data.getData() != null)
                about.restorePicked(data.getData());
            else Log.i("StormLauncher", "Save restore: the picker closed without a file");
            publish();
            return;
        }
        if (request != IMPORT && request != IMPORT_FOLDER || result != RESULT_OK || data == null)
            return;
        List<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null)
            for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                Uri uri = data.getClipData().getItemAt(i).getUri();
                if (uri != null && !uris.contains(uri)) uris.add(uri);
            }
        if (uris.isEmpty() && data.getData() != null) uris.add(data.getData());
        if (uris.isEmpty() || !canChangeSource()) {
            publish();
            return;
        }
        if (request == IMPORT_FOLDER) imports.startTree(uris.get(0));
        else imports.startDocuments(uris);
        publish();
    }
}
