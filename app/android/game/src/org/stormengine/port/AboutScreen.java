package org.stormengine.port;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * About / licences, a screen of the launcher's UI host (portable UI screen "about"):
 * application and engine versions with exact source revisions (GPL-3.0 corresponding
 * source), every bundled notice from assets/licenses, opened verbatim, «Поделиться отчётом»
 * ({@link DiagnosticsExport}), «Отправить сохранение» / «Отправить все сохранения» ({@link SaveExport}) and
 * «Восстановить сохранение» ({@link SaveRestore}: the system file picker, then a question when a save of that name
 * is already there, then the result, each in the game's language from the engine's about screen).
 */
final class AboutScreen {
    private static final String TAG = "StormAbout";
    private static final int TEXT_LIMIT = 192 * 1024, TAIL = 16 * 1024;

    /** The picker's request code (GameLauncherActivity.onActivityResult hands the result to {@link #restorePicked}). */
    static final int RESTORE = 4;

    private final Activity context;
    private final StormUiView ui;
    private final Runnable changed;
    private final Handler main = new Handler(Looper.getMainLooper());
    private JSONObject info;
    private String[] notices;
    private String openFile, openText;
    private boolean openTruncated;

    /** The save picker is open: the saves listed when it opened. */
    private List<SaveExport.Save> saves;

    /** «Восстановить сохранение»: a file is being read or written; the plan waiting for the player's answer; the result. */
    private boolean restoring;

    private SaveRestore.Plan conflict;
    private String resultKey;
    private JSONArray resultArgs;

    /** `changed` publishes the screen again (on the main thread). */
    AboutScreen(Activity context, StormUiView ui, Runnable changed) {
        this.context = context;
        this.ui = ui;
        this.changed = changed;
    }

