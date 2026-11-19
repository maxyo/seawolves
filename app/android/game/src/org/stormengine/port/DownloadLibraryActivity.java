package org.stormengine.port;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The download library of the github flavor (portable UI screen "downloads"): separate
 * retained source sets, independent of the selected game, worked on by
 * {@link TorrentDownloadService} in the :downloads process. The rows, their states and
 * verbs come from {@link DownloadLibraryState}; the questions (which candidate, details,
 * errors) are dialogs of the same screen. A finished set is imported by the launcher.
 */
public final class DownloadLibraryActivity extends UiScreenActivity {
    private static final int PICK = 73;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService reader = Executors.newSingleThreadExecutor();

    /** Owned by the reader thread (metadata snapshots); rows and entries are handed to the main thread. */
    private final DownloadLibraryState library =
            new DownloadLibraryState(e -> TorrentDownloadService.catalog(this).metadata(e));

    private List<DownloadLibraryState.Row> rows = new ArrayList<>();
    private final Map<String, GameDownloads.Entry> entries = new HashMap<>();
    private String pickerKind = "archive", fingerprint = "", readError = "", magnetDraft = "";
    private boolean visible, reading, destroyed;

    /** The library was read once: before that the screen says nothing, not even that it is empty. */
    private boolean loaded;

    /** The open question, as published; with what answering it needs. */
    private JSONObject dialog;

    private String dialogSet;

    /** File indices of each offered choice; the last one is every file. */
    private List<int[]> dialogChoices;

    @Override
    protected String screen() {
        return "downloads";
    }

