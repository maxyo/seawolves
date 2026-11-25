package org.stormengine.port;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.util.Log;

import org.json.*;

import java.io.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Owns one native session on one worker in the :downloads process. Sets stay separate and
 * never become the active game here: a finished set is imported by the launcher's
 * ImportCoordinator ({@link #importSet}), which reads its files in place. Screen state comes
 * from {@link DownloadLibraryState}.
 */
public final class TorrentDownloadService extends Service {
    static volatile boolean running;
    static final String ADD_LOCAL = "local",
            ADD_MAGNET = "magnet",
            RESUME = "resume",
            START = "start",
            PAUSE = "pause",
            DELETE = "delete";
    private static final String TAG = "StormDownloads";
    private static final String CHANNEL = "game-downloads";
    private static final int NOTIFICATION = 7301, FINISHED_NOTIFICATION = 7302;
    private HandlerThread thread;
    private Handler worker;
    private GameDownloads catalog;
    private long session;
    private volatile String activeId = "";
    private volatile boolean cancelCopy;
    private final AtomicInteger commands = new AtomicInteger();
    private final Object lifecycle = new Object();
    private volatile int lastStart;
    private boolean pendingStart;
    private final TorrentStatus torrentStatus = new TorrentStatus();
    private long lastSave;
    private PowerManager.WakeLock wakeLock;

    /** The library lives beside the game, on its volume: a finished game moves into place instead of being copied. */
    static GameDownloads catalog(Context context) throws IOException {
        return new GameDownloads(
                new File(GameStorage.imported(context).getParentFile(), "game-downloads"));
    }

    static void command(Context context, String action, String id, int[] selection) {
        Intent intent =
                new Intent(context, TorrentDownloadService.class)
                        .setAction(action)
                        .putExtra("id", id);
        if (selection != null) intent.putExtra("selection", selection);
        start(context, intent);
    }

    static void addLocal(Context context, ArrayList<Uri> uris, String kind) {
        Intent intent =
                new Intent(context, TorrentDownloadService.class)
                        .setAction(ADD_LOCAL)
                        .putParcelableArrayListExtra("uris", uris)
                        .putExtra("kind", kind);
        start(context, intent);
    }

    static void addMagnet(Context context, String magnet) {
        Intent intent =
                new Intent(context, TorrentDownloadService.class)
                        .setAction(ADD_MAGNET)
                        .putExtra("source", magnet);
        start(context, intent);
    }

    /**
     * Offers a finished set to the launcher (main process), whose ImportCoordinator imports
     * it with startTree on the set's files. The launcher re-validates the set itself.
     */
    static void importSet(Context context, String id) {
        context.startActivity(
                new Intent(context, GameLauncherActivity.class)
                        .putExtra(GameLauncherActivity.EXTRA_IMPORT_DOWNLOAD, id)
                        .addFlags(
                                Intent.FLAG_ACTIVITY_NEW_TASK
                                        | Intent.FLAG_ACTIVITY_CLEAR_TOP
                                        | Intent.FLAG_ACTIVITY_SINGLE_TOP));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        running = true;
        thread = new HandlerThread("game-download-library");
        thread.start();
        worker = new Handler(thread.getLooper());
        worker.post(
                () -> {
                    try {
                        catalog = catalog(this);
                        catalog.recoverInterrupted();
                    } catch (Exception error) {
                        Log.e(TAG, "Cannot open persistent library", error);
                    }
                });
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        synchronized (lifecycle) {
            commands.incrementAndGet();
            lastStart = startId;
        }
        try {
            Notification notification = notification(DownloadWords.text(this, "working"), -1);
            if (Build.VERSION.SDK_INT >= 29)
                startForeground(
                        NOTIFICATION, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else startForeground(NOTIFICATION, notification);
            if (wakeLock == null)
                wakeLock =
                        getSystemService(PowerManager.class)
                                .newWakeLock(
                                        PowerManager.PARTIAL_WAKE_LOCK,
                                        "seawolves:download-library");
            if (!wakeLock.isHeld()) wakeLock.acquire(6L * 60 * 60 * 1000);
        } catch (RuntimeException error) {
            commands.decrementAndGet();
            Log.e(TAG, "Foreground download cannot start", error);
            latestError(error);
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        if (intent == null) {
            commands.decrementAndGet();
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        if (PAUSE.equals(intent.getAction()) && activeId.equals(intent.getStringExtra("id")))
            cancelCopy = true;
        if (DELETE.equals(intent.getAction()) && activeId.equals(intent.getStringExtra("id")))
            intent.putExtra("blockedWhileBusy", true);
        worker.post(
                () -> {
                    String id = intent.getStringExtra("id");
                    try {
                        if (catalog == null)
                            throw new IOException("Download library is unavailable");
                        handle(intent);
                    } catch (Exception error) {
                        Log.e(TAG, "Download command failed", error);
                        if (id != null && !id.isEmpty()) failure(id, error);
                        else {
                            // Creation failures have no set to attach a status to.
                            latestError(error);
                        }
                    } finally {
                        commands.decrementAndGet();
                        maybeStop();
                    }
                });
        return START_NOT_STICKY; // A killed service never silently approves payload again.
    }

    private void handle(Intent intent) throws Exception {
        String action = intent.getAction(), id = intent.getStringExtra("id");
        if (ADD_MAGNET.equals(action)) {
            ArrayList<String> empty = new ArrayList<>();
            GameDownloads.Entry entry =
                    catalog.create(
                            "magnet",
                            DownloadWords.text(this, "magnet_set"),
                            intent.getStringExtra("source"),
                            empty,
                            empty);
            try {
                open(entry, false);
            } catch (Exception error) {
                failure(entry.id, error);
                throw error;
            }
            return;
        }
        if (ADD_LOCAL.equals(action)) {
            ArrayList<Uri> uris = intent.getParcelableArrayListExtra("uris");
            if (uris == null || uris.isEmpty() || uris.size() > 1000)
                throw new IOException("Choose source documents first");
            String kind = intent.getStringExtra("kind");
            if (!"torrent".equals(kind) && !"archive".equals(kind))
                throw new IOException("Unknown document type");
            if ("torrent".equals(kind) && uris.size() != 1)
                throw new IOException("Choose one torrent at a time");
            ArrayList<String> names = new ArrayList<>(), sources = new ArrayList<>();
            for (Uri uri : uris) {
                names.add(documentName(uri));
                sources.add(uri.toString());
            }
            GameDownloads.Entry entry =
                    catalog.create(kind, names.get(0), names.get(0), names, sources);
            try {
                copyDocuments(entry);
                inspect(entry);
            } catch (Exception error) {
                failure(entry.id, error);
                throw error;
            }
            return;
        }
        // A game import may be reading a finished set in place: change no files meanwhile.
        if (!PAUSE.equals(action) && ResourceImporter.importing(GameStorage.imported(this)))
            throw new DownloadProblem("import-running", "A game import is running");
        if (DELETE.equals(action)) {
            if (intent.getBooleanExtra("blockedWhileBusy", false))
                throw new DownloadProblem(
                        "pause-first", "The set is busy: pause it before deleting");
            if (id.equals(activeId)) closeSession("paused");
            GameDownloads.Entry removed = null;
            try {
                removed = catalog.read(id);
            } catch (IOException damaged) {
                // Damaged records must still be removable; their document grants stay with the app.
                Log.w(TAG, "Set " + id + ": removing a damaged record", damaged);
            }
            catalog.remove(id);
            if (removed != null)
                for (String source : removed.documents) {
                    boolean shared = false;
                    for (GameDownloads.Entry other : catalog.list())
                        if (other.documents.contains(source)) {
                            shared = true;
                            break;
                        }
                    if (!shared)
                        try {
                            getContentResolver()
                                    .releasePersistableUriPermission(
                                            Uri.parse(source),
                                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        } catch (SecurityException notPersisted) {
                            /* Temporary-only grants need no release. */
                        }
                }
            return;
        }
        GameDownloads.Entry entry = catalog.read(id);
        if (PAUSE.equals(action)) {
            if (id.equals(activeId)) closeSession("paused");
            else if (!GameDownloads.ready(entry) && !"error".equals(entry.state)) {
                entry.state = "paused";
                entry.rate = 0;
                entry.stalledSeconds = 0;
                catalog.save(entry);
            }
            return;
        }
        if (RESUME.equals(action)) {
            // Also the retry of an error or interruption. A saved selection continues:
            // native rehashes the files on disk, then downloads only what is missing.
            Log.i(
                    TAG,
                    "Resume "
                            + entry.id
                            + " from "
                            + entry.state
                            + (entry.selectedCount > 0
                                    ? " with " + entry.selectedCount + " selected files"
                                    : ""));
            if (!"magnet".equals(entry.kind) && !allInputsPresent(entry)) {
                copyDocuments(entry);
                inspect(entry);
            } else if ("archive".equals(entry.kind)) inspect(entry);
            else open(entry, "payload".equals(entry.phase) && entry.selectedCount > 0);
            return;
        }
        if (START.equals(action)) {
            int[] selection = intent.getIntArrayExtra("selection");
            TorrentStatus.prepareSelection(catalog, entry, selection);
            catalog.save(entry);
            open(entry, true);
            return;
        }
        throw new IOException("Unknown library action");
    }

    private String documentName(Uri uri) throws IOException {
        if (!"content".equals(uri.getScheme()))
            throw new IOException("Select documents through Android's file picker");
        String name = null;
        try (Cursor cursor =
                getContentResolver()
                        .query(
                                uri,
                                new String[] {OpenableColumns.DISPLAY_NAME},
                                null,
                                null,
                                null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        }
        GameDownloads.safeName(name);
        return name;
    }

    private boolean allInputsPresent(GameDownloads.Entry entry) throws IOException {
        for (String name : entry.names) if (!catalog.input(entry, name).isFile()) return false;
        return !entry.names.isEmpty();
    }

    private void copyDocuments(GameDownloads.Entry entry) throws Exception {
        if (session != 0) closeSession("paused");
        activeId = entry.id;
        cancelCopy = false;
        entry.sourceState = "";
        entry.discovered = "";
        entry.state = "copying";
        entry.error = "";
        entry.downloaded = 0;
        catalog.save(entry);
        long limit = "torrent".equals(entry.kind) ? 16L << 20 : 32L << 30;
        long[] total = {0};
        try {
            for (int i = 0; i < entry.names.size(); i++) {
                File target = catalog.input(entry, entry.names.get(i));
                // finished before an interruption; copies are written atomically
                if (target.isFile()) {
                    if (target.length() > limit - total[0])
                        throw new DownloadProblem(
                                "too-large", "Saved inputs exceed the package size limit");
                    total[0] += target.length();
                    continue;
                }
                File partial = catalog.copyTemporary(entry);
                try (InputStream input =
                                getContentResolver()
                                        .openInputStream(Uri.parse(entry.documents.get(i)));
                        FileOutputStream output = new FileOutputStream(partial)) {
                    if (input == null)
                        throw new DownloadProblem("open-failed", "Cannot open selected document");
                    long[] saved = {SystemClock.elapsedRealtime()};
                    Streams.copy(
                            input,
                            output,
                            limit - total[0],
                            n -> {
                                if (cancelCopy)
                                    throw new InterruptedIOException(
                                            "Copy paused; resume to copy the selected documents again");
                                if (partial.getParentFile().getUsableSpace() < n + (64L << 20))
                                    throw new DownloadProblem(
                                            "storage-full",
                                            "Not enough free storage to copy the selected documents");
                                total[0] += n;
                                if (SystemClock.elapsedRealtime() - saved[0] > 1500) {
                                    saved[0] = SystemClock.elapsedRealtime();
                                    entry.downloaded = total[0];
                                    catalog.save(entry);
                                    updateNotification(entry);
                                }
                            });
                    output.getFD().sync();
                } catch (Exception error) {
                    if (partial.exists() && !partial.delete())
                        Log.w(TAG, "Cannot remove incomplete source copy");
                    if (error instanceof Streams.TooLarge)
                        throw new DownloadProblem(
                                "too-large", "Selected documents exceed the package size limit");
                    throw error;
                }
                if (!partial.renameTo(target))
                    throw new IOException("Cannot finish private source copy");
            }
            entry.downloaded = total[0];
            entry.state = "saved";
            catalog.save(entry);
        } finally {
            activeId = "";
        }
    }

    private void inspect(GameDownloads.Entry entry) throws Exception {
        entry.state = "inspecting";
        entry.error = "";
        catalog.save(entry);
        if ("torrent".equals(entry.kind)) {
            TorrentStatus.keepMetadata(
                    catalog, entry, NativeTorrents.inspect(catalog.nativeSource(entry)));
            entry.downloaded = 0;
            entry.wanted = 0;
            entry.state = "awaiting-selection";
        } else {
            // Local documents are complete once copied; the import discovers what they hold.
            entry.state = "complete";
            entry.sourceState = "complete";
        }
        entry.errorKind = "";
        catalog.save(entry);
    }

    private void open(GameDownloads.Entry entry, boolean start) throws Exception {
        if (session != 0 && !entry.id.equals(activeId)) closeSession("paused");
        if (session == 0) {
            session =
                    NativeTorrents.open(
                            catalog.nativeSource(entry),
                            catalog.sessionDirectory(entry).getAbsolutePath());
            activeId = entry.id;
        }
        pendingStart = start;
        torrentStatus.reset();
        if (start) {
            entry.sourceState = "";
            entry.discovered = "";
        }
        entry.state = "metadata";
        entry.error = "";
        entry.errorKind = "";
        entry.networkError = "";
        entry.stalledSeconds = 0;
        catalog.save(entry);
        worker.removeCallbacks(poll);
        worker.post(poll);
    }

    private final Runnable poll =
            new Runnable() {
                @Override
                public void run() {
                    if (session == 0) return;
                    String id = activeId;
                    try {
                        GameDownloads.Entry entry = catalog.read(id);
                        JSONObject status = new JSONObject(NativeTorrents.poll(session));
                        torrentStatus.apply(
                                catalog,
                                entry,
                                status,
                                online(),
                                () -> NativeTorrents.metadata(session));
                        if (pendingStart && torrentStatus.metadataKept) {
                            checkpointBeforeStart(entry);
                            NativeTorrents.start(session, catalog.selection(entry));
                            pendingStart = false;
                            entry.state = "downloading";
                            catalog.save(entry);
                        } else if ("complete".equals(status.optString("state"))
                                && !status.optBoolean("checkpointPending")) {
                            entry.sourceState = "complete";
                            entry.rate = 0;
                            entry.stalledSeconds = 0;
                            catalog.save(entry);
                            closeSession("complete");
                            Log.i(
                                    TAG,
                                    "Download "
                                            + id
                                            + " complete ("
                                            + entry.downloaded
                                            + " bytes); ready to import");
                            SetDiscovery.request(TorrentDownloadService.this, id);
                            finished(catalog.read(id));
                            maybeStop();
                            return;
                        } else if ("error".equals(status.optString("state"))) {
                            String kind = status.optString("errorKind");
                            throw new DownloadProblem(
                                    kind.isEmpty() ? "transfer" : kind,
                                    status.optString("error", "Torrent transfer failed"));
                        } else if (!pendingStart
                                && "awaiting-selection".equals(status.optString("state"))) {
                            catalog.save(entry);
                            closeSession("awaiting-selection");
                            maybeStop();
                            return;
                        }
                        if (SystemClock.elapsedRealtime() - lastSave > 1500) {
                            catalog.save(entry);
                            lastSave = SystemClock.elapsedRealtime();
                        }
                        updateNotification(entry);
                        worker.postDelayed(this, 1000);
                    } catch (Exception error) {
                        try {
                            closeSession("error");
                        } catch (Exception closeError) {
                            Log.e(TAG, "Cannot checkpoint failed session", closeError);
                        }
                        failure(id, error);
                        maybeStop();
                    }
                }
            };

    /** An active network that reaches the internet; without one a stall is the phone's, not the peers'. */
    private boolean online() {
        ConnectivityManager connectivity = getSystemService(ConnectivityManager.class);
        Network network = connectivity.getActiveNetwork();
        NetworkCapabilities capabilities =
                network == null ? null : connectivity.getNetworkCapabilities(network);
        return capabilities != null
                && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    }

    /** The longest wait for a resume checkpoint to reach the disk. */
    private static final int CHECKPOINT_MS = 30000;

    private void checkpointBeforeStart(GameDownloads.Entry entry) throws Exception {
        // A killed first download must have a matching metadata checkpoint even
        // before the native periodic checkpoint interval has elapsed.
        entry.state = "pausing";
        catalog.save(entry);
        NativeTorrents.pause(session);
        if (!NativeTorrents.awaitCheckpoint(session, CHECKPOINT_MS))
            throw new IOException("Cannot save the initial torrent checkpoint");
        if (!new File(catalog.sessionDirectory(entry), "resume.dat").isFile())
            throw new IOException(
                    "The initial torrent checkpoint was not written; payload download was not started");
    }

    private void closeSession(String state) throws Exception {
        if (session == 0) return;
        long handle = session;
        String id = activeId;
        worker.removeCallbacks(poll);
        pendingStart = false;
        try {
            GameDownloads.Entry entry = catalog.read(id);
            entry.state = "pausing";
            entry.rate = 0;
            catalog.save(entry);
            NativeTorrents.pause(handle);
            if (!NativeTorrents.awaitCheckpoint(handle, CHECKPOINT_MS))
                throw new IOException("Torrent checkpoint timed out; saved files were retained");
            entry.state = state;
            entry.rate = 0;
            catalog.save(entry);
        } finally {
            NativeTorrents.close(handle);
            session = 0;
            activeId = "";
        }
    }

    /** A failure with no set to show it on: the library shows it above the sets (its kind) until the next action. */
    private void latestError(Throwable error) {
        getSharedPreferences("download-errors", MODE_PRIVATE)
                .edit()
                .putString("latest", String.valueOf(error.getMessage()))
                .putString("latestKind", TorrentStatus.errorKind(error))
                .apply();
    }

    private void failure(String id, Throwable error) {
        try {
            GameDownloads.Entry e = catalog.read(id);
            e.state = "error";
            e.rate = 0;
            e.stalledSeconds = 0;
            e.error = error.getMessage() == null ? error.toString() : error.getMessage();
            e.errorKind = TorrentStatus.errorKind(error);
            catalog.save(e);
            Log.e(TAG, "Download " + id + " failed (" + e.errorKind + "): " + e.error);
        } catch (Exception nested) {
            Log.e(TAG, "Cannot record package error", nested);
        }
    }

    /** A finished set stays offered in the library; this notice leads there. */
    private void finished(GameDownloads.Entry entry) {
        Intent open = DownloadLibraryActivity.reopen(this);
        Notification.Builder builder = builder();
        builder.setSmallIcon(R.drawable.ic_notification_download)
                .setContentTitle(entry.name)
                .setContentText(DownloadWords.text(this, "ready_to_import"))
                .setAutoCancel(true)
                .setContentIntent(
                        PendingIntent.getActivity(
                                this,
                                2,
                                open,
                                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        getSystemService(NotificationManager.class).notify(FINISHED_NOTIFICATION, builder.build());
    }

    /** The ongoing notice; `permille` fills its bar (-1: none). */
    private static void start(Context context, Intent intent) {
        context.startForegroundService(intent);
    }

    private Notification.Builder builder() {
        return new Notification.Builder(this, CHANNEL);
    }

    private Notification notification(String text, int permille) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(
                new NotificationChannel(
                        CHANNEL,
                        DownloadWords.text(this, "title"),
                        NotificationManager.IMPORTANCE_LOW));
        Intent open = DownloadLibraryActivity.reopen(this);
        PendingIntent content =
                PendingIntent.getActivity(
                        this,
                        0,
                        open,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = builder();
        builder.setSmallIcon(R.drawable.ic_notification_download)
                .setContentTitle(DownloadWords.text(this, "title"))
                .setContentText(text)
                .setContentIntent(content)
                .setOngoing(true)
                .setOnlyAlertOnce(true);
        if (permille >= 0) builder.setProgress(1000, permille, false);
        if (!activeId.isEmpty()) {
            Intent pause =
                    new Intent(this, TorrentDownloadService.class)
                            .setAction(PAUSE)
                            .putExtra("id", activeId);
            builder.addAction(
                    R.drawable.ic_notification_pause,
                    DownloadWords.text(this, "pause"),
                    PendingIntent.getService(
                            this,
                            1,
                            pause,
                            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE));
        }
        return builder.build();
    }

    private void updateNotification(GameDownloads.Entry entry) {
        getSystemService(NotificationManager.class)
                .notify(
                        NOTIFICATION,
                        notification(
                                DownloadWords.status(this, entry), DownloadWords.permille(entry)));
    }

    private void maybeStop() {
        synchronized (lifecycle) {
            if (session == 0 && commands.get() == 0) {
                stopForeground(true);
                stopSelfResult(lastStart);
            }
        }
    }

    @Override
    public void onTimeout(int startId, int foregroundServiceType) {
        cancelCopy = true;
        worker.post(
                () -> {
                    String id = activeId;
                    try {
                        closeSession("paused");
                    } catch (Exception error) {
                        failure(id, error);
                    } finally {
                        stopForeground(true);
                        stopSelf(startId);
                    }
                });
    }

    @Override
    public void onDestroy() {
        cancelCopy = true;
        worker.post(
                () -> {
                    try {
                        closeSession("interrupted");
                    } catch (Exception error) {
                        Log.e(TAG, "Download cleanup failed", error);
                    } finally {
                        running = false;
                        thread.quitSafely();
                    }
                });
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        super.onDestroy();
    }
}
