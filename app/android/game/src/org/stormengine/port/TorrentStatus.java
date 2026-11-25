package org.stormengine.port;

import android.util.Log;

import org.json.JSONObject;

import java.io.IOException;
import java.util.HashSet;

/**
 * The download service's step from the torrent client's status (TorrentDownloadService's poll, once a second) to the
 * set's record: the file list kept once per session, and the offline, network-error and stall episodes logged once
 * each. Also the selection gate and the error kind the player sees.
 */
final class TorrentStatus {
    private static final String TAG = "StormDownloads";

    /** The session's file list (NativeTorrents.metadata). */
    interface FileList {
        String fetch() throws Exception;
    }

    /** The open session's metadata was fetched and kept (once per session: a resume refreshes it). */
    boolean metadataKept;

    boolean stallLogged;
    private String lastNetworkError = "";
    private boolean lastOffline;

    /** A session opens. */
    void reset() {
        stallLogged = false;
        metadataKept = false;
        lastNetworkError = "";
        lastOffline = false;
    }

    /** One poll: the client's status into the set's record (the caller saves it). */
    void apply(
            GameDownloads catalog,
            GameDownloads.Entry entry,
            JSONObject status,
            boolean online,
            FileList fileList)
            throws Exception {
        entry.state = status.getString("state");
        if ("complete".equals(entry.state) && status.optBoolean("checkpointPending"))
            entry.state = "pausing";
        entry.downloaded = status.optLong("downloadedBytes");
        entry.wanted = status.optLong("wantedBytes");
        entry.rate = status.optLong("bytesPerSecond");
        entry.peers = Math.max(0, status.optLong("peers"));
        entry.seeds = Math.max(0, status.optLong("seeds"));
        entry.stalledSeconds = Math.max(0, status.optLong("stalledSeconds"));
        entry.networkError = status.optString("networkError");
        entry.checkedPermille =
                Math.round(Math.max(0, Math.min(1, status.optDouble("checkProgress", 0))) * 1000);
        entry.offline = !online;
        // Logged once per episode, as the network errors.
        if (entry.offline != lastOffline)
            Log.i(
                    TAG,
                    "Download "
                            + entry.id
                            + (entry.offline
                                    ? ": the phone has no network"
                                    : ": the network is back"));
        lastOffline = entry.offline;
        if (!metadataKept && status.optBoolean("metadataReady")) {
            keepMetadata(catalog, entry, fileList.fetch());
            metadataKept = true;
        }
        // Logged once per episode; the row shows it for as long as it lasts.
        if (!entry.networkError.isEmpty() && !entry.networkError.equals(lastNetworkError))
            Log.w(TAG, "Download " + entry.id + ": " + entry.networkError);
        lastNetworkError = entry.networkError;
        boolean stalled = entry.stalledSeconds >= DownloadLibraryState.STALL_SECONDS;
        if (stalled && !stallLogged)
            Log.w(
                    TAG,
                    "Download "
                            + entry.id
                            + " stalled in "
                            + entry.state
                            + ": nothing for "
                            + entry.stalledSeconds
                            + " s, "
                            + entry.peers
                            + " peers"
                            + (entry.networkError.isEmpty() ? "" : ", " + entry.networkError));
        else if (!stalled && stallLogged) Log.i(TAG, "Download " + entry.id + " moving again");
        stallLogged = stalled;
    }

    /** Keeps the set's file list (written only when it changed) and takes the torrent's name from it. */
    static void keepMetadata(GameDownloads catalog, GameDownloads.Entry entry, String json)
            throws Exception {
        String name = DownloadLibraryState.parse(json).name;
        catalog.saveMetadata(entry, json);
        if (!name.isEmpty()) entry.name = name;
    }

    /** Any explicitly chosen selectable file of version-2 metadata; nothing is inferred from names. */
    static void prepareSelection(GameDownloads catalog, GameDownloads.Entry entry, int[] selection)
            throws Exception {
        if ("archive".equals(entry.kind)
                || selection == null
                || selection.length == 0
                || selection.length > 100000)
            throw new IOException("Choose the torrent files to download first");
        DownloadLibraryState.Metadata metadata;
        try {
            if (!entry.hasMetadata) throw new IOException("No file list yet");
            metadata = DownloadLibraryState.parse(catalog.metadata(entry));
        } catch (IOException earlier) {
            Log.w(TAG, "No usable file list for " + entry.id + ": " + earlier.getMessage());
            throw new DownloadProblem(
                    "refresh-first", "Resume this set to refresh its file list first");
        }
        HashSet<Integer> unique = new HashSet<>();
        for (int index : selection) {
            if (index < 0 || index >= metadata.selectable.length || !unique.add(index))
                throw new IOException("Unknown or duplicate file selection");
            if (!metadata.selectable[index])
                throw new IOException("This torrent file cannot be selected");
            GameDownloads.relativePath(metadata.paths.get(index));
        }
        catalog.saveSelection(entry, selection);
        entry.phase = "payload";
        entry.error = "";
        entry.errorKind = "";
        entry.sourceState = "";
        entry.discovered = "";
    }

    /** Error kind for the player: a DownloadProblem's kind, else "failed" (the message goes to the log only). */
    static String errorKind(Throwable error) {
        // The torrent client reports what the player can act on (no space for the payload) as the
        // transfer's
        // errorKind, which becomes a DownloadProblem; anything it throws is a plain failure.
        return error instanceof DownloadProblem ? ((DownloadProblem) error).kind : "failed";
    }
}