    /**
     * The library from a notice: brings the open library forward instead of a second live
     * instance on top of it (one UI host per process, see {@link UiScreenActivity#onPause}).
     */
    static Intent reopen(Context context) {
        return new Intent(context, DownloadLibraryActivity.class)
                .addFlags(
                        Intent.FLAG_ACTIVITY_NEW_TASK
                                | Intent.FLAG_ACTIVITY_CLEAR_TOP
                                | Intent.FLAG_ACTIVITY_SINGLE_TOP);
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) pickerKind = state.getString("pickerKind", "archive");
    }

    @Override
    protected void onStart() {
        super.onStart();
        visible = true;
        main.post(refresh);
    }

    @Override
    protected void onStop() {
        visible = false;
        main.removeCallbacks(refresh);
        super.onStop();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        state.putString("pickerKind", pickerKind);
        super.onSaveInstanceState(state);
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        main.removeCallbacks(refresh);
        reader.shutdown();
        super.onDestroy();
    }

    private final Runnable refresh =
            new Runnable() {
                @Override
                public void run() {
                    if (!visible || destroyed) return;
                    if (!reading) {
                        reading = true;
                        reader.execute(
                                () -> {
                                    try {
                                        GameDownloads catalog =
                                                TorrentDownloadService.catalog(
                                                        DownloadLibraryActivity.this);
                                        if (!TorrentDownloadService.running)
                                            catalog.recoverInterrupted();
                                        List<GameDownloads.Entry> fresh = catalog.list();
                                        List<DownloadLibraryState.Row> next =
                                                library.rows(fresh, ProcessMarker.importing());
                                        for (DownloadLibraryState.Row r : next)
                                            if ("checking".equals(r.discovered))
                                                SetDiscovery.request(
                                                        DownloadLibraryActivity.this, r.id);
                                        main.post(
                                                () -> {
                                                    reading = false;
                                                    if (!destroyed) show(fresh, next, "");
                                                });
                                    } catch (Exception error) {
                                        Log.e(
                                                "StormDownloads",
                                                "Cannot read the download library",
                                                error);
                                        main.post(
                                                () -> {
                                                    reading = false;
                                                    if (!destroyed)
                                                        show(
                                                                null,
                                                                rows,
                                                                String.valueOf(error.getMessage()));
                                                });
                                    }
                                });
                    }
                    main.postDelayed(this, 1500);
                }
            };

    private void show(
            List<GameDownloads.Entry> fresh, List<DownloadLibraryState.Row> next, String error) {
        if (fresh != null) {
            entries.clear();
            for (GameDownloads.Entry e : fresh) entries.put(e.id, e);
        }
        StringBuilder print = new StringBuilder(error).append(loaded);
        loaded = true;
        for (DownloadLibraryState.Row r : next)
            print.append(r.id)
                    .append(r.state)
                    .append(r.downloaded)
                    .append(r.wanted)
                    .append(r.rate)
                    .append(r.error)
                    .append(r.stalled)
                    .append(r.main)
                    .append(r.menu)
                    .append(r.discovered)
                    .append(r.offline)
                    .append(r.checkedPermille);
        print.append(latestError());
        if (fingerprint.contentEquals(print)) return;
        fingerprint = print.toString();
        rows = next;
        readError = error;
        publish();
    }

    private String latestError() {
        return getSharedPreferences("download-errors", MODE_PRIVATE).getString("latest", "");
    }

    private String latestErrorKind() {
        return getSharedPreferences("download-errors", MODE_PRIVATE)
                .getString("latestKind", "failed");
    }

    @Override
    protected JSONObject screenState() throws JSONException {
        JSONArray sets = new JSONArray();
        for (DownloadLibraryState.Row r : rows) sets.put(set(r));
        String error = readError.isEmpty() ? latestError() : readError;
        String errorKind =
                error.isEmpty() ? "" : readError.isEmpty() ? latestErrorKind() : "failed";
        return new JSONObject()
                .put("loaded", loaded)
                .put("sets", sets)
                .put("error", error)
                .put("errorKind", errorKind)
                .put("magnetDraft", magnetDraft)
                .put("dialog", dialog == null ? JSONObject.NULL : dialog);
    }

    /** One set as DownloadLibraryState reports it (main verb inline, the rest under «⋯»). */
    private static JSONObject set(DownloadLibraryState.Row r) throws JSONException {
        return new JSONObject()
                .put("id", r.id)
                .put("kind", r.kind)
                .put("name", r.name)
                .put("state", r.state)
                .put("downloaded", r.downloaded)
                .put("wanted", r.wanted)
                .put("rate", r.rate)
                .put("peers", r.peers)
                .put("stalled", r.stalled)
                .put("offline", r.offline)
                .put("checked", r.checkedPermille / 1000.0)
                .put("networkError", r.networkError)
                .put("error", r.error)
                .put("errorKind", r.errorKind)
                .put("discovered", r.discovered)
                .put("main", r.main)
                .put("menu", new JSONArray(r.menu))
                .put("busy", "pausing".equals(r.state));
    }

    private GameDownloads.Entry entry(String id) {
        GameDownloads.Entry e = entries.get(id);
        if (e == null) throw new IllegalStateException("No download set " + id);
        return e;
    }

    @Override
    protected void onAction(String verb, String value) {
        try {
            switch (verb) {
                case "back":
                    finish();
                    return;
                case "add-torrent":
                    pick("torrent");
                    break;
                case "add-archive":
                    pick("archive");
                    break;
                case "magnet":
                    addMagnet();
                    break;
                case "import":
                    importSet(value);
                    break;
                case "pause":
                    command(TorrentDownloadService.PAUSE, value, null);
                    break;
                case "resume":
                    command(TorrentDownloadService.RESUME, value, null);
                    break;
                case "delete":
                    command(TorrentDownloadService.DELETE, value, null);
                    break;
                case "details":
                    details(entry(value));
                    break;
                case "choose":
                    choose(entry(value));
                    break;
                case "download":
                    download(value);
                    break;
                case "dialog-close":
                    closeDialog();
                    break;
                default:
                    Log.e("StormDownloads", "Unknown downloads action " + verb + " " + value);
            }
        } catch (Exception error) {
            error(error);
        }
        publish();
    }

    @Override
    protected void onEdited(String field, String text) {
        if (!field.equals("MAGNET")) {
            super.onEdited(field, text);
            return;
        }
        magnetDraft = text.trim();
        publish();
    }

    private void closeDialog() {
        dialog = null;
        dialogSet = null;
        dialogChoices = null;
    }

    private void error(Exception error) {
        Log.e("StormDownloads", "Download library action failed", error);
        closeDialog();
        try {
            dialog =
                    new JSONObject()
                            .put("kind", "error")
                            .put("text", String.valueOf(error.getMessage()))
                            .put("errorKind", TorrentDownloadService.errorKind(error));
        } catch (JSONException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    private void addMagnet() {
        if (magnetDraft.isEmpty()) return;
        clearError();
        askNotifications();
        TorrentDownloadService.addMagnet(this, magnetDraft);
        magnetDraft = "";
    }

    /** The launcher's ImportCoordinator imports the set's files in place. */
    private void importSet(String id) throws IOException {
        if (GameActivity.isEngineAlive())
            throw new DownloadProblem("exit-game-first", "The game is running");
        TorrentDownloadService.importSet(this, id);
        finish();
    }

    /** First files of the set and its totals (the metadata snapshot is read on the reader thread). */
    private void details(GameDownloads.Entry e) {
        reader.execute(
                () -> {
                    try {
                        DownloadLibraryState.Metadata metadata = library.metadata(e);
                        JSONArray shown = new JSONArray();
                        for (int i = 0; i < Math.min(20, metadata.paths.size()); i++)
                            shown.put(metadata.paths.get(i));
                        JSONObject next =
                                new JSONObject()
                                        .put("kind", "details")
                                        .put("id", e.id)
                                        .put("title", e.name)
                                        .put("files", shown)
                                        .put("count", metadata.paths.size())
                                        .put("bytes", metadata.totalBytes);
                        main.post(
                                () -> {
                                    if (!destroyed) {
                                        closeDialog();
                                        dialog = next;
                                        publish();
                                    }
                                });
                    } catch (Exception error) {
                        main.post(
                                () -> {
                                    if (!destroyed) {
                                        error(error);
                                        publish();
                                    }
                                });
                    }
                });
    }

    /** Which files to download: one candidate of the metadata (with its volume hints), or every file. */
    private void choose(GameDownloads.Entry e) {
        reader.execute(
                () -> {
                    try {
                        DownloadLibraryState.Metadata metadata = library.metadata(e);
                        List<int[]> choices = new ArrayList<>();
                        JSONArray offered = new JSONArray();
                        for (DownloadLibraryState.Candidate c : metadata.candidates) {
                            choices.add(c.files);
                            offered.put(
                                    new JSONObject()
                                            .put("label", c.root.isEmpty() ? metadata.name : c.root)
                                            .put("kind", c.kind)
                                            .put("files", c.files.length)
                                            .put("bytes", c.bytes));
                        }
                        int[] all = library.allFiles(e);
                        choices.add(all);
                        JSONObject next =
                                new JSONObject()
                                        .put("kind", "candidates")
                                        .put("id", e.id)
                                        .put("candidates", offered)
                                        .put(
                                                "all",
                                                new JSONObject()
                                                        .put("files", all.length)
                                                        .put("bytes", metadata.totalBytes));
                        main.post(
                                () -> {
                                    if (!destroyed) {
                                        closeDialog();
                                        dialogSet = e.id;
                                        dialogChoices = choices;
                                        dialog = next;
                                        publish();
                                    }
                                });
                    } catch (Exception error) {
                        main.post(
                                () -> {
                                    if (!destroyed) {
                                        error(error);
                                        publish();
                                    }
                                });
                    }
                });
    }

    /** "<set>:<choice>": a candidate number, or -1 for every file. */
    private void download(String value) {
        int colon = value.indexOf(':');
        String id = value.substring(0, colon);
        if (!id.equals(dialogSet) || dialogChoices == null)
            throw new IllegalStateException("The file choice is no longer open");
        int choice = Integer.parseInt(value.substring(colon + 1));
        int[] selection = dialogChoices.get(choice == -1 ? dialogChoices.size() - 1 : choice);
        closeDialog();
        command(TorrentDownloadService.START, id, selection);
    }

    private void pick(String kind) {
        pickerKind = kind;
        Intent intent =
                new Intent(Intent.ACTION_OPEN_DOCUMENT)
                        .addCategory(Intent.CATEGORY_OPENABLE)
                        .setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, "archive".equals(kind));
        intent.addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, PICK);
        } catch (ActivityNotFoundException error) {
            error(error);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null) return;
        ArrayList<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null)
            for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                Uri uri = data.getClipData().getItemAt(i).getUri();
                if (!uris.contains(uri)) uris.add(uri);
            }
        if (data.getData() != null && !uris.contains(data.getData())) uris.add(data.getData());
        for (Uri uri : uris)
            try {
                getContentResolver()
                        .takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException unavailable) {
                /* Some providers grant only the current process. Copy failure remains visible. */
            }
        if (!uris.isEmpty())
            try {
                clearError();
                askNotifications();
                TorrentDownloadService.addLocal(this, uris, pickerKind);
            } catch (RuntimeException error) {
                error(error);
            }
        publish();
    }

    private void clearError() {
        getSharedPreferences("download-errors", MODE_PRIVATE)
                .edit()
                .remove("latest")
                .remove("latestKind")
                .apply();
    }

    private void command(String action, String id, int[] selection) {
        clearError();
        if (!TorrentDownloadService.DELETE.equals(action)
                && !TorrentDownloadService.PAUSE.equals(action)) askNotifications();
        TorrentDownloadService.command(this, action, id, selection);
    }

    private void askNotifications() {
        if (Build.VERSION.SDK_INT >= 33
                && !getSharedPreferences("download-errors", MODE_PRIVATE)
                        .getBoolean("notificationAsked", false)
                && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                        != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            getSharedPreferences("download-errors", MODE_PRIVATE)
                    .edit()
                    .putBoolean("notificationAsked", true)
                    .apply();
            requestPermissions(new String[] {android.Manifest.permission.POST_NOTIFICATIONS}, 74);
        }
    }
}
