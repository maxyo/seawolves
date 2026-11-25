package org.stormengine.port;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * Persistent independent packages. Never writes a selected game's directory.
 * Each set's directory holds its small status record (set.json, rewritten while a transfer
 * runs) and, written once, the torrent's file list (metadata.json) and the chosen files
 * (selection.json): a game torrent lists tens of thousands of files, megabytes that must
 * not be copied, parsed or rewritten with every status update.
 */
final class GameDownloads {
    private static final Object LOCK = new Object();
    private static final int MAX_RECORD = 1024 * 1024;

    /** metadata.json: the native manifest of at most 100000 files with paths up to 4096 bytes. */
    private static final int MAX_METADATA = 256 * 1024 * 1024;

    private static final int MAX_SELECTION = 100000;
    private final File root;

    static final class Entry {
        String id, kind, name, source, state = "saved", error = "";
        String phase = "metadata";
        String sourceState = "";

        /** What the import's discovery found in the finished files: game, none, undecided; "" until it ran. */
        String discovered = "";

        // Transfer health from the last poll: a fatal error's kind, a non-fatal network
        // problem, and how long nothing arrived (the UI calls it stalled past STALL_SECONDS).
        String errorKind = "", networkError = "";
        long downloaded, wanted, rate, updated, peers, seeds, stalledSeconds;

        /** The phone had no network at the last poll. */
        boolean offline;

        /** Share of the hash check after a resume (state checking), in thousandths. */
        long checkedPermille;

        final ArrayList<String> names = new ArrayList<>(), documents = new ArrayList<>();

        /** Whether metadata.json exists, and its modification time (a changed file list is a new snapshot). */
        boolean hasMetadata;

        long metadataStamp;

        /** How many files selection.json chose; 0 before a selection. */
        int selectedCount;

        boolean corrupt;
    }

    GameDownloads(File root) throws IOException {
        this.root = root.getCanonicalFile();
        if (!this.root.isDirectory() && !this.root.mkdirs())
            throw new IOException("Cannot create download library");
    }

    Entry create(
            String kind, String name, String source, List<String> names, List<String> documents)
            throws IOException {
        synchronized (LOCK) {
            if (!Arrays.asList("torrent", "magnet", "archive").contains(kind))
                throw new IOException("Unknown package type");
            if (name == null || name.length() > 4096 || source == null || source.length() > 16384)
                throw new IOException("Invalid package name/source");
            if (names.size() != documents.size() || names.size() > 1000)
                throw new IOException("Invalid document selection");
            HashSet<String> unique = new HashSet<>();
            for (String item : names) {
                safeName(item);
                if (!unique.add(item.toLowerCase(Locale.ROOT)))
                    throw new IOException("Duplicate input filename: " + item);
            }
            if ("magnet".equals(kind)) validMagnet(source);
            Entry entry = new Entry();
            entry.id = UUID.randomUUID().toString();
            entry.kind = kind;
            entry.name = name;
            entry.source = source;
            entry.names.addAll(names);
            entry.documents.addAll(documents);
            if (!directory(entry.id).mkdir())
                throw new IOException("Cannot create independent package directory");
            save(entry);
            return entry;
        }
    }

    /** A magnet link must name its torrent by a well-formed info-hash (BitTorrent v1 hex/base32 or v2 SHA-256). */
    static void validMagnet(String link) throws IOException {
        if (!link.startsWith("magnet:?"))
            throw new DownloadProblem(
                    "magnet-prefix", "Invalid magnet link: it must start with magnet:?");
        boolean hash = false;
        for (String parameter : link.substring(8).split("&")) {
            String lower = parameter.toLowerCase(Locale.ROOT);
            if (lower.startsWith("xt=urn:btih:")) {
                String value = parameter.substring(12);
                if (!value.matches("[0-9a-fA-F]{40}|[A-Za-z2-7]{32}"))
                    throw new DownloadProblem(
                            "magnet-damaged",
                            "Invalid magnet link: malformed BitTorrent info-hash");
                hash = true;
            } else if (lower.startsWith("xt=urn:btmh:")) {
                if (!parameter.substring(12).matches("1220[0-9a-fA-F]{64}"))
                    throw new DownloadProblem(
                            "magnet-damaged",
                            "Invalid magnet link: malformed BitTorrent v2 info-hash");
                hash = true;
            }
        }
        if (!hash)
            throw new DownloadProblem(
                    "magnet-damaged",
                    "Invalid magnet link: no info-hash (xt=urn:btih:… or urn:btmh:…)");
    }

