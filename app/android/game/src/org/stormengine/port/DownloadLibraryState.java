package org.stormengine.port;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;

/**
 * The download library as screen state (github flavor). One immutable {@link Row} per set:
 * its progress, its health (error kind, stall) and the verbs it accepts now. A screen renders
 * rows and sends the verbs back; it never derives states itself:
 * <ul>
 * <li>{@code pause}, {@code resume} (also the retry of an error or an interrupted set),
 *     {@code delete}: {@link TorrentDownloadService#command};</li>
 * <li>{@code choose}: offer {@link Row#candidates} (or every file, {@link #allFiles}), then
 *     {@link TorrentDownloadService#START} with the chosen indices;</li>
 * <li>{@code import}: {@link TorrentDownloadService#importSet}, which hands the finished files
 *     to the launcher's ImportCoordinator;</li>
 * <li>{@code details}: {@link #metadata} lists the files.</li>
 * </ul>
 * Native metadata is parsed once per change and kept as a snapshot, so a refresh every
 * second does not re-read large file lists. Not thread-safe: one reader thread owns it.
 */
final class DownloadLibraryState {
    /** Seconds without new data, while metadata or payload should arrive, after which a set is stalled. */
    static final long STALL_SECONDS = 120;

    /** One coherent set of files the metadata names as a possible game (file names only, never verified). */
    static final class Candidate {
        final String id, kind, root;

        /** Indices to download: the candidate's files plus its volume hints. */
        final int[] files;

        final long bytes;

        Candidate(String id, String kind, String root, int[] files, long bytes) {
            this.id = id;
            this.kind = kind;
            this.root = root;
            this.files = files;
            this.bytes = bytes;
        }
    }

    /** Parsed native metadata of a set (metadata version 2). */
    static final class Metadata {
        final String name;
        final long totalBytes;
        final List<String> paths;
        final long[] sizes;
        final boolean[] selectable;
        final List<Candidate> candidates;

        Metadata(
                String name,
                long totalBytes,
                List<String> paths,
                long[] sizes,
                boolean[] selectable,
                List<Candidate> candidates) {
            this.name = name;
            this.totalBytes = totalBytes;
            this.paths = Collections.unmodifiableList(paths);
            this.sizes = sizes;
            this.selectable = selectable;
            this.candidates = Collections.unmodifiableList(candidates);
        }
    }

    static final class Row {
        final String id, kind, name;

        /** copying, inspecting, metadata, awaiting-selection, checking, downloading, pausing, paused, complete, interrupted, error. */
        final String state;

        final long downloaded, wanted, rate, peers, seeds, stalledSeconds;

        /** True while metadata or payload should arrive but nothing did for {@link #STALL_SECONDS}. */
        final boolean stalled;

        /** Active while the phone has no network: that, not the peers, is why nothing moves. */
        final boolean offline;

        /** Share of the hash check in state checking, in thousandths. */
        final long checkedPermille;

        /** Finished and checkpointed: the files can be imported. */
        final boolean ready;

        /** {@code error}: the message (log only); errorKind: what the player is told (DownloadProblem kinds, or failed). */
        final String error, errorKind;

        /** A non-fatal network problem (tracker, listen socket) that explains a stall, or empty. */
        final String networkError;

        /** The main verb ("" when none) and the rare ones. */
        final String main;

        final List<String> menu;
        final List<Candidate> candidates;

        /**
         * What the import's discovery found in the finished files ({@link SetDiscovery}): game, none,
         * undecided, or checking until it ran; "" before the set is ready. Never from file names.
         */
        final String discovered;

        final boolean corrupt;

        Row(GameDownloads.Entry e, Metadata metadata, boolean importRunning) {
            id = e.id;
            kind = e.kind;
            name = metadata != null && !metadata.name.isEmpty() ? metadata.name : e.name;
            state = publicState(e);
            corrupt = e.corrupt;
            downloaded = e.downloaded;
            wanted = e.wanted;
            rate = active(state) ? e.rate : 0;
            peers = e.peers;
            seeds = e.seeds;
            stalledSeconds = active(state) ? e.stalledSeconds : 0;
            offline = active(state) && e.offline;
            stalled =
                    ("metadata".equals(state) || "downloading".equals(state))
                            && stalledSeconds >= STALL_SECONDS
                            && !offline;
            checkedPermille = "checking".equals(state) ? e.checkedPermille : 0;
            ready = GameDownloads.ready(e);
            error = e.error;
            errorKind = e.errorKind;
            networkError = e.networkError;
            candidates =
                    metadata == null ? Collections.<Candidate>emptyList() : metadata.candidates;
            discovered = !ready ? "" : e.discovered.isEmpty() ? "checking" : e.discovered;
            List<String> verbs = new ArrayList<>();
            if (!corrupt) {
                if (active(state)) {
                    if (!"pausing".equals(state)) verbs.add("pause");
                } else if (!importRunning) {
                    if (ready) verbs.add("import");
                    // Awaiting a choice: choose from the metadata, or fetch it again when it cannot
                    // be read.
                    if ("awaiting-selection".equals(state))
                        verbs.add(metadata != null ? "choose" : "resume");
                    else if (!ready) verbs.add("resume");
                    if (metadata != null && !"awaiting-selection".equals(state))
                        verbs.add("choose");
                }
                if (metadata != null) verbs.add("details");
            }
            if (!active(state) && !importRunning) verbs.add("delete");
            main =
                    verbs.isEmpty()
                                    || "details".equals(verbs.get(0))
                                    || "delete".equals(verbs.get(0))
                            ? ""
                            : verbs.get(0);
            if (!main.isEmpty()) verbs.remove(0);
            menu = Collections.unmodifiableList(verbs);
        }
    }

