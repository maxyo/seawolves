package org.stormengine.port;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Game import: one application-owned worker; screens subscribe only while visible.
 *
 * <p>UI contract: all calls on the main thread:
 * <ul>
 * <li>{@link #startDocuments} (picker multi-select: archives, installers with volumes, any mix),
 *     {@link #startTree(Uri)} (a picked folder) or {@link #startTree(File)} (a finished download
 *     set of the github library, read in place) begins an import;</li>
 * <li>{@link #add}/{@link #remove} a {@link Listener}; on each change read {@link #snapshot()};</li>
 * <li>{@link Snapshot#choices} non-empty: show the found games, then {@link #choose};</li>
 * <li>{@link Snapshot#passwordFor} non-null: ask for the password, then {@link #answerPassword}
 *     (null cancels);</li>
 * <li>{@link #cancel()} while {@link Snapshot#canCancel}; the finished result is
 *     {@link Snapshot#resultKey} (the result, e.g. import_finished, import_archive_volumes) plus {@link Snapshot#resultArgs}.</li>
 * </ul>
 * The launcher screens (engine launcher.cpp) format the snapshot's text from the UI pack.
 */
final class ImportCoordinator {
    interface Listener {
        void changed();
    }

    /** Immutable view of the import for rendering. */
    static final class Snapshot {
        final boolean busy, recovering, canCancel;
        final ResourceImporter.Stage stage;
        final int files;
        final long bytes, totalBytes;
        final List<ArchiveInstaller.GameRoot> choices;
        final String passwordFor;
        final boolean passwordRetry;
        final String resultKey;
        final Object[] resultArgs;

        Snapshot(ImportCoordinator c) {
            busy = c.busy;
            recovering = c.recovering;
            canCancel = c.canCancel();
            stage = c.stage;
            files = c.files;
            bytes = c.bytes;
            totalBytes = c.totalBytes;
            choices = c.choices;
            passwordFor = c.passwordFor;
            passwordRetry = c.passwordRetry;
            resultKey = c.resultKey;
            resultArgs = c.resultArgs.clone();
        }
    }

    private static ImportCoordinator instance;
    private final Context context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Set<Listener> listeners = new HashSet<>();
    private final LinkedBlockingQueue<Object[]> answers = new LinkedBlockingQueue<>();
    private boolean busy = true, recovering = true;
    private volatile boolean cancelled, committing;
    private String resultKey = "";
    private Object[] resultArgs = {};
    private List<Uri> pendingUris;
    private boolean pendingTree;
    private File pendingDirectory;

    /** What a finished import from `pendingDirectory` / the running one used up (the download set), removed after success. */
    private Runnable pendingConsumed, consumed;

    private ResourceImporter.Stage stage;
    private int files;
    private long bytes, totalBytes;
    private List<ArchiveInstaller.GameRoot> choices = Collections.emptyList();
    private String passwordFor;
    private boolean passwordRetry;

    static synchronized ImportCoordinator get(Context context) {
        if (instance == null) instance = new ImportCoordinator(context.getApplicationContext());
        return instance;
    }

    private ImportCoordinator(Context context) {
        this.context = context;
        new Thread(
                        () -> {
                            String result = "";
                            try {
                                if (ResourceImporter.recover(GameStorage.imported(context))) {
                                    Log.w(
                                            "StormImport",
                                            "An interrupted import was found and cleaned up");
                                    result = "import_interrupted";
                                }
                            } catch (Exception e) {
                                Log.e("StormImport", "Interrupted import recovery failed", e);
                                result = "import_recovery_failed";
                            }
                            finish(result, new Object[0]);
                        },
                        "storm-import-recovery")
                .start();
    }

    void add(Listener listener) {
        listeners.add(listener);
        listener.changed();
    }

    void remove(Listener listener) {
        listeners.remove(listener);
    }

    Snapshot snapshot() {
        return new Snapshot(this);
    }

    boolean busy() {
        return busy;
    }

    boolean canCancel() {
        return busy && !recovering && !cancelled && !committing;
    }

    void clearResult() {
        if (!busy) {
            resultKey = "";
            resultArgs = new Object[0];
            notifyListeners();
        }
    }

    void cancel() {
        if (canCancel()) {
            cancelled = true;
            notifyListeners();
        }
    }

    void choose(ArchiveInstaller.GameRoot root) {
        if (!choices.isEmpty() && root != null && root.ready()) answers.offer(new Object[] {root});
    }

    void answerPassword(String password) {
        if (passwordFor != null) answers.offer(new Object[] {password});
    }

    /** Documents from ACTION_OPEN_DOCUMENT with multiple selection. */
    boolean startDocuments(List<Uri> uris) {
        return start(uris, false, null, null);
    }

    /** Game folder from ACTION_OPEN_DOCUMENT_TREE; copied, never used in place. */
    boolean startTree(Uri tree) {
        List<Uri> uris = new ArrayList<>();
        uris.add(tree);
        return start(uris, true, null, null);
    }

    /**
     * A private directory of this app (a finished download set): a loose game in it moves into place, archives in it
     * are extracted (ArchiveInstaller.installDirectory); `consumed` runs after a successful install (removes the set).
     */
    boolean startTree(File directory, Runnable consumed) {
        return start(Collections.<Uri>emptyList(), true, directory, consumed);
    }

    private boolean start(List<Uri> uris, boolean tree, File directory, Runnable used) {
        if (busy) {
            if (recovering && pendingUris == null && pendingDirectory == null) {
                pendingUris = uris;
                pendingTree = tree;
                pendingDirectory = directory;
                pendingConsumed = used;
                return true;
            }
            return false;
        }
        consumed = used;
        busy = true;
        recovering = false;
        cancelled = false;
        committing = false;
        stage = null;
        files = 0;
        bytes = 0;
        totalBytes = 0;
        choices = Collections.emptyList();
        passwordFor = null;
        answers.clear();
        // Other processes (the game, the download library) see that the game is being replaced.
        ProcessMarker.set(ProcessMarker.IMPORT, true);
        notifyListeners();
        new Thread(() -> run(uris, tree, directory), "storm-resource-import").start();
        return true;
    }

    private final ArchiveInstaller.Choices bridge =
            new ArchiveInstaller.Choices() {
                @Override
                public ArchiveInstaller.GameRoot choose(List<ArchiveInstaller.GameRoot> roots)
                        throws IOException {
                    List<ArchiveInstaller.GameRoot> offered =
                            Collections.unmodifiableList(new ArrayList<>(roots));
                    Log.i("StormImport", "Several games found: " + roots.size());
                    main.post(
                            () -> {
                                choices = offered;
                                notifyListeners();
                            });
                    try {
                        return (ArchiveInstaller.GameRoot) await()[0];
                    } finally {
                        main.post(
                                () -> {
                                    choices = Collections.emptyList();
                                    notifyListeners();
                                });
                    }
                }

                @Override
                public String password(String container, boolean retry) throws IOException {
                    Log.i(
                            "StormImport",
                            (retry ? "Wrong password, asking again for " : "Password needed for ")
                                    + container);
                    main.post(
                            () -> {
                                passwordFor = container;
                                passwordRetry = retry;
                                notifyListeners();
                            });
                    try {
                        return (String) await()[0];
                    } finally {
                        main.post(
                                () -> {
                                    passwordFor = null;
                                    notifyListeners();
                                });
                    }
                }

                private Object[] await() throws IOException {
                    try {
                        while (true) {
                            if (cancelled) throw new ResourceImporter.CancelledException();
                            Object[] answer = answers.poll(200, TimeUnit.MILLISECONDS);
                            if (answer != null) return answer;
                        }
                    } catch (InterruptedException interrupted) {
                        Thread.currentThread().interrupt();
                        throw new ResourceImporter.CancelledException();
                    }
                }
            };

    private void run(List<Uri> uris, boolean tree, File directory) {
        ResourceImporter.Monitor monitor =
                new ResourceImporter.Monitor() {
                    private long lastUpdate;

                    @Override
                    public boolean cancelled() {
                        return cancelled;
                    }

                    @Override
                    public void committing() {
                        committing = true;
                        main.post(() -> notifyListeners());
                    }

                    @Override
                    public void stage(ResourceImporter.Stage next, long total) {
                        Log.i(
                                "StormImport",
                                "Import stage "
                                        + next
                                        + (total > 0 ? " (" + total + " bytes)" : ""));
                        main.post(
                                () -> {
                                    stage = next;
                                    totalBytes = total;
                                    files = 0;
                                    bytes = 0;
                                    notifyListeners();
                                });
                    }

                    @Override
                    public void progress(int count, long processed) {
                        if (throttled()) return;
                        main.post(
                                () -> {
                                    files = count;
                                    bytes = processed;
                                    notifyListeners();
                                });
                    }

                    private boolean throttled() {
                        long now = SystemClock.elapsedRealtime();
                        if (now - lastUpdate < 250) return true;
                        lastUpdate = now;
                        return false;
                    }
                };
        String key;
        Object[] args = new Object[0];
        ContentResolver resolver = context.getContentResolver();
        File game = null;
        try {
            game = GameStorage.imported(context);
            // The found copies are identified against the script patch layer the engine will apply
            // (the choice marks
            // a copy that is not the tested version): installed from the APK before any discovery.
            File patchLayer = MobileResources.scriptPatches(context);
            if (directory != null) {
                Log.i("StormImport", "Importing the finished download " + directory);
                ArchiveInstaller.installDirectory(directory, game, patchLayer, monitor, bridge);
            } else if (tree)
                importTree(
                        new TreeImporter(resolver, uris.get(0), patchLayer),
                        game,
                        patchLayer,
                        monitor);
            else {
                List<PickedDocument> documents = new ArrayList<>();
                for (Uri uri : uris) documents.add(PickedDocument.query(resolver, uri));
                ArchiveInstaller.installDocuments(documents, game, patchLayer, monitor, bridge);
            }
            key = "import_finished";
        } catch (ResourceImporter.CancelledException e) {
            Log.i("StormImport", "Import cancelled");
            key = "import_cancelled";
        } catch (ResourceImporter.StorageException e) {
            Log.e(
                    "StormImport",
                    "Not enough storage for import: need "
                            + e.neededBytes
                            + ", free "
                            + e.availableBytes,
                    e);
            if (e.neededBytes > 0) {
                key = "import_no_space_need";
                double gib = 1073741824.0;
                args =
                        new Object[] {
                            e.neededBytes / gib,
                            e.availableBytes / gib,
                            Math.max(0, e.neededBytes - e.availableBytes) / gib
                        };
            } else key = "import_no_space";
        } catch (ResourceImporter.MissingGameRootException e) {
            Log.e("StormImport", "No game root in the selection", e);
            key = "import_invalid_game";
        } catch (ArchiveInstaller.Failure e) {
            Log.e("StormImport", "Archive import failed: " + e.reason + " " + e.detail, e);
            key = "import_archive_" + e.reason;
            args = new Object[] {e.detail};
        } catch (SecurityException e) {
            Log.e("StormImport", "Selected document is no longer accessible", e);
            key = "import_failed";
        } catch (Exception e) {
            Log.e("StormImport", "Game import failed", e);
            key = "import_failed";
        }
        finish(key, args);
    }

    /** Unpacked roots in the folder are offered; a folder without one is read for archives and installers. */
    private void importTree(
            TreeImporter folder, File game, File patchLayer, ResourceImporter.Monitor monitor)
            throws IOException {
        monitor.stage(ResourceImporter.Stage.READING, 0);
        TreeImporter.Scan scan = folder.scan(monitor);
        if (scan.roots.isEmpty()) {
            if (scan.containers.isEmpty())
                throw new ArchiveInstaller.Failure(
                        ArchiveInstaller.Failure.NO_GAME,
                        "No game folder, archive or installer in the folder");
            Log.i(
                    "StormImport",
                    "No unpacked game in the folder; reading "
                            + scan.containers.size()
                            + " archive/installer files");
            ArchiveInstaller.installDocuments(scan.containers, game, patchLayer, monitor, bridge);
            return;
        }
        if (!scan.containers.isEmpty())
            Log.i(
                    "StormImport",
                    "Folder has unpacked games; its "
                            + scan.containers.size()
                            + " archive/installer files are not read");
        List<ArchiveInstaller.GameRoot> roots =
                folder.describe(
                        scan.roots, new File(context.getCacheDir(), "import-metadata"), monitor);
        ArchiveInstaller.GameRoot chosen = roots.get(0);
        if (roots.size() > 1) {
            monitor.stage(ResourceImporter.Stage.CHOOSING, 0);
            chosen = bridge.choose(roots);
        }
        String id = chosen.id;
        ResourceImporter.importExtracted(
                (stage, progress) -> folder.copyRoot(id, stage, progress), game, monitor);
    }

    /** Deletes the installed game (and what the engine derived from it) as an operation of its own, shown like an import. */
    boolean deleteGame() {
        if (busy) return false;
        busy = true;
        recovering = false;
        cancelled = false;
        committing = true;
        stage = ResourceImporter.Stage.DELETING;
        files = 0;
        bytes = 0;
        totalBytes = 0;
        choices = Collections.emptyList();
        passwordFor = null;
        ProcessMarker.set(ProcessMarker.IMPORT, true);
        notifyListeners();
        new Thread(
                        () -> {
                            String key = "game_deleted";
                            try {
                                ResourceImporter.discard(GameStorage.imported(context));
                                File external = context.getExternalCacheDir();
                                for (GameCache.Removed removed :
                                        GameCache.remove(
                                                Collections.singletonList(
                                                        GameCache.root(
                                                                external, context.getCacheDir()))))
                                    Log.i("StormImport", "Removed with the game: " + removed);
                                Log.i("StormImport", "The player deleted the installed game");
                            } catch (Exception e) {
                                Log.e("StormImport", "Cannot delete the installed game", e);
                                key = "delete_failed";
                            }
                            finish(key, new Object[0]);
                        },
                        "storm-game-delete")
                .start();
        return true;
    }

    private void finish(String result, Object[] args) {
        main.post(
                () -> {
                    busy = false;
                    recovering = false;
                    stage = null;
                    choices = Collections.emptyList();
                    passwordFor = null;
                    resultKey = result;
                    resultArgs = args;
                    ProcessMarker.set(ProcessMarker.IMPORT, false);
                    List<Uri> queued = pendingUris;
                    File queuedDirectory = pendingDirectory;
                    Runnable queuedConsumed = pendingConsumed;
                    pendingUris = null;
                    pendingDirectory = null;
                    pendingConsumed = null;
                    Runnable used = consumed;
                    consumed = null;
                    if (used != null && result.equals("import_finished")) used.run();
                    // A start queued behind the recovery runs after a clean one, also after
                    // cleaning up an interrupted import.
                    if ((queued != null || queuedDirectory != null)
                            && (result.isEmpty() || result.equals("import_interrupted")))
                        start(queued, pendingTree, queuedDirectory, queuedConsumed);
                    else notifyListeners();
                });
    }

    private void notifyListeners() {
        for (Listener listener : new ArrayList<>(listeners)) listener.changed();
    }

    /** A document granted through the system picker, read only through its content URI. */
    private static final class PickedDocument implements ArchiveInstaller.Document {
        private final ContentResolver resolver;
        private final Uri uri;
        private final String name;
        private final long size;

        private PickedDocument(ContentResolver resolver, Uri uri, String name, long size) {
            this.resolver = resolver;
            this.uri = uri;
            this.name = name;
            this.size = size;
        }

        static PickedDocument query(ContentResolver resolver, Uri uri) throws IOException {
            if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme()))
                throw new IOException("Select documents through Android's file picker");
            String name = null;
            long size = -1;
            try (Cursor cursor =
                    resolver.query(
                            uri,
                            new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                            null,
                            null,
                            null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    name = cursor.getString(0);
                    if (!cursor.isNull(1)) size = cursor.getLong(1);
                }
            }
            if (name == null || name.isEmpty())
                throw new IOException("The selected document has no file name");
            return new PickedDocument(resolver, uri, name, size);
        }

        @Override
        public String name() {
            return name;
        }

        @Override
        public long size() {
            return size;
        }

        @Override
        public InputStream open() throws IOException {
            InputStream input = resolver.openInputStream(uri);
            if (input == null) throw new IOException("Cannot open selected document " + name);
            return input;
        }

        /** A ZIP's unpacked size from its central directory, through a seekable descriptor of the provider. */
        @Override
        public long unpackedBytes() throws IOException {
            if (!name.toLowerCase(java.util.Locale.ROOT).endsWith(".zip")) return -1;
            try (android.os.ParcelFileDescriptor descriptor =
                            resolver.openFileDescriptor(uri, "r");
                    java.io.FileInputStream input =
                            new java.io.FileInputStream(
                                    java.util.Objects.requireNonNull(descriptor, "no descriptor")
                                            .getFileDescriptor())) {
                long bytes = ArchiveInstaller.zipUnpackedBytes(input.getChannel());
                Log.i(
                        "StormImport",
                        name
                                + (bytes < 0
                                        ? ": no single-volume ZIP directory; its size is checked"
                                                + " after copying"
                                        : " unpacks to " + bytes + " bytes"));
                return bytes;
            } catch (IOException | IllegalArgumentException | NullPointerException unreadable) {
                // A provider that only streams (no seekable descriptor): the size is checked after
                // copying.
                Log.w(
                        "StormImport",
                        name + ": its unpacked size cannot be read before copying: " + unreadable);
                return -1;
            }
        }
    }
}