    File directory(String id) throws IOException {
        if (id == null
                || !id.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
            throw new IOException("Invalid package ID");
        return contained(new File(root, id));
    }

    File input(Entry entry, String name) throws IOException {
        safeName(name);
        File parent = contained(new File(directory(entry.id), "input"));
        if (!parent.isDirectory() && !parent.mkdir())
            throw new IOException("Cannot create package input directory");
        return contained(new File(parent, name));
    }

    File sessionDirectory(Entry entry) throws IOException {
        File file = contained(new File(directory(entry.id), "torrent"));
        if (!file.isDirectory() && !file.mkdir())
            throw new IOException("Cannot create torrent directory");
        return file;
    }

    File copyTemporary(Entry entry) throws IOException {
        return contained(new File(directory(entry.id), "copy.partial"));
    }

    /**
     * The finished files of a set, for an import that reads them in place: the copied
     * documents of a local set, the downloaded payload of a torrent. Only a set whose
     * transfer completed and checkpointed qualifies.
     */
    File importSource(String id) throws IOException {
        Entry entry = read(id);
        if (!ready(entry)) throw new IOException("This set has not finished downloading");
        File source =
                contained(
                        new File(
                                directory(id),
                                "archive".equals(entry.kind) ? "input" : "torrent/payload"));
        if (!source.isDirectory())
            throw new DownloadProblem(
                    "files-missing", "The finished files of this set are missing");
        return source;
    }

    /** Records the discovery of a set's finished files, unless the set changed since (no longer ready). */
    void recordDiscovery(String id, String discovered) throws IOException {
        synchronized (LOCK) {
            Entry entry = read(id);
            if (!ready(entry)) return;
            entry.discovered = discovered;
            save(entry);
        }
    }

    /** Complete and checkpointed; nothing is still being written. */
    static boolean ready(Entry entry) {
        return !entry.corrupt
                && "complete".equals(entry.sourceState)
                && !Arrays.asList(ACTIVE).contains(entry.state);
    }

    /** States in which the download process is writing the set. */
    static final String[] ACTIVE = {
        "copying", "inspecting", "metadata", "checking", "downloading", "pausing"
    };

    String nativeSource(Entry entry) throws IOException {
        if ("magnet".equals(entry.kind)) return entry.source;
        if (!entry.names.contains(entry.source))
            throw new IOException("Package has no saved source file");
        File file = input(entry, entry.source);
        if (!file.isFile())
            throw new DownloadProblem("files-missing", "Saved source file is missing");
        return file.getAbsolutePath();
    }

    private File contained(File file) throws IOException {
        File canonical = file.getCanonicalFile();
        if (!canonical.getPath().startsWith(root.getPath() + File.separator)
                || !canonical.equals(file.getAbsoluteFile()))
            throw new IOException(
                    "Package path escaped its private directory or contains a symlink");
        return canonical;
    }

    static void safeName(String name) throws IOException {
        if (name == null
                || name.isEmpty()
                || name.length() > 255
                || name.equals(".")
                || name.equals("..")
                || name.contains("/")
                || name.contains("\\")
                || name.contains(":")
                || name.endsWith(".")
                || name.endsWith(" ")) throw new IOException("Unsafe input filename");
        for (int i = 0; i < name.length(); i++)
            if (name.charAt(i) < 32 || name.charAt(i) == 127)
                throw new IOException("Unsafe input filename");
    }

    Entry read(String id) throws IOException {
        synchronized (LOCK) {
            File file = contained(new File(directory(id), "set.json"));
            String text = new String(readBounded(file, MAX_RECORD), StandardCharsets.UTF_8);
            JSONObject values = object(text);
            if (number(values, "version") != 1 || !id.equals(string(values, "id")))
                throw new IOException("Invalid package record");
            Entry e = new Entry();
            e.id = id;
            e.kind = string(values, "kind");
            e.name = string(values, "name");
            e.source = string(values, "source");
            e.state = string(values, "state");
            e.error = string(values, "error");
            e.phase = string(values, "phase");
            e.sourceState = string(values, "sourceState");
            e.downloaded = number(values, "downloaded");
            e.wanted = number(values, "wanted");
            e.rate = number(values, "rate");
            e.updated = number(values, "updated");
            e.errorKind = optionalString(values, "errorKind");
            e.networkError = optionalString(values, "networkError");
            e.discovered = optionalString(values, "discovered");
            if (!Arrays.asList("", "game", "none", "undecided").contains(e.discovered))
                throw new IOException("Invalid discovery result");
            e.peers = optionalNumber(values, "peers");
            e.seeds = optionalNumber(values, "seeds");
            e.stalledSeconds = optionalNumber(values, "stalledSeconds");
            e.offline = Boolean.TRUE.equals(values.opt("offline"));
            e.checkedPermille =
                    Math.max(0, Math.min(1000, optionalNumber(values, "checkedPermille")));
            JSONArray names = list(values, "names"), documents = list(values, "documents");
            for (int i = 0; i < names.length(); i++) {
                Object item = names.opt(i);
                if (!(item instanceof String)) throw new IOException("Invalid filename");
                safeName((String) item);
                e.names.add((String) item);
            }
            for (int i = 0; i < documents.length(); i++) {
                Object item = documents.opt(i);
                if (!(item instanceof String)) throw new IOException("Invalid URI");
                e.documents.add((String) item);
            }
            e.selectedCount =
                    (int) Math.min(Integer.MAX_VALUE, optionalNumber(values, "selectedCount"));
            if (!Arrays.asList("torrent", "magnet", "archive").contains(e.kind)
                    || e.names.size() != e.documents.size())
                throw new IOException("Invalid package source");
            if (e.names.size() > 1000
                    || e.source.length() > 16384
                    || e.name.length() > 4096
                    || e.selectedCount > MAX_SELECTION
                    || e.downloaded < 0
                    || e.wanted < 0
                    || e.rate < 0
                    || e.peers < 0
                    || e.seeds < 0
                    || e.stalledSeconds < 0) throw new IOException("Invalid package limits");
            HashSet<String> unique = new HashSet<>();
            for (String name : e.names)
                if (!unique.add(name.toLowerCase(Locale.ROOT)))
                    throw new IOException("Duplicate saved filename");
            File metadata = metadataFile(id);
            e.hasMetadata = metadata.isFile();
            e.metadataStamp = e.hasMetadata ? metadata.lastModified() : 0;
            return e;
        }
    }

    List<Entry> list() throws IOException {
        synchronized (LOCK) {
            ArrayList<Entry> result = new ArrayList<>();
            File[] children = root.listFiles();
            if (children == null) throw new IOException("Cannot read download library");
            for (File child : children) {
                String id = child.getName();
                if (!id.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
                    continue;
                try {
                    result.add(read(id));
                } catch (IOException damaged) {
                    Entry e = new Entry();
                    e.id = id;
                    e.name = id;
                    e.kind = "archive";
                    e.source = "";
                    e.state = "error";
                    e.error = "Cannot read saved package record: " + damaged.getMessage();
                    e.corrupt = true;
                    result.add(e);
                }
            }
            result.sort((a, b) -> Long.compare(b.updated, a.updated));
            return result;
        }
    }

    void save(Entry e) throws IOException {
        synchronized (LOCK) {
            if (e.corrupt) throw new IOException("Damaged records are read-only");
            File directory = directory(e.id);
            if (!directory.isDirectory()) throw new IOException("Package directory is missing");
            JSONObject m = new JSONObject();
            try {
                m.put("version", 1);
                m.put("id", e.id);
                m.put("kind", e.kind);
                m.put("name", e.name);
                m.put("source", e.source);
                m.put("state", e.state);
                m.put("error", e.error);
                m.put("downloaded", e.downloaded);
                m.put("phase", e.phase);
                m.put("sourceState", e.sourceState);
                m.put("errorKind", e.errorKind);
                m.put("networkError", e.networkError);
                m.put("discovered", e.discovered);
                m.put("peers", e.peers);
                m.put("seeds", e.seeds);
                m.put("stalledSeconds", e.stalledSeconds);
                m.put("offline", e.offline);
                m.put("checkedPermille", e.checkedPermille);
                m.put("wanted", e.wanted);
                m.put("rate", e.rate);
                e.updated = System.currentTimeMillis();
                m.put("updated", e.updated);
                m.put("names", new JSONArray(e.names));
                m.put("documents", new JSONArray(e.documents));
                m.put("selectedCount", e.selectedCount);
            } catch (JSONException impossible) {
                throw new IOException("Package record cannot be written", impossible);
            }
            byte[] bytes = m.toString().getBytes(StandardCharsets.UTF_8);
            if (bytes.length > MAX_RECORD) throw new IOException("Package record is too large");
            writeAtomically(contained(new File(directory, "set.json")), bytes);
        }
    }

    /** The set's torrent metadata JSON (native manifest, version 2); "" before it is known. */
    String metadata(Entry e) throws IOException {
        synchronized (LOCK) {
            File file = metadataFile(e.id);
            return file.isFile()
                    ? new String(readBounded(file, MAX_METADATA), StandardCharsets.UTF_8)
                    : "";
        }
    }

    /** Keeps the set's metadata; an unchanged file list is not rewritten. */
    void saveMetadata(Entry e, String json) throws IOException {
        if (json == null || json.isEmpty()) throw new IOException("Empty torrent metadata");
        byte[] bytes = json.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > MAX_METADATA) throw new IOException("Torrent metadata is too large");
        synchronized (LOCK) {
            File file = metadataFile(e.id);
            if (!file.isFile()
                    || file.length() != bytes.length
                    || !Arrays.equals(readBounded(file, MAX_METADATA), bytes))
                writeAtomically(file, bytes);
            e.hasMetadata = true;
            e.metadataStamp = file.lastModified();
        }
    }

    /** The chosen file indices of the set; empty before a selection. */
    int[] selection(Entry e) throws IOException {
        synchronized (LOCK) {
            File file = contained(new File(directory(e.id), "selection.json"));
            if (!file.isFile()) return new int[0];
            JSONArray items;
            try {
                items =
                        new JSONArray(
                                new String(
                                        readBounded(file, MAX_SELECTION * 12 + 2),
                                        StandardCharsets.UTF_8));
            } catch (IOException | JSONException damaged) {
                throw new IOException("Invalid file selection", damaged);
            }
            if (items.length() > MAX_SELECTION) throw new IOException("Invalid file selection");
            int[] selection = new int[items.length()];
            for (int i = 0; i < selection.length; i++) {
                Object item = items.opt(i);
                if (!(item instanceof Integer) || (Integer) item < 0)
                    throw new IOException("Invalid file selection");
                selection[i] = (Integer) item;
            }
            return selection;
        }
    }

    /** Records the chosen files; the caller saves the record with the new selectedCount. */
    void saveSelection(Entry e, int[] selection) throws IOException {
        if (selection.length > MAX_SELECTION) throw new IOException("Invalid file selection");
        StringBuilder text = new StringBuilder("[");
        for (int i = 0; i < selection.length; i++) {
            if (selection[i] < 0) throw new IOException("Invalid selection");
            if (i > 0) text.append(',');
            text.append(selection[i]);
        }
        synchronized (LOCK) {
            writeAtomically(
                    contained(new File(directory(e.id), "selection.json")),
                    text.append(']').toString().getBytes(StandardCharsets.UTF_8));
            e.selectedCount = selection.length;
        }
    }

    private File metadataFile(String id) throws IOException {
        return contained(new File(directory(id), "metadata.json"));
    }

    private static byte[] readBounded(File file, int limit) throws IOException {
        if (file.length() > limit)
            throw new IOException("Package file is too large: " + file.getName());
        try {
            return Streams.read(new FileInputStream(file), limit);
        } catch (Streams.TooLarge grown) {
            throw new IOException("Package file is too large: " + file.getName(), grown);
        }
    }

    /** The published file is never removed before its synced replacement. */
    private void writeAtomically(File target, byte[] bytes) throws IOException {
        contained(new File(target.getPath() + ".tmp"));
        DurableFile.write(target, bytes);
    }

    /**
     * After the download process died: a set it was writing becomes "interrupted" with its
     * bytes, selection and checkpoint kept (resume rehashes the files and continues), and no
     * settled set keeps a transfer speed. Never resumes anything by itself. An interruption is
     * a state, not an error: the row says «Прервано» and offers resume.
     */
    void recoverInterrupted() throws IOException {
        synchronized (LOCK) {
            for (Entry entry : list()) {
                if (entry.corrupt) continue;
                if (Arrays.asList(ACTIVE).contains(entry.state)) {
                    entry.state = "interrupted";
                    entry.rate = 0;
                    entry.stalledSeconds = 0;
                    entry.offline = false;
                    entry.error = "";
                    entry.errorKind = "";
                    save(entry);
                } else if (entry.rate != 0 || entry.stalledSeconds != 0 || entry.offline) {
                    entry.rate = 0;
                    entry.stalledSeconds = 0;
                    entry.offline = false;
                    save(entry);
                }
            }
        }
    }

    /** Deletes the set's directory: its names come from a torrent, so by their bytes (ResourceImporter.deleteTree);
     *  a link in it is removed, never followed. */
    void remove(String id) throws IOException {
        synchronized (LOCK) {
            ResourceImporter.deleteTree(directory(id));
        }
    }

    static void relativePath(String path) throws IOException {
        if (path == null
                || path.isEmpty()
                || path.length() > 4096
                || path.startsWith("/")
                || path.contains("\\")) throw new IOException("Unsafe selected game path");
        for (String component : path.split("/", -1)) safeName(component);
    }

    /** A JSON object of the catalog (set.json, the native metadata.json): malformed or truncated text is an IOException. */
    static JSONObject object(String text) throws IOException {
        try {
            return new JSONObject(text);
        } catch (JSONException damaged) {
            throw new IOException("Invalid catalog JSON", damaged);
        }
    }

    private static String string(JSONObject map, String key) throws IOException {
        Object value = map.opt(key);
        if (!(value instanceof String)) throw new IOException("Invalid record field: " + key);
        return (String) value;
    }

    private static String optionalString(JSONObject map, String key) throws IOException {
        return map.has(key) ? string(map, key) : "";
    }

    private static long optionalNumber(JSONObject map, String key) throws IOException {
        return map.has(key) ? number(map, key) : 0;
    }

    /** An integer member (org.json reads one as Integer or Long, anything with a fraction or exponent as Double). */
    private static long number(JSONObject map, String key) throws IOException {
        Object value = map.opt(key);
        if (!(value instanceof Integer) && !(value instanceof Long))
            throw new IOException("Invalid record number: " + key);
        return ((Number) value).longValue();
    }

    private static JSONArray list(JSONObject map, String key) throws IOException {
        Object value = map.opt(key);
        if (!(value instanceof JSONArray)) throw new IOException("Invalid record list: " + key);
        return (JSONArray) value;
    }
}
