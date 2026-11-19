package org.stormengine.port;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Runs the import's own discovery ({@link ArchiveInstaller#discoverGame}) on finished
 * download sets in the :downloads process, one set at a time, and records the result on
 * the set ({@link GameDownloads.Entry#discovered}). The library says "game found" only
 * from this, never from the torrent's file names.
 */
final class SetDiscovery {
    private static final String TAG = "GameDownloads";
    private static final ExecutorService worker = Executors.newSingleThreadExecutor();
    private static final Set<String> running = new HashSet<>();

    private SetDiscovery() {}

    /** Queues the discovery of a ready set whose files were not discovered yet; once per set at a time. */
    static void request(Context context, String id) {
        final Context app = context.getApplicationContext();
        synchronized (running) {
            if (!running.add(id)) return;
        }
        worker.execute(
                () -> {
                    try {
                        GameDownloads catalog = TorrentDownloadService.catalog(app);
                        GameDownloads.Entry entry = catalog.read(id);
                        if (!GameDownloads.ready(entry) || !entry.discovered.isEmpty()) return;
                        long started = System.currentTimeMillis();
                        ArchiveInstaller.Discovery found =
                                ArchiveInstaller.discoverGame(
                                        catalog.importSource(id),
                                        new File(app.getCacheDir(), "set-discovery"));
                        String result = found.name().toLowerCase(Locale.ROOT);
                        catalog.recordDiscovery(id, result);
                        Log.i(
                                TAG,
                                "Set "
                                        + id
                                        + ": discovery found "
                                        + result
                                        + " in "
                                        + (System.currentTimeMillis() - started)
                                        + " ms");
                    } catch (Exception error) {
                        // No claim either way (and no retry loop): the import reports the cause
                        // itself.
                        Log.e(
                                TAG,
                                "Set " + id + ": discovery failed; recorded as undecided",
                                error);
                        try {
                            TorrentDownloadService.catalog(app).recordDiscovery(id, "undecided");
                        } catch (Exception nested) {
                            Log.e(TAG, "Set " + id + ": cannot record the discovery", nested);
                        }
                    } finally {
                        synchronized (running) {
                            running.remove(id);
                        }
                    }
                });
    }
}
