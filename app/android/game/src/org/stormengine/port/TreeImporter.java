package org.stormengine.port;

import android.content.ContentResolver;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.TreeSet;

/**
 * A folder chosen with the Storage Access Framework. Every unpacked game root
 * in it (engine.ini, PROGRAM and RESOURCE together, up to {@link #MAX_DEPTH}
 * folders deep) is offered; a folder without one is searched for archives and
 * installers, which are then read like picked documents. Only engine.ini,
 * options, PROGRAM and RESOURCE are copied, into an empty private stage that
 * the recoverable ResourceImporter transaction publishes. No broad storage
 * permission is needed and a failed copy preserves the previous game.
 */
final class TreeImporter {
    private static final long LIMIT = ArchiveInstaller.TOTAL_BYTES;
    private static final long RESERVE = ArchiveInstaller.RESERVE;
    private static final int MAX_FILES = 200000, MAX_DEPTH = 6, MAX_CONTAINERS = 1000;
    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE
    };

    /** Names that can hold a game: archives, disc images, installers and their volumes. */
    private static final String[] CONTAINERS = {
        ".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".bz2", ".tbz", ".xz", ".txz", ".iso", ".cab",
        ".exe", ".bin"
    };

    private static final class Item {
        final String documentId, path;
        final long bytes;

        Item(String documentId, String path, long bytes) {
            this.documentId = documentId;
            this.path = path;
            this.bytes = bytes;
        }
    }

    /** What a folder holds: unpacked roots (document id + display path) and container files. */
    static final class Scan {
        final List<String[]> roots = new ArrayList<>();
        final List<ArchiveInstaller.Document> containers = new ArrayList<>();
    }

    private final ContentResolver resolver;
    private final Uri tree;

    /** The APK's assets: the roots are identified against its script patch layer. */
    private final AssetManager assets;

    TreeImporter(ContentResolver resolver, Uri tree, AssetManager assets) {
        this.resolver = resolver;
        this.tree = tree;
        this.assets = assets;
    }

    Scan scan(ResourceImporter.Monitor monitor) throws IOException {
        Scan scan = new Scan();
        ArrayList<String[]> pending = new ArrayList<>(); // {documentId, relative path, depth}
        pending.add(new String[] {DocumentsContract.getTreeDocumentId(tree), "", "0"});
        int seen = 0;
        while (!pending.isEmpty()) {
            String[] next = pending.remove(pending.size() - 1);
            List<String[]> children = children(next[0]);
            if ((seen += children.size()) > MAX_FILES)
                throw new ArchiveInstaller.Failure(
                        ArchiveInstaller.Failure.LIMIT, "The folder has too many files");
            if (rootItems(children) != null) {
                scan.roots.add(new String[] {next[0], next[1]});
                continue;
            }
            int depth = Integer.parseInt(next[2]);
            for (String[] child : children) {
                check(monitor);
                String relative = next[1].isEmpty() ? child[1] : next[1] + "/" + child[1];
                if (directory(child)) {
                    if (depth < MAX_DEPTH)
                        pending.add(new String[] {child[0], relative, String.valueOf(depth + 1)});
                } else if (container(child[1])) {
                    if (scan.containers.size() >= MAX_CONTAINERS)
                        throw new ArchiveInstaller.Failure(
                                ArchiveInstaller.Failure.LIMIT, "The folder has too many archives");
                    scan.containers.add(new TreeDocument(child[0], relative, parse(child[3])));
                }
            }
        }
        return scan;
    }

    /**
     * Describes unpacked roots for the choice. Title, version, default language and the script patch set that
     * identifies the copy come from the engine's reader, given copies of just the files that hold them (the
     * identify files the layer names, whole).
     */
    List<ArchiveInstaller.GameRoot> describe(
            List<String[]> roots, File scratch, ResourceImporter.Monitor monitor)
            throws IOException {
        TreeSet<String> identify = identifyFiles();
        List<ArchiveInstaller.GameRoot> result = new ArrayList<>();
        for (String[] root : roots) {
            List<Item> files = items(root[0], monitor);
            long bytes = 0;
            TreeSet<String> languages = new TreeSet<>();
            List<String> markers = new ArrayList<>();
            for (Item item : files) {
                bytes += Math.max(0, item.bytes);
                String[] parts = item.path.split("/");
                if (parts.length >= 5
                        && parts[0].equals("RESOURCE")
                        && parts[1].equalsIgnoreCase("ini")
                        && parts[2].equalsIgnoreCase("texts"))
                    languages.add(parts[3].toLowerCase(Locale.ROOT));
            }
            for (String[] child : children(root[0])) {
                String name = child[1].toLowerCase(Locale.ROOT);
                if (!directory(child)
                        && (name.contains("patch")
                                || name.contains("update")
                                || name.contains("hotfix"))) markers.add(child[1]);
            }
            String folder = root[1].isEmpty() ? "" : root[1] + "/";
            result.add(
                    metadata(
                            root[0],
                            folder,
                            files,
                            identify,
                            scratch,
                            new ArrayList<>(languages),
                            markers,
                            bytes));
        }
        return result;
    }

    /** The resource keys (lower case, as installed) the layer's script patch sets identify a game by. */
    private TreeSet<String> identifyFiles() throws IOException {
        String[] result = NativeArchives.patchLayer(assets);
        if (Integer.parseInt(result[0]) != ArchiveStatus.OK)
            throw new IOException("Cannot read the script patch layer: " + result[1]);
        TreeSet<String> keys = new TreeSet<>();
        try {
            JSONArray sets = new JSONObject(result[2]).getJSONArray("patch_sets");
            for (int i = 0; i < sets.length(); i++) {
                JSONArray identify = sets.getJSONObject(i).getJSONArray("identify");
                for (int k = 0; k < identify.length(); k++) keys.add(identify.getString(k));
            }
        } catch (JSONException error) {
            throw new IOException("Archive reader returned an invalid script patch layer", error);
        }
        return keys;
    }

    private ArchiveInstaller.GameRoot metadata(
            String rootId,
            String folder,
            List<Item> files,
            TreeSet<String> identify,
            File scratch,
            List<String> languages,
            List<String> markers,
            long bytes)
            throws IOException {
        File source = new File(scratch, "source"), work = new File(scratch, "work");
        ResourceImporter.deleteTree(scratch);
        if (!scratch.mkdirs() || !source.mkdir() || !work.mkdir())
            throw new IOException("Cannot create private scratch directory");
        try {
            for (Item item : files) {
                String path = item.path.toLowerCase(Locale.ROOT);
                boolean wanted =
                        path.equals("engine.ini")
                                || path.equals("program/_lsc_on_off.h")
                                || path.equals("resource/ini/texts/language.ini");
                // An identify file is copied whole, whatever its size: the reader decides what it
                // can identify.
                if (identify.contains(path))
                    copy(item, new File(source, item.path), ArchiveInstaller.FILE_BYTES);
                else if (wanted && item.bytes >= 0 && item.bytes <= (1 << 20))
                    copy(item, new File(source, item.path), 1 << 20);
            }
            // The reader recognises a root only with engine.ini and PROGRAM/ and RESOURCE/ content
            // present. The stand-ins are named so that they are never a file a script patch set
            // identifies a game by.
            for (String name :
                    new String[] {
                        "PROGRAM/storm-placeholder", "RESOURCE/storm-placeholder", "engine.ini"
                    }) {
                String parent = name.contains("/") ? name.substring(0, name.indexOf('/')) : name;
                if (findIgnoringCase(source, parent) != null) continue;
                File placeholder = new File(source, name);
                placeholder.getParentFile().mkdirs();
                new FileOutputStream(placeholder).close();
            }
            String[] result =
                    NativeArchives.discover(
                            source.getPath(),
                            work.getPath(),
                            null,
                            assets,
                            ArchiveInstaller.limits(),
                            null);
            if (!"0".equals(result[0]))
                throw new IOException("Cannot read the game's version: " + result[2]);
            JSONObject document = new JSONObject(result[3]);
            JSONArray candidates = document.getJSONArray("candidates");
            if (candidates.length() != 1)
                throw new IOException(
                        "The game's metadata is not one game root: " + candidates.length());
            JSONObject candidate = candidates.getJSONObject(0);
            List<String> none = new ArrayList<>();
            return new ArchiveInstaller.GameRoot(
                    rootId,
                    "tree",
                    "folder",
                    folder,
                    candidate.optString("title"),
                    candidate.optString("version"),
                    candidate.optString("default_language"),
                    none,
                    languages,
                    markers,
                    none,
                    bytes,
                    files.size(),
                    ArchiveInstaller.Identification.parse(candidate, document));
        } catch (JSONException error) {
            throw new IOException("Archive reader returned an invalid result", error);
        } finally {
            ResourceImporter.deleteTree(scratch);
        }
    }

    private static File findIgnoringCase(File base, String relative) {
        File current = base;
        for (String part : relative.split("/")) {
            File[] children = current.listFiles();
            File match = null;
            if (children != null)
                for (File child : children)
                    if (child.getName().equalsIgnoreCase(part)) match = child;
            if (match == null) return null;
            current = match;
        }
        return current;
    }

    /** Copies one unpacked root into the empty stage under the canonical names. */
    int copyRoot(String rootId, File stage, ResourceImporter.Monitor monitor) throws IOException {
        List<Item> files = items(rootId, monitor);
        long total = 0;
        for (Item item : files) total += Math.max(0, item.bytes);
        if (monitor != null) monitor.stage(ResourceImporter.Stage.INSTALLING, total);
        if (total + RESERVE > stage.getUsableSpace())
            throw new ResourceImporter.StorageException(
                    "Insufficient free storage for the game folder",
                    total + RESERVE,
                    stage.getUsableSpace());
        long[] copied = {0};
        int count = 0;
        for (Item item : files) {
            check(monitor);
            copied[0] += copy(item, new File(stage, item.path), LIMIT - copied[0]);
            count++;
            if (monitor != null) monitor.progress(count, copied[0]);
            if (stage.getUsableSpace() < RESERVE)
                throw new ResourceImporter.StorageException(
                        "Insufficient free storage", 0, stage.getUsableSpace());
        }
        return count;
    }

    private long copy(Item item, File output, long limit) throws IOException {
        File parent = output.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create directory");
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, item.documentId);
        long written = 0;
        byte[] buffer = new byte[65536];
        try (InputStream in = resolver.openInputStream(document);
                FileOutputStream out = new FileOutputStream(output)) {
            if (in == null) throw new IOException("Cannot open " + item.path);
            for (int n; (n = in.read(buffer)) != -1; ) {
                written += n;
                if (written > limit)
                    throw new ArchiveInstaller.Failure(
                            ArchiveInstaller.Failure.LIMIT, "Game folder exceeds the import limit");
                out.write(buffer, 0, n);
            }
        }
        return written;
    }

    /** engine.ini, options, PROGRAM/** and RESOURCE/** of a root, with canonical top-level names. */
    private List<Item> items(String rootId, ResourceImporter.Monitor monitor) throws IOException {
        String[][] top = rootItems(children(rootId));
        if (top == null) throw new ResourceImporter.MissingGameRootException();
        List<Item> files = new ArrayList<>();
        long[] total = {0};
        for (String[] child : top) {
            if (child == null) continue;
            String name = canonical(child[1]);
            if (directory(child)) walk(child[0], name, files, total, monitor);
            else {
                files.add(new Item(child[0], name, parse(child[3])));
                total[0] += Math.max(0, parse(child[3]));
            }
        }
        return files;
    }

    /** {engine.ini, PROGRAM, RESOURCE, options} children when the folder is a game root, else null. */
    private static String[][] rootItems(List<String[]> children) {
        String[][] found = new String[4][];
        for (String[] child : children) {
            String name = child[1] == null ? "" : child[1].toLowerCase(Locale.ROOT);
            if (!directory(child) && name.equals("engine.ini")) found[0] = child;
            else if (directory(child) && name.equals("program")) found[1] = child;
            else if (directory(child) && name.equals("resource")) found[2] = child;
            else if (!directory(child) && name.equals("options")) found[3] = child;
        }
        return found[0] != null && found[1] != null && found[2] != null ? found : null;
    }

    private static String canonical(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.equals("program") ? "PROGRAM" : lower.equals("resource") ? "RESOURCE" : lower;
    }

    private static boolean directory(String[] child) {
        return DocumentsContract.Document.MIME_TYPE_DIR.equals(child[2]);
    }

    private static boolean container(String name) {
        String lower = name == null ? "" : name.toLowerCase(Locale.ROOT);
        if (lower.matches(".*\\.(\\d{3}|r\\d{2})$"))
            return true; // Raw split parts and old RAR volumes.
        for (String suffix : CONTAINERS) if (lower.endsWith(suffix)) return true;
        return false;
    }

    private void walk(
            String directoryId,
            String path,
            List<Item> files,
            long[] total,
            ResourceImporter.Monitor monitor)
            throws IOException {
        // Explicit stack: deep game trees must not exhaust the worker thread's stack.
        ArrayList<String[]> pending = new ArrayList<>();
        pending.add(new String[] {directoryId, path});
        while (!pending.isEmpty()) {
            String[] next = pending.remove(pending.size() - 1);
            for (String[] child : children(next[0])) {
                check(monitor);
                String name = child[1];
                if (name == null
                        || name.isEmpty()
                        || name.equals(".")
                        || name.equals("..")
                        || name.contains("/")
                        || name.indexOf('\0') >= 0)
                    throw new IOException("Game folder contains an unsafe name");
                String relative = next[1] + "/" + name;
                if (directory(child)) pending.add(new String[] {child[0], relative});
                else {
                    long bytes = parse(child[3]);
                    if (files.size() >= MAX_FILES)
                        throw new ArchiveInstaller.Failure(
                                ArchiveInstaller.Failure.LIMIT, "Game folder has too many files");
                    total[0] += Math.max(0, bytes);
                    if (total[0] > LIMIT)
                        throw new ArchiveInstaller.Failure(
                                ArchiveInstaller.Failure.LIMIT,
                                "Game folder exceeds the import limit");
                    files.add(new Item(child[0], relative, bytes));
                }
            }
        }
    }

    private List<String[]> children(String documentId) throws IOException {
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        ArrayList<String[]> result = new ArrayList<>();
        try (Cursor cursor = resolver.query(uri, COLUMNS, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list the selected folder");
            while (cursor.moveToNext())
                result.add(
                        new String[] {
                            cursor.getString(0),
                            cursor.getString(1),
                            cursor.getString(2),
                            cursor.isNull(3) ? "-1" : cursor.getString(3)
                        });
        }
        return result;
    }

    /** A document's COLUMN_SIZE as text; -1 (unknown, checked while copying) when the provider gave none. */
    private static long parse(String value) {
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException unknown) {
            Log.w("StormImport", "A document provider gave an unreadable size: " + value);
            return -1;
        }
    }

    private static void check(ResourceImporter.Monitor monitor)
            throws ResourceImporter.CancelledException {
        if (monitor != null && monitor.cancelled()) throw new ResourceImporter.CancelledException();
    }

    /** A container file in the picked folder, copied with its folder path kept (installer volumes stay together). */
    private final class TreeDocument implements ArchiveInstaller.Document {
        private final String documentId, path;
        private final long size;

        TreeDocument(String documentId, String path, long size) {
            this.documentId = documentId;
            this.path = path;
            this.size = size;
        }

        @Override
        public String name() {
            return path;
        }

        @Override
        public long size() {
            return size;
        }

        @Override
        public InputStream open() throws IOException {
            InputStream input =
                    resolver.openInputStream(
                            DocumentsContract.buildDocumentUriUsingTree(tree, documentId));
            if (input == null) throw new IOException("Cannot open " + path);
            return input;
        }
    }
}
