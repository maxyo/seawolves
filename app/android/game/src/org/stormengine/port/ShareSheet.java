package org.stormengine.port;

import android.content.ClipData;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Hands one file the app wrote into its cache to the share sheet (about screen: «Поделиться отчётом»
 * {@link DiagnosticsExport}, «Отправить сохранение» {@link SaveExport}). The file is written off the UI
 * threads, then offered by ACTION_SEND with a read grant for that one URI; a failure is logged and shown.
 */
final class ShareSheet {
    private static final String TAG = "StormShare";

    /** The cache folders a shared file may live in: one per kind, expired exports are removed on the next export. */
    static final String DIAGNOSTICS = "diagnostics", SAVES = "saves";

    private static final List<String> KINDS = Arrays.asList(DIAGNOSTICS, SAVES);

    /** A second tap while one export runs is ignored: the current archive must finish before another export starts. */
    private static final AtomicBoolean running = new AtomicBoolean();

    private ShareSheet() {}

    /** Writes the file of an export (in {@link #directory}); runs on a worker thread. */
    interface Writer {
        File write() throws IOException;
    }

    static void offer(
            final Context context,
            final StormUiView ui,
            final String what,
            final String mime,
            final Writer writer) {
        if (!running.compareAndSet(false, true)) {
            Log.i(TAG, what + ": an export is already running");
            return;
        }
        new Thread(
                        () -> {
                            try {
                                File file = writer.write();
                                String kind = file.getParentFile().getName();
                                if (!sharedFile(context.getCacheDir(), kind, file.getName())
                                        .getCanonicalFile()
                                        .equals(file.getCanonicalFile()))
                                    throw new IOException(
                                            file + " is not in a shared cache folder");
                                Uri uri =
                                        new Uri.Builder()
                                                .scheme("content")
                                                .authority(authority(context))
                                                .appendPath(kind)
                                                .appendPath(file.getName())
                                                .build();
                                Intent send =
                                        new Intent(Intent.ACTION_SEND)
                                                .setType(mime)
                                                .putExtra(Intent.EXTRA_STREAM, uri)
                                                .putExtra(Intent.EXTRA_SUBJECT, file.getName())
                                                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                                send.setClipData(
                                        ClipData.newRawUri(
                                                file.getName(),
                                                uri)); // the grant reaches the chosen app
                                Log.i(
                                        TAG,
                                        what
                                                + ": sharing "
                                                + file
                                                + " ("
                                                + file.length()
                                                + " bytes)");
                                if (!new Handler(Looper.getMainLooper())
                                        .post(
                                                () -> {
                                                    try {
                                                        context.startActivity(
                                                                Intent.createChooser(send, null));
                                                    } catch (RuntimeException error) {
                                                        Log.e(
                                                                TAG,
                                                                what + " share sheet failed",
                                                                error);
                                                        ui.toast(error.toString(), "danger");
                                                    } finally {
                                                        running.set(false);
                                                    }
                                                }))
                                    throw new IOException("The share sheet cannot be opened");
                            } catch (IOException | RuntimeException error) {
                                Log.e(TAG, what + " failed", error);
                                ui.toast(error.toString(), "danger");
                                running.set(false);
                            }
                        },
                        TAG)
                .start();
    }

    /** Expire cache exports after a day; a new export must not revoke a receiver's recent URI. */
    static File directory(Context context, String kind) throws IOException {
        File directory = folder(context, kind);
        if (!directory
                .getCanonicalFile()
                .equals(new File(context.getCacheDir().getCanonicalFile(), kind)))
            throw new IOException("A shared cache folder must not be a link");
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IOException("Cannot create " + directory);
        File[] old = directory.listFiles();
        if (old == null) throw new IOException("Cannot list " + directory);
        long cutoff = System.currentTimeMillis() - 24L * 60 * 60 * 1000;
        for (File stale : old)
            if (stale.isFile() && stale.lastModified() < cutoff && !stale.delete())
                Log.w(TAG, "Cannot delete " + stale);
        return directory;
    }

    private static File folder(Context context, String kind) {
        if (!KINDS.contains(kind))
            throw new IllegalArgumentException("Unknown shared kind " + kind);
        return new File(context.getCacheDir(), kind);
    }

    private static String authority(Context context) {
        return context.getPackageName() + ".share";
    }

    /** The provider's path boundary, also checked on the host without opening Android file descriptors. */
    static File sharedFile(File cache, String kind, String name) throws FileNotFoundException {
        if (!KINDS.contains(kind)
                || name == null
                || name.isEmpty()
                || name.contains("/")
                || name.contains("\\")
                || name.startsWith(".")) throw new FileNotFoundException("Not a shared export");
        File directory = new File(cache, kind), file = new File(directory, name);
        try {
            // Neither the folder nor the file may redirect outside the app's cache.
            File expected = new File(new File(cache.getCanonicalFile(), kind), name);
            if (!file.isFile() || !file.getCanonicalFile().equals(expected))
                throw new FileNotFoundException(name);
        } catch (IOException invalid) {
            throw new FileNotFoundException(name);
        }
        return file;
    }

    /**
     * Serves the shared files read-only to the app the player picks (granted per URI), as
     * content://&lt;application id&gt;.share/&lt;kind&gt;/&lt;file&gt; (the app is built without AndroidX, so
     * without its FileProvider).
     */
    public static final class Provider extends ContentProvider {
        @Override
        public boolean onCreate() {
            return true;
        }

        private File file(Uri uri) throws FileNotFoundException {
            List<String> path = uri.getPathSegments();
            if (path.size() != 2) throw new FileNotFoundException(uri.toString());
            return sharedFile(getContext().getCacheDir(), path.get(0), path.get(1));
        }

        @Override
        public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
            if (!"r".equals(mode)) throw new SecurityException("Read-only: " + uri);
            return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY);
        }

        @Override
        public Cursor query(
                Uri uri, String[] projection, String selection, String[] arguments, String order) {
            File file;
            try {
                file = file(uri);
            } catch (FileNotFoundException missing) {
                return null;
            }
            String[] columns =
                    projection != null
                            ? projection
                            : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
            Object[] row = new Object[columns.length];
            for (int i = 0; i < columns.length; ++i)
                row[i] =
                        OpenableColumns.DISPLAY_NAME.equals(columns[i])
                                ? file.getName()
                                : OpenableColumns.SIZE.equals(columns[i])
                                        ? (Object) file.length()
                                        : null;
            MatrixCursor cursor = new MatrixCursor(columns, 1);
            cursor.addRow(row);
            return cursor;
        }

        @Override
        public String getType(Uri uri) {
            String name = String.valueOf(uri.getLastPathSegment());
            return name.endsWith(".zip")
                    ? "application/zip"
                    : name.endsWith(".txt") ? "text/plain" : null;
        }

        @Override
        public Uri insert(Uri uri, ContentValues values) {
            throw new UnsupportedOperationException("read-only");
        }

        @Override
        public int delete(Uri uri, String selection, String[] arguments) {
            throw new UnsupportedOperationException("read-only");
        }

        @Override
        public int update(Uri uri, ContentValues values, String selection, String[] arguments) {
            throw new UnsupportedOperationException("read-only");
        }
    }
}