    /** Reads a set's kept metadata text (GameDownloads#metadata). */
    interface MetadataText {
        String of(GameDownloads.Entry e) throws IOException;
    }

    private final MetadataText text;

    DownloadLibraryState(MetadataText text) {
        this.text = text;
    }

    private static final class Snapshot {
        final long stamp;
        final Metadata metadata;

        Snapshot(long s, Metadata m) {
            stamp = s;
            metadata = m;
        }
    }

    private final Map<String, Snapshot> snapshots = new HashMap<>();

    /** Rows in catalog order; importRunning hides the verbs that would change files during a game import. */
    List<Row> rows(List<GameDownloads.Entry> entries, boolean importRunning) {
        List<Row> rows = new ArrayList<>();
        Map<String, Snapshot> kept = new HashMap<>();
        for (GameDownloads.Entry e : entries) {
            Metadata metadata = null;
            if (!e.corrupt && e.hasMetadata) {
                try {
                    metadata = metadata(e);
                    kept.put(e.id, snapshots.get(e.id));
                } catch (IOException invalid) {
                    /* The service rewrites metadata on its next poll; shown without file choice. */
                }
            }
            rows.add(new Row(e, metadata, importRunning));
        }
        snapshots.keySet().retainAll(kept.keySet());
        return rows;
    }

    /** The set's parsed metadata snapshot; IOException for metadata older than version 2 (resume refreshes it). */
    Metadata metadata(GameDownloads.Entry e) throws IOException {
        Snapshot snapshot = snapshots.get(e.id);
        if (snapshot != null && snapshot.stamp == e.metadataStamp) return snapshot.metadata;
        Metadata parsed = parse(text.of(e));
        snapshots.put(e.id, new Snapshot(e.metadataStamp, parsed));
        return parsed;
    }

    /** Every selectable file of the set (the "all files" choice). */
    int[] allFiles(GameDownloads.Entry e) throws IOException {
        Metadata metadata = metadata(e);
        int count = 0;
        for (boolean value : metadata.selectable) if (value) count++;
        int[] result = new int[count];
        for (int i = 0, at = 0; i < metadata.selectable.length; i++)
            if (metadata.selectable[i]) result[at++] = i;
        return result;
    }

    static boolean active(String state) {
        return Arrays.asList(GameDownloads.ACTIVE).contains(state);
    }

    /** The state a row shows; a state this version does not know (a record written by a newer one) is shown as
     *  awaiting a choice or interrupted. */
    static String publicState(GameDownloads.Entry e) {
        if (e.corrupt) return "error";
        if (GameDownloads.ready(e)) return "complete";
        switch (e.state) {
            case "copying":
            case "inspecting":
            case "metadata":
            case "awaiting-selection":
            case "checking":
            case "downloading":
            case "pausing":
            case "paused":
            case "interrupted":
            case "error":
                return e.state;
            default:
                return e.hasMetadata ? "awaiting-selection" : "interrupted";
        }
    }

    @SuppressWarnings("unchecked")
    static Metadata parse(String json) throws IOException {
        Map<String, Object> root = GameDownloads.parse(json);
        if (!Long.valueOf(2).equals(root.get("metadataVersion")))
            throw new IOException("Unsupported torrent metadata version");
        List<Object> files = (List<Object>) list(root, "files");
        List<String> paths = new ArrayList<>();
        long[] sizes = new long[files.size()];
        boolean[] selectable = new boolean[files.size()];
        for (int i = 0; i < files.size(); i++) {
            Map<String, Object> file = (Map<String, Object>) files.get(i);
            if (number(file, "index") != i)
                throw new IOException("Torrent metadata files are out of order");
            paths.add((String) file.get("path"));
            sizes[i] = number(file, "bytes");
            selectable[i] = Boolean.TRUE.equals(file.get("selectable"));
        }
        List<Candidate> candidates = new ArrayList<>();
        for (Object item : list(root, "candidates")) {
            Map<String, Object> c = (Map<String, Object>) item;
            LinkedHashSet<Integer> chosen = new LinkedHashSet<>();
            for (String key : new String[] {"indices", "companionIndices"})
                for (Object index : list(c, key)) {
                    if (!(index instanceof Long)
                            || (Long) index < 0
                            || (Long) index >= files.size())
                        throw new IOException("Invalid candidate file index");
                    chosen.add(((Long) index).intValue());
                }
            int[] indices = new int[chosen.size()];
            long bytes = 0;
            int at = 0;
            for (int index : chosen) {
                indices[at++] = index;
                bytes += sizes[index];
            }
            candidates.add(
                    new Candidate(
                            (String) c.get("id"),
                            (String) c.get("kind"),
                            (String) c.get("root"),
                            indices,
                            bytes));
        }
        Object name = root.get("name");
        return new Metadata(
                name instanceof String ? (String) name : "",
                number(root, "totalBytes"),
                paths,
                sizes,
                selectable,
                candidates);
    }

    private static List<?> list(Map<String, Object> map, String key) throws IOException {
        Object value = map.get(key);
        if (!(value instanceof List)) throw new IOException("Invalid torrent metadata: " + key);
        return (List<?>) value;
    }

    private static long number(Map<String, Object> map, String key) throws IOException {
        Object value = map.get(key);
        if (!(value instanceof Long)) throw new IOException("Invalid torrent metadata: " + key);
        return (Long) value;
    }
}