    JSONObject state() throws JSONException {
        if (info == null) {
            try {
                info = new JSONObject(new String(read("build-info.json"), StandardCharsets.UTF_8));
            } catch (IOException | JSONException missing) {
                throw new IllegalStateException("The APK has no valid build-info.json", missing);
            }
            try {
                notices = context.getAssets().list("licenses");
            } catch (IOException error) {
                throw new IllegalStateException("Cannot list the bundled notices", error);
            }
            if (notices == null) notices = new String[0];
            Arrays.sort(notices, String.CASE_INSENSITIVE_ORDER);
        }
        JSONObject app =
                new JSONObject()
                        .put("version", info.optString("versionName"))
                        .put("code", info.optInt("versionCode"))
                        .put("commit", info.optString("commit"))
                        .put("source", info.optString("source"));
        // The graphics API of the engine's last device and why (moved here from the settings
        // screen).
        String[] backend = GraphicsSettings.renderBackend(context);
        if (backend != null) app.put("renderBackend", backend[0]);
        JSONObject state = new JSONObject().put("app", app).put("notices", new JSONArray(notices));
        if (openFile != null)
            state.put(
                    "open",
                    new JSONObject()
                            .put("file", openFile)
                            .put("text", openText)
                            .put("truncated", openTruncated));
        if (saves != null) {
            JSONArray rows = new JSONArray();
            SimpleDateFormat date = new SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.ROOT);
            for (SaveExport.Save save : saves)
                rows.put(
                        new JSONObject()
                                .put("key", save.key)
                                .put("name", save.name)
                                .put("date", date.format(new Date(save.written))));
            state.put("saves", rows);
        }
        if (restoring) state.put("restore", new JSONObject().put("busy", true));
        else if (conflict != null) {
            JSONArray names = new JSONArray();
            for (SaveRestore.Item item : conflict.conflicts()) names.put(item.name);
            state.put(
                    "restore",
                    new JSONObject()
                            .put(
                                    "conflict",
                                    new JSONObject()
                                            .put("names", names)
                                            .put("total", conflict.items.size())));
        } else if (resultKey != null)
            state.put(
                    "restore",
                    new JSONObject()
                            .put(
                                    "result",
                                    new JSONObject()
                                            .put("key", resultKey)
                                            .put("args", resultArgs)));
        return state;
    }

    /** False when the action closes the screen ("back"). */
    boolean onAction(String verb, String value) {
        switch (verb) {
            case "license":
                open(value);
                break;
            case "license-close":
                openFile = null;
                openText = null;
                break;
            case "open-url":
                try {
                    context.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(value)));
                } catch (ActivityNotFoundException none) {
                    ui.toast(value, "info");
                }
                break;
            case "share-log":
                DiagnosticsExport.share(context, info, ui);
                break;
            case "save-list":
                listSaves();
                break;
            case "save-close":
                saves = null;
                break;
            case "share-save":
                share(Collections.singletonList(value));
                break;
            case "share-all-saves":
                shareAll();
                break;
            case "restore-pick":
                pickRestore();
                break;
            case "restore-replace":
                answer(SaveRestore.Choice.REPLACE);
                break;
            case "restore-keep":
                answer(SaveRestore.Choice.KEEP_BOTH);
                break;
            case "restore-cancel":
                cancelRestore();
                break;
            case "restore-close":
                resultKey = null;
                resultArgs = null;
                break;
            case "back":
                openFile = null;
                openText = null;
                saves = null;
                return false;
            default:
                Log.e(TAG, "Unknown about action " + verb + " " + value);
        }
        return true;
    }

    private void listSaves() {
        saves = null;
        try {
            saves = SaveExport.list(SaveArchive.root(context.getFilesDir()));
        } catch (IOException error) {
            Log.e(TAG, "Cannot list the saves", error);
            ui.toast(error.toString(), "danger");
        }
    }

    /** «Отправить все сохранения»: every save the picker lists, in one archive of the same format. */
    private void shareAll() {
        List<String> keys = new ArrayList<>();
        if (saves != null) for (SaveExport.Save save : saves) keys.add(save.key);
        if (keys.isEmpty()) {
            Log.e(TAG, "share-all-saves without listed saves");
            return;
        }
        share(keys);
    }

    private void share(final List<String> keys) {
        final File root = SaveArchive.root(context.getFilesDir());
        ShareSheet.offer(
                context,
                ui,
                "Save export " + (keys.size() == 1 ? keys.get(0) : keys.size() + " saves"),
                "application/zip",
                () ->
                        SaveExport.zip(
                                root,
                                keys,
                                ShareSheet.directory(context, ShareSheet.SAVES),
                                new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT)
                                        .format(new Date())));
    }

    /** «Восстановить сохранение»: one file from the system picker (the launcher's game import uses the same picker). */
    private void pickRestore() {
        resultKey = null;
        resultArgs = null;
        if (restoring || conflict != null) {
            Log.i(TAG, "restore-pick while a restore is open: ignored");
            return;
        }
        try {
            context.startActivityForResult(GameLauncherActivity.openDocument(false), RESTORE);
        } catch (ActivityNotFoundException unavailable) {
            Log.e(TAG, "No document picker for the restore", unavailable);
            result("picker_unavailable");
        }
    }

    /** The picked file: copied into the cache, read and checked off the main thread; written at once without a conflict. */
    void restorePicked(final Uri uri) {
        if (restoring || conflict != null) {
            Log.e(TAG, "A second restore file while one is open: " + uri);
            return;
        }
        restoring = true;
        final File staging = new File(context.getCacheDir(), "save-restore"),
                files = context.getFilesDir();
        new Thread(
                        () -> {
                            try {
                                clear(staging);
                                String[] described = describe(uri);
                                File picked = new File(staging, "picked");
                                copy(
                                        uri,
                                        picked,
                                        described[1] == null ? -1 : Long.parseLong(described[1]));
                                SaveRestore.Plan plan =
                                        SaveRestore.inspect(
                                                picked,
                                                described[0],
                                                SaveArchive.root(files),
                                                new File(files, "storm/options"),
                                                staging);
                                Log.i(
                                        TAG,
                                        "Restore "
                                                + described[0]
                                                + ": "
                                                + plan.items.size()
                                                + " saves, "
                                                + plan.conflicts().size()
                                                + " already there");
                                if (!plan.conflicts().isEmpty()) {
                                    main.post(
                                            () -> {
                                                restoring = false;
                                                conflict = plan;
                                                changed.run();
                                            });
                                    return;
                                }
                                finish(plan, null, staging);
                            } catch (IOException | RuntimeException error) {
                                failed(error, staging);
                            }
                        },
                        "StormSaveRestore")
                .start();
        changed.run();
    }

    private void answer(final SaveRestore.Choice choice) {
        final SaveRestore.Plan plan = conflict;
        if (plan == null || restoring) {
            Log.e(TAG, "Restore answer " + choice + " without a question");
            return;
        }
        conflict = null;
        restoring = true;
        Log.i(
                TAG,
                "Restore: the player chose "
                        + choice
                        + " for "
                        + plan.conflicts().size()
                        + " saves already there");
        final File staging = new File(context.getCacheDir(), "save-restore");
        new Thread(
                        () -> {
                            try {
                                finish(plan, choice, staging);
                            } catch (IOException | RuntimeException error) {
                                failed(error, staging);
                            }
                        },
                        "StormSaveRestore")
                .start();
    }

    private void cancelRestore() {
        if (conflict == null) {
            Log.e(TAG, "restore-cancel without a question");
            return;
        }
        Log.i(TAG, "Restore cancelled by the player: nothing written");
        conflict = null;
        final File staging = new File(context.getCacheDir(), "save-restore");
        new Thread(
                        () -> {
                            try {
                                clear(staging);
                            } catch (IOException error) {
                                Log.e(TAG, "Cannot clear " + staging, error);
                            }
                        },
                        "StormSaveRestore")
                .start();
    }

    /** Worker thread: writes the plan and publishes the result. */
    private void finish(SaveRestore.Plan plan, SaveRestore.Choice choice, File staging)
            throws IOException {
        List<String> keys =
                SaveRestore.commit(
                        plan,
                        SaveArchive.root(context.getFilesDir()),
                        choice,
                        AboutScreen::syncDirectory);
        Log.i(TAG, "Restored " + keys);
        clear(staging);
        String first = keys.get(0).substring(keys.get(0).indexOf('/') + 1);
        main.post(
                () -> {
                    restoring = false;
                    if (keys.size() == 1) result("done", first);
                    else result("done_many", keys.size());
                    changed.run();
                });
    }

    /** Worker thread: a refusal shows its reason, anything else the general failure; both logged with the cause. */
    private void failed(Exception error, File staging) {
        Log.e(TAG, "Restore failed", error);
        try {
            clear(staging);
        } catch (IOException cleanup) {
            Log.e(TAG, "Cannot clear " + staging, cleanup);
        }
        main.post(
                () -> {
                    restoring = false;
                    SaveArchive.Refused refused =
                            error instanceof SaveArchive.Refused
                                    ? (SaveArchive.Refused) error
                                    : error.getCause() instanceof SaveArchive.Refused
                                            ? (SaveArchive.Refused) error.getCause()
                                            : null;
                    if (error instanceof SaveRestore.Stopped) {
                        SaveRestore.Stopped stopped = (SaveRestore.Stopped) error;
                        result("partial", stopped.restored.size(), stopped.total);
                    } else if (refused != null
                            && refused.reason.equals(SaveArchive.Refused.NO_SPACE))
                        result(refused.reason, Math.max(1, (refused.bytes + (1 << 20) - 1) >> 20));
                    else if (refused != null) result(refused.reason);
                    else result("failed");
                    changed.run();
                });
    }

    private void result(String key, Object... args) {
        resultKey = key;
        resultArgs = new JSONArray();
        for (Object arg : args) resultArgs.put(arg);
    }

    /** {display name, size or null} of the picked document. */
    private String[] describe(Uri uri) throws IOException {
        try (Cursor row =
                context.getContentResolver()
                        .query(
                                uri,
                                new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                                null,
                                null,
                                null)) {
            if (row == null || !row.moveToFirst())
                throw new IOException("The picked document has no name: " + uri);
            String name = row.isNull(0) ? null : row.getString(0),
                    size = row.isNull(1) ? null : String.valueOf(row.getLong(1));
            if (name == null) throw new IOException("The picked document has no name: " + uri);
            return new String[] {name, size};
        }
    }

    private void copy(Uri uri, File picked, long size) throws IOException {
        if (size > SaveArchive.MAX_SAVE_BYTES * 4)
            throw new SaveArchive.Refused(
                    SaveArchive.Refused.NOT_SAVE, uri + ": " + size + " bytes");
        if (size >= 0) SaveRestore.requireSpace(picked.getParentFile(), size);
        long copied = 0;
        try (InputStream in = context.getContentResolver().openInputStream(uri);
                FileOutputStream out = new FileOutputStream(picked)) {
            if (in == null) throw new IOException("Cannot open " + uri);
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) != -1; ) {
                copied += n;
                if (copied > SaveArchive.MAX_SAVE_BYTES * 4)
                    throw new SaveArchive.Refused(
                            SaveArchive.Refused.NOT_SAVE, uri + ": over " + copied + " bytes");
                out.write(buffer, 0, n);
            }
        } catch (SaveArchive.Refused refused) {
            throw refused;
        } catch (IOException error) {
            throw SaveRestore.spaceOr(picked.getParentFile(), size < 0 ? 1 : size - copied, error);
        }
    }

    /** The staging folder, emptied (created when missing): only this restore's files live there. */
    private static void clear(File staging) throws IOException {
        if (!staging.isDirectory() && !staging.mkdirs())
            throw new IOException("Cannot create " + staging);
        File[] old = staging.listFiles();
        if (old == null) throw new IOException("Cannot list " + staging);
        for (File file : old) if (!file.delete()) throw new IOException("Cannot delete " + file);
    }

    /**
     * save_file.hpp syncDirectory: makes the rename durable. Like the engine, a filesystem that refuses a directory
     * fsync does not fail a restore whose file is already synced; it is logged.
     */
    private static void syncDirectory(File directory) {
        try {
            FileDescriptor fd = Os.open(directory.getPath(), OsConstants.O_RDONLY, 0);
            try {
                Os.fsync(fd);
            } finally {
                Os.close(fd);
            }
        } catch (ErrnoException refused) {
            Log.w(
                    TAG,
                    "The folder "
                            + directory
                            + " cannot be synced; the restored save itself is: "
                            + refused);
        }
    }

    /** Large notices (single-header sources) keep their licence at the end of the file. */
    private void open(String file) {
        try {
            byte[] data = read("licenses/" + file);
            openTruncated = data.length > TEXT_LIMIT;
            openText =
                    openTruncated
                            ? new String(data, data.length - TAIL, TAIL, StandardCharsets.UTF_8)
                            : new String(data, StandardCharsets.UTF_8);
            openFile = file;
        } catch (IOException error) {
            Log.e(TAG, "Cannot read notice " + file, error);
            ui.toast(error.toString(), "danger");
        }
    }

    private byte[] read(String asset) throws IOException {
        try (InputStream in = context.getAssets().open(asset)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) != -1; ) out.write(buffer, 0, n);
            return out.toByteArray();
        }
    }
}
