package org.stormengine.port;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileChannel;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;

/**
 * Installs the player's game from a private source set through the engine's
 * archive reader: loose folders, archives, Inno Setup installers with their
 * volumes, multi-volume archives and containers nested up to three levels.
 * Every complete game root is discovered; one is installed directly, several
 * are offered through {@link Choices}. Call on a worker. Publication uses the
 * same recoverable transaction as the folder import.
 */
public final class ArchiveInstaller {
    static final long TOTAL_BYTES = 64L << 30;
    static final long FILE_BYTES = 4L << 30;
    static final long ENTRIES = 400000;
    static final long SECONDS = 3600;
    static final long SCRATCH_BYTES = 32L << 30;
    static final long DEPTH = 3;
    static final long RATIO = 100;
    static final long RESERVE = 64L << 20;
    private static final int MAX_DOCUMENTS = 1000;

    /** A failure the user can act on; {@link #reason} selects the explanation, {@link #detail} names files. */
    public static final class Failure extends IOException {
        public static final String UNSUPPORTED = "unsupported",
                ENCRYPTED = "encrypted",
                DAMAGED = "damaged",
                MEMORY = "memory",
                LIMIT = "limit",
                VOLUMES = "volumes",
                SELECTION = "selection",
                NO_GAME = "no_game",
                TOO_DEEP = "too_deep";
        public final String reason, detail;

        Failure(String reason, String message) {
            this(reason, "", message, null);
        }

        Failure(String reason, String message, Throwable cause) {
            this(reason, "", message, cause);
        }

        Failure(String reason, String detail, String message, Throwable cause) {
            super(message, cause);
            this.reason = reason;
            this.detail = detail == null ? "" : detail;
        }
    }

    /** One user-selected source document (for example from the system file picker). */
    public interface Document {
        /** File name, or a relative path inside a picked folder ("Game/setup.exe"). */
        String name();

        /** Declared size in bytes, or -1 when the provider does not report it. */
        long size();

        InputStream open() throws IOException;

        /** The bytes a ZIP unpacks to, read from its central directory without copying it; -1 when not known up front. */
        default long unpackedBytes() throws IOException {
            return -1;
        }
    }

    /**
     * Which script patch set the engine would apply to a copy: the archive reader runs the engine's own
     * rule (script_patches::identify) on the copy's identify files (engine android/archive/README.md),
     * so the launcher and the engine cannot disagree. Not read (installers, solid archives): unknown.
     */
    public static final class Identification {
        /** The copy's identify files were read (or are absent from it). */
        public final boolean identified;

        /** The set that identifies the copy; both "" when none does or the copy was not read. */
        public final String setId, setVersion;

        /** The versions the APK's script patch sets are for (the layer's sets, in its order). */
        public final List<String> testedVersions;

        Identification(
                boolean identified, String setId, String setVersion, List<String> testedVersions) {
            this.identified = identified;
            this.setId = setId;
            this.setVersion = setVersion;
            this.testedVersions = Collections.unmodifiableList(new ArrayList<>(testedVersions));
        }

        /** A candidate of a discovery document (engine android/archive/README.md, "identified"/"patch_set"/"patch_sets"). */
        static Identification parse(JSONObject candidate, JSONObject document)
                throws IOException, JSONException {
            if (!candidate.has("identified")
                    || !candidate.has("patch_set")
                    || !document.has("patch_sets"))
                throw new IOException("Archive reader returned no script patch identification");
            JSONObject set = candidate.optJSONObject("patch_set");
            JSONArray sets = document.optJSONArray("patch_sets");
            List<String> versions = new ArrayList<>();
            for (int i = 0; sets != null && i < sets.length(); i++) {
                String version = sets.getJSONObject(i).getString("version");
                if (!versions.contains(version)) versions.add(version);
            }
            return new Identification(
                    candidate.getBoolean("identified"),
                    set == null ? "" : set.getString("id"),
                    set == null ? "" : set.getString("version"),
                    versions);
        }
    }

    /** One complete game found in a source (engine.ini, PROGRAM and RESOURCE together). */
    public static final class GameRoot {
        /** Opaque reader id (or a SAF document id for a picked folder). */
        public final String id;

        public final String kind, format, root, title, version, defaultLanguage;
        public final List<String> location, languages, patchMarkers, missingVolumes;
        public final long bytes, files;
        public final Identification identification;

        GameRoot(
                String id,
                String kind,
                String format,
                String root,
                String title,
                String version,
                String defaultLanguage,
                List<String> location,
                List<String> languages,
                List<String> patchMarkers,
                List<String> missingVolumes,
                long bytes,
                long files,
                Identification identification) {
            this.id = id;
            this.kind = kind;
            this.format = format;
            this.root = root;
            this.title = title;
            this.version = version;
            this.defaultLanguage = defaultLanguage;
            this.location = unmodifiable(location);
            this.languages = unmodifiable(languages);
            this.patchMarkers = unmodifiable(patchMarkers);
            this.missingVolumes = unmodifiable(missingVolumes);
            this.bytes = bytes;
            this.files = files;
            this.identification = identification;
        }

        public boolean ready() {
            return missingVolumes.isEmpty();
        }

        /** Where the copy is: containers and the folder inside the innermost one. */
        public String place() {
            StringBuilder text = new StringBuilder();
            for (String item : location) text.append(text.length() == 0 ? "" : " › ").append(item);
            String folder = root.endsWith("/") ? root.substring(0, root.length() - 1) : root;
            if (!folder.isEmpty()) text.append(text.length() == 0 ? "" : " › ").append(folder);
            return text.toString();
        }

        /** A short name: the game's own title, else the folder, else the container. */
        public String name() {
            if (!title.isEmpty()) return title;
            String folder = root.endsWith("/") ? root.substring(0, root.length() - 1) : root;
            if (!folder.isEmpty()) return folder.substring(folder.lastIndexOf('/') + 1);
            return location.isEmpty() ? "" : location.get(location.size() - 1);
        }

        private static List<String> unmodifiable(List<String> values) {
            return Collections.unmodifiableList(new ArrayList<>(values));
        }
    }

    /** Answers the reader needs from the player; both block the worker until answered. */
    public interface Choices {
        /** Pick one of several roots (not-ready roots are shown but cannot be picked). Throw CancelledException to stop. */
        GameRoot choose(List<GameRoot> roots) throws IOException;

        /** Password for an encrypted container; retry is true after a wrong one. Return null to cancel. */
        String password(String container, boolean retry) throws IOException;
    }

    private ArchiveInstaller() {}

    static long[] limits() {
        return new long[] {
            TOTAL_BYTES, FILE_BYTES, ENTRIES, SECONDS, SCRATCH_BYTES, DEPTH, RATIO, RESERVE
        };
    }

    /**
     * A finished download set (github download library): the set's private directory is
     * discovered like copied documents (loose game folders, archives, installers with volumes,
     * nested containers) and the chosen root installed. A loose game folder of the set is moved
     * into place (the set lives on the game's volume), so the install needs no room for a copy;
     * the set is used up by that. Archives and installers are extracted, the set unchanged.
     */
    public static int installDirectory(
            File directory,
            File game,
            File patchLayer,
            ResourceImporter.Monitor monitor,
            Choices choices)
            throws IOException {
        checkCancelled(monitor);
        requireLayer(patchLayer);
        if (directory == null || game == null)
            throw new Failure(
                    Failure.SELECTION, "A downloaded set and the game directory are required");
        File source = directory.getCanonicalFile(), destination = game.getCanonicalFile();
        if (!source.isDirectory() || !source.canRead())
            throw new Failure(Failure.SELECTION, "The downloaded files are missing or unreadable");
        requireSeparateSource(source, destination);
        return ResourceImporter.importExtracted(
                (stage, progress) ->
                        installFrom(
                                source, destination, patchLayer, stage, progress, choices, true),
                destination,
                monitor);
    }

    /**
     * What {@link #discoverGame} found: a ready game root; no game root at all; or a root the
     * import cannot take as it is or that needs the player (missing volumes, a password).
     */
    enum Discovery {
        GAME,
        NONE,
        UNDECIDED
    }

    /**
     * The import's own discovery of a finished download set, without installing: the same
     * reader, limits and readiness rule as {@link #installDirectory}. File names prove
     * nothing; only this says whether the set holds the game. Failures throw. It does not
     * identify the copies (no script patch layer): it only asks whether a game is there.
     */
    static Discovery discoverGame(File directory, File scratch) throws IOException {
        File source = directory.getCanonicalFile();
        if (!source.isDirectory() || !source.canRead())
            throw new IOException("The downloaded files are missing or unreadable");
        try {
            return Discovery.values()[
                    guarded(
                            () -> {
                                ResourceImporter.deleteTree(scratch);
                                if (!scratch.mkdirs())
                                    throw new IOException(
                                            "Cannot create private scratch directory");
                                String[] result =
                                        NativeArchives.discover(
                                                source.getPath(),
                                                scratch.getPath(),
                                                null,
                                                null,
                                                limits(),
                                                null);
                                int status = status(result);
                                JSONObject json = json(result);
                                if (status == ArchiveStatus.ENCRYPTED
                                        || status == ArchiveStatus.ENCRYPTION_UNSUPPORTED)
                                    return Discovery.UNDECIDED.ordinal();
                                if (status == ArchiveStatus.NO_GAME_DATA)
                                    return Discovery.NONE.ordinal();
                                check(status, result, json);
                                if (json == null
                                        || json.optInt("version") != 2
                                        || json.optJSONArray("candidates") == null)
                                    throw new IOException(
                                            "Archive reader returned no discovery result");
                                List<GameRoot> roots = roots(json);
                                for (GameRoot root : roots)
                                    if (root.ready()) return Discovery.GAME.ordinal();
                                return (roots.isEmpty() ? Discovery.NONE : Discovery.UNDECIDED)
                                        .ordinal();
                            })];
        } finally {
            ResourceImporter.deleteTree(scratch);
        }
    }

    /**
     * The installed game as the import describes a found copy (title, version, languages, size, patches) and the
     * script patch set the engine applies to it (identified against `patchLayer`, the installed layer).
     */
    static GameRoot describeInstalled(File game, File scratch, File patchLayer) throws IOException {
        requireLayer(patchLayer);
        try {
            GameRoot[] found = {null};
            guarded(
                    () -> {
                        ResourceImporter.deleteTree(scratch);
                        if (!scratch.mkdirs())
                            throw new IOException("Cannot create private scratch directory");
                        String[] result =
                                NativeArchives.discover(
                                        game.getCanonicalPath(),
                                        scratch.getPath(),
                                        null,
                                        patchLayer.getPath(),
                                        limits(),
                                        null);
                        JSONObject json = json(result);
                        check(status(result), result, json);
                        List<GameRoot> roots =
                                json == null ? Collections.<GameRoot>emptyList() : roots(json);
                        if (roots.size() != 1 || !roots.get(0).root.isEmpty())
                            throw new IOException(
                                    "The installed game is not one game root: " + roots.size());
                        found[0] = roots.get(0);
                        return 0;
                    });
            return found[0];
        } finally {
            ResourceImporter.deleteTree(scratch);
        }
    }

    /**
     * Copies picked documents (any mix of archives, installers and their volumes)
     * into private input storage owned by the import transaction, discovers the
     * game roots in them and installs one. Copies and scratch are deleted
     * afterwards and by interrupted-import recovery.
     */
    public static int installDocuments(
            List<? extends Document> documents,
            File game,
            File patchLayer,
            ResourceImporter.Monitor monitor,
            Choices choices)
            throws IOException {
        checkCancelled(monitor);
        requireLayer(patchLayer);
        if (documents == null || documents.isEmpty() || game == null)
            throw new Failure(Failure.SELECTION, "Choose files to import first");
        if (documents.size() > MAX_DOCUMENTS)
            throw new Failure(Failure.SELECTION, "Too many documents selected");
        HashSet<String> unique = new HashSet<>();
        long total = 0, unpacked = 0;
        for (Document document : documents) {
            requirePath(document.name());
            if (!unique.add(document.name().toLowerCase(Locale.ROOT)))
                throw new Failure(
                        Failure.SELECTION,
                        document.name(),
                        "Two selected files have the same name: " + document.name(),
                        null);
            total += Math.max(0, document.size());
            unpacked += Math.max(0, document.unpackedBytes());
        }
        File destination = game.getCanonicalFile();
        // The copies stay until the game is unpacked from them: both must fit before anything is
        // copied. What other formats unpack to is known only after reading them (checked again
        // before extraction).
        File parent = destination.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create the game directory's parent");
        requireSpace(parent, total + unpacked, "copy and unpack the selected files");
        final long declared = total;
        return ResourceImporter.importExtracted(
                (stage, progress) -> {
                    File input = ResourceImporter.input(destination);
                    try {
                        if (!input.mkdir())
                            throw new IOException("Cannot create private archive input directory");
                        if (progress != null)
                            progress.stage(ResourceImporter.Stage.COPYING, declared);
                        long[] copied = {0};
                        for (Document document : documents)
                            copy(document, input, copied, declared, progress);
                        return installFrom(
                                input, destination, patchLayer, stage, progress, choices, false);
                    } finally {
                        ResourceImporter.deleteTree(
                                input); // Free the copies before publication needs the space.
                    }
                },
                destination,
                monitor);
    }

    /**
     * Discovers the roots in a private source (directory or one file), identified against the script patch layer for the
     * choice, and extracts one into the empty stage; `move`: a loose root moves.
     */
    static int installFrom(
            File source,
            File game,
            File patchLayer,
            File stage,
            ResourceImporter.Monitor monitor,
            Choices choices,
            boolean move)
            throws IOException {
        File scratch = ResourceImporter.scratch(game);
        try {
            return guarded(
                    () -> {
                        String[] password = {null};
                        JSONObject document =
                                discover(source, scratch, patchLayer, password, monitor, choices);
                        List<GameRoot> roots = roots(document);
                        GameRoot chosen = choose(roots, document, monitor, choices);
                        if (move && chosen.kind.equals("folder") && chosen.location.isEmpty())
                            return ResourceImporter.moveInto(
                                    new File(source, chosen.root), game, stage, monitor);
                        return extract(source, scratch, chosen, stage, password, monitor, choices);
                    });
        } finally {
            ResourceImporter.deleteTree(scratch);
        }
    }

    private static JSONObject discover(
            File source,
            File scratch,
            File patchLayer,
            String[] password,
            ResourceImporter.Monitor monitor,
            Choices choices)
            throws IOException, JSONException {
        while (true) {
            checkCancelled(monitor);
            ResourceImporter.deleteTree(scratch);
            if (!scratch.mkdir()) throw new IOException("Cannot create private scratch directory");
            if (monitor != null) monitor.stage(ResourceImporter.Stage.READING, 0);
            String[] result =
                    NativeArchives.discover(
                            source.getPath(),
                            scratch.getPath(),
                            password[0],
                            patchLayer.getPath(),
                            limits(),
                            progress(monitor, 0));
            int status = status(result);
            JSONObject json = json(result);
            if (status == ArchiveStatus.ENCRYPTED) {
                password[0] = askPassword(location(json), password[0] != null, choices);
                continue;
            }
            check(status, result, json);
            if (json == null
                    || json.optInt("version") != 2
                    || json.optJSONArray("candidates") == null)
                throw new IOException("Archive reader returned no discovery result");
            return json;
        }
    }

    /** The container a failure document's {"encrypted":{"location":[...]}} names. */
    private static String location(JSONObject json) throws IOException, JSONException {
        if (json == null || !json.has("encrypted"))
            throw new IOException("Archive reader reported encryption without naming the archive");
        return join(strings(json.getJSONObject("encrypted").getJSONArray("location")), " › ");
    }

    private static String askPassword(String container, boolean retry, Choices choices)
            throws IOException {
        String answer = choices.password(container, retry);
        if (answer == null) throw new ResourceImporter.CancelledException();
        return answer;
    }

    static List<GameRoot> roots(JSONObject document) throws IOException, JSONException {
        List<GameRoot> roots = new ArrayList<>();
        JSONArray candidates = document.getJSONArray("candidates");
        for (int i = 0; i < candidates.length(); i++) {
            JSONObject c = candidates.getJSONObject(i);
            long bytes = c.getLong("unpacked_bytes"), files = c.getLong("file_count");
            if (bytes < 0 || files < 1)
                throw new IOException("Archive reader returned an invalid game root");
            roots.add(
                    new GameRoot(
                            c.getString("id"),
                            c.getString("kind"),
                            c.getString("format"),
                            c.getString("root"),
                            c.optString("title"),
                            c.optString("version"),
                            c.optString("default_language"),
                            strings(c.getJSONArray("location")),
                            strings(c.getJSONArray("languages")),
                            strings(c.getJSONArray("patch_markers")),
                            strings(c.getJSONArray("missing_volumes")),
                            bytes,
                            files,
                            Identification.parse(c, document)));
        }
        return roots;
    }

    /** One ready root is installed directly; otherwise the player chooses (or learns what is missing). */
    static GameRoot choose(
            List<GameRoot> roots,
            JSONObject document,
            ResourceImporter.Monitor monitor,
            Choices choices)
            throws IOException, JSONException {
        List<GameRoot> ready = new ArrayList<>();
        for (GameRoot root : roots) if (root.ready()) ready.add(root);
        if (ready.isEmpty()) throw explainNothingReady(roots, document);
        if (roots.size() == 1) return roots.get(0);
        checkCancelled(monitor);
        if (monitor != null) monitor.stage(ResourceImporter.Stage.CHOOSING, 0);
        GameRoot chosen = choices.choose(roots);
        if (chosen == null || !roots.contains(chosen) || !chosen.ready())
            throw new IOException("No ready game root was chosen");
        return chosen;
    }

    /** The most specific reason nothing can be installed. */
    static Failure explainNothingReady(List<GameRoot> roots, JSONObject document)
            throws IOException, JSONException {
        if (!roots.isEmpty()) {
            List<String> missing = new ArrayList<>();
            for (GameRoot root : roots)
                for (String volume : root.missingVolumes)
                    if (!missing.contains(volume)) missing.add(volume);
            return new Failure(
                    Failure.VOLUMES,
                    join(missing, ", "),
                    "Installer volumes are missing: " + missing,
                    null);
        }
        JSONArray issues = document.getJSONArray("issues");
        List<String> missing = new ArrayList<>(),
                unsupported = new ArrayList<>(),
                damaged = new ArrayList<>(),
                deep = new ArrayList<>();
        for (int i = 0; i < issues.length(); i++) {
            JSONObject issue = issues.getJSONObject(i);
            String where = join(strings(issue.getJSONArray("location")), " › ");
            switch (issue.getInt("status")) {
                case ArchiveStatus.MISSING_VOLUME:
                    missing.addAll(strings(issue.getJSONArray("missing_volumes")));
                    break;
                case ArchiveStatus.UNSUPPORTED:
                    unsupported.add(where);
                    break;
                case ArchiveStatus.LIMIT:
                    deep.add(where);
                    break;
                case ArchiveStatus.UNSUPPORTED_EXECUTABLE:
                    break; // An unrelated executable (menu.exe, autorun) is not a reason on its
                // own.
                default:
                    damaged.add(where + ": " + issue.optString("error"));
                    break;
            }
        }
        if (!missing.isEmpty())
            return new Failure(
                    Failure.VOLUMES, join(missing, ", "), "Volumes are missing: " + missing, null);
        if (!deep.isEmpty())
            return new Failure(
                    Failure.TOO_DEEP,
                    join(deep, ", "),
                    "Archives are nested too deeply: " + deep,
                    null);
        if (!damaged.isEmpty())
            return new Failure(
                    Failure.DAMAGED, join(damaged, "\n"), "Damaged sources: " + damaged, null);
        if (!unsupported.isEmpty())
            return new Failure(
                    Failure.UNSUPPORTED,
                    join(unsupported, ", "),
                    "Unsupported formats: " + unsupported,
                    null);
        List<String> partial = new ArrayList<>();
        JSONArray partials = document.getJSONArray("partial_roots");
        for (int i = 0; i < partials.length(); i++) {
            JSONObject p = partials.getJSONObject(i);
            List<String> lacks = new ArrayList<>();
            if (!p.optBoolean("has_engine_ini")) lacks.add("engine.ini");
            if (!p.optBoolean("has_program")) lacks.add("PROGRAM");
            if (!p.optBoolean("has_resource")) lacks.add("RESOURCE");
            List<String> place = strings(p.getJSONArray("location"));
            String root = p.getString("root");
            if (!root.isEmpty()) place.add(root.substring(0, root.length() - 1));
            partial.add(join(place, " › ") + " − " + join(lacks, ", "));
        }
        return new Failure(
                Failure.NO_GAME,
                join(partial, "\n"),
                "No complete game root found" + (partial.isEmpty() ? "" : ": " + partial),
                null);
    }

    private static int extract(
            File source,
            File scratch,
            GameRoot chosen,
            File stage,
            String[] password,
            ResourceImporter.Monitor monitor,
            Choices choices)
            throws IOException, JSONException {
        while (true) {
            checkCancelled(monitor);
            requireSpace(stage, chosen.bytes, "install the game files");
            if (monitor != null) monitor.stage(ResourceImporter.Stage.INSTALLING, chosen.bytes);
            String[] result =
                    NativeArchives.extractRoot(
                            source.getPath(),
                            scratch.getPath(),
                            password[0],
                            chosen.id,
                            stage.getPath(),
                            limits(),
                            progress(monitor, chosen.bytes));
            int status = status(result);
            if (status == ArchiveStatus.ENCRYPTED) {
                // A wrong ZIP password is only detected while decrypting file data.
                clear(stage);
                password[0] =
                        askPassword(join(chosen.location, " › "), password[0] != null, choices);
                continue;
            }
            check(status, result, json(result));
            checkCancelled(monitor);
            long files = countFiles(stage, monitor);
            if (files != chosen.files)
                throw new IOException(
                        "Installed "
                                + files
                                + " files instead of "
                                + chosen.files
                                + "; read the source again");
            return (int) files;
        }
    }

    private static NativeArchives.Progress progress(ResourceImporter.Monitor monitor, long total) {
        int[] reported = {0};
        return (stage, path, bytes, totalBytes, files) -> {
            if (cancelled(monitor)) return true; // JNI contract: true means cancel.
            if (monitor != null) {
                if (stage == 2 && reported[0] != 2)
                    monitor.stage(ResourceImporter.Stage.NESTED, totalBytes);
                if (stage == 1 && reported[0] == 2)
                    monitor.stage(ResourceImporter.Stage.READING, 0);
                reported[0] = stage;
                monitor.progress(
                        (int) Math.min(Integer.MAX_VALUE, Math.max(0, files)), Math.max(0, bytes));
            }
            return cancelled(monitor);
        };
    }

    private static long countFiles(File directory, ResourceImporter.Monitor monitor)
            throws IOException {
        long count = 0;
        ArrayDeque<File> pending = new ArrayDeque<>();
        pending.add(directory);
        while (!pending.isEmpty()) {
            checkCancelled(monitor);
            File[] children = pending.removeFirst().listFiles();
            if (children == null) throw new IOException("Cannot verify the installed files");
            for (File child : children) {
                if (child.isDirectory()) pending.add(child);
                else if (child.isFile()) count++;
            }
        }
        return count;
    }

    private static void clear(File directory) throws IOException {
        ResourceImporter.deleteTree(directory);
        if (!directory.mkdir()) throw new IOException("Cannot clear the staging directory");
    }

    private static void requireSpace(File directory, long bytes, String step) throws IOException {
        long available = directory.getUsableSpace(), needed = bytes + RESERVE;
        if (available < needed)
            throw new ResourceImporter.StorageException(
                    "Not enough free storage to " + step, needed, available);
    }

    private interface NativeStep {
        int run() throws IOException, JSONException;
    }

    private static int guarded(NativeStep step) throws IOException {
        try {
            return step.run();
        } catch (JSONException error) {
            throw new IOException("Archive reader returned an invalid result", error);
        } catch (OutOfMemoryError error) {
            throw new Failure(
                    Failure.MEMORY,
                    "Not enough memory to read this archive. Close other apps or unpack it on a PC.",
                    error);
        }
    }

    /**
     * The sum of the uncompressed sizes in a ZIP's central directory (ZIP64 included), or -1 when the file is no
     * single-volume ZIP. Reads only the end of the file and the directory.
     */
    static long zipUnpackedBytes(FileChannel file) throws IOException {
        long sum = 0;
        try {
            for (ZipDirectory.Entry entry : ZipDirectory.entries(file, ENTRIES)) {
                if (entry.size < 0) return -1;
                sum += entry.size;
            }
        } catch (ZipDirectory.Malformed notZip) {
            return -1;
        }
        return sum;
    }

    private static void copy(
            Document document,
            File input,
            long[] copied,
            long total,
            ResourceImporter.Monitor monitor)
            throws IOException {
        checkCancelled(monitor);
        File target = new File(input, document.name());
        File parent = target.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create private archive input directory");
        long declared = document.size();
        if (declared > TOTAL_BYTES)
            throw new Failure(Failure.LIMIT, "Selected document exceeds the import limit");
        try (InputStream in = document.open();
                FileOutputStream out = new FileOutputStream(target)) {
            if (in == null)
                throw new IOException("Cannot open selected document " + document.name());
            long written;
            try {
                written =
                        Streams.copy(
                                in,
                                out,
                                TOTAL_BYTES,
                                count -> {
                                    checkCancelled(monitor);
                                    // Before every chunk: a write then runs out of space only when
                                    // another
                                    // app fills the volume meanwhile, and fails as the I/O error it
                                    // is.
                                    if (input.getUsableSpace() < RESERVE + count)
                                        throw new ResourceImporter.StorageException(
                                                "Not enough free storage to copy "
                                                        + document.name(),
                                                Math.max(total, copied[0]) - copied[0] + RESERVE,
                                                input.getUsableSpace());
                                    copied[0] += count;
                                    if (monitor != null) {
                                        monitor.copying(copied[0], Math.max(total, copied[0]));
                                        monitor.progress(0, copied[0]);
                                    }
                                });
            } catch (Streams.TooLarge grown) {
                throw new Failure(Failure.LIMIT, "Selected document exceeds the import limit");
            }
            if (declared >= 0 && written != declared)
                throw new IOException(
                        "Selected document "
                                + document.name()
                                + " changed or was truncated while copying");
        }
    }

    private static boolean cancelled(ResourceImporter.Monitor monitor) {
        return Thread.currentThread().isInterrupted() || (monitor != null && monitor.cancelled());
    }

    private static void checkCancelled(ResourceImporter.Monitor monitor)
            throws ResourceImporter.CancelledException {
        if (cancelled(monitor)) throw new ResourceImporter.CancelledException();
    }

    private static void requireName(String name) throws IOException {
        if (name == null
                || name.isEmpty()
                || name.length() > 255
                || name.equals(".")
                || name.equals("..")
                || name.endsWith(".")
                || name.endsWith(" "))
            throw new Failure(Failure.SELECTION, String.valueOf(name), "Invalid file name", null);
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':')
                throw new Failure(Failure.SELECTION, name, "Unsafe file name", null);
        }
    }

    /** A relative path of safe names (documents picked from a folder keep their sub-folders). */
    private static void requirePath(String path) throws IOException {
        if (path == null
                || path.isEmpty()
                || path.startsWith("/")
                || path.endsWith("/")
                || path.length() > 1024)
            throw new Failure(Failure.SELECTION, String.valueOf(path), "Invalid file name", null);
        for (String part : path.split("/", -1)) requireName(part);
    }

    /** The installed script patch layer (MobileResources.scriptPatches): copies are identified against it, never without it. */
    private static void requireLayer(File patchLayer) throws IOException {
        if (patchLayer == null || !patchLayer.isDirectory())
            throw new IOException("The script patch layer is not installed: " + patchLayer);
    }

    private static void requireSeparateSource(File source, File game) throws IOException {
        if (inside(source, game) || inside(game, source))
            throw new IOException("Keep the downloaded source outside the active game directory");
        for (String suffix :
                new String[] {
                    "stage", "backup", "pending", "journal-temp", "lock", "input", "scratch"
                }) {
            File reserved = ResourceImporter.sibling(game, suffix).getCanonicalFile();
            if (inside(source, reserved) || inside(reserved, source))
                throw new IOException(
                        "Downloaded source overlaps the import transaction directory");
        }
    }

    private static boolean inside(File file, File directory) {
        return file.equals(directory)
                || file.getPath().startsWith(directory.getPath() + File.separator);
    }

    private static List<String> strings(JSONArray array) throws JSONException {
        List<String> values = new ArrayList<>();
        for (int i = 0; i < array.length(); i++) values.add(array.getString(i));
        return values;
    }

    private static String join(List<String> values, String separator) {
        StringBuilder text = new StringBuilder();
        for (String value : values) text.append(text.length() == 0 ? "" : separator).append(value);
        return text.toString();
    }

    private static int status(String[] result) throws IOException {
        if (result == null || result.length != 3 || result[0] == null)
            throw new IOException("Archive reader returned no valid result");
        try {
            return Integer.parseInt(result[0]);
        } catch (NumberFormatException error) {
            throw new IOException("Archive reader returned an invalid status", error);
        }
    }

    private static JSONObject json(String[] result) throws JSONException {
        return result[2] == null || result[2].isEmpty() ? null : new JSONObject(result[2]);
    }

    private static void check(int status, String[] result, JSONObject json)
            throws IOException, JSONException {
        if (status == ArchiveStatus.OK) return;
        String detail = result[1] == null ? "" : result[1];
        String suffix = detail.isEmpty() ? "" : ": " + detail;
        switch (status) {
            case ArchiveStatus.CANCELLED:
                throw new ResourceImporter.CancelledException();
            case ArchiveStatus.NO_SPACE:
                {
                    JSONObject space = json == null ? null : json.optJSONObject("no_space");
                    throw new ResourceImporter.StorageException(
                            "Not enough free storage" + suffix,
                            space == null ? 0 : space.getLong("needed_bytes"),
                            space == null ? 0 : space.getLong("available_bytes"));
                }
            case ArchiveStatus.NO_GAME_DATA:
                throw new ResourceImporter.MissingGameRootException();
            case ArchiveStatus.IO:
                throw new IOException("Cannot read the archive or write its game data" + suffix);
            case ArchiveStatus.UNSUPPORTED:
            case ArchiveStatus.UNSUPPORTED_EXECUTABLE:
                throw new Failure(
                        Failure.UNSUPPORTED, "", "Unsupported archive or installer" + suffix, null);
            case ArchiveStatus.ENCRYPTED:
            case ArchiveStatus.ENCRYPTION_UNSUPPORTED:
                {
                    String container = json != null && json.has("encrypted") ? location(json) : "";
                    throw new Failure(
                            Failure.ENCRYPTED,
                            container,
                            "Encrypted archive this app cannot open" + suffix,
                            null);
                }
            case ArchiveStatus.INVALID:
                throw new Failure(
                        Failure.DAMAGED,
                        detail,
                        "Archive is damaged or contains unsafe or conflicting files" + suffix,
                        null);
            case ArchiveStatus.LIMIT:
                throw new Failure(
                        Failure.LIMIT, detail, "Archive exceeded an import limit" + suffix, null);
            case ArchiveStatus.MEMORY:
                throw new Failure(
                        Failure.MEMORY, "Not enough memory to read this archive" + suffix);
            case ArchiveStatus.MISSING_VOLUME:
                {
                    List<String> names =
                            json == null || !json.has("missing_volumes")
                                    ? Collections.<String>emptyList()
                                    : strings(json.getJSONArray("missing_volumes"));
                    throw new Failure(
                            Failure.VOLUMES,
                            join(names, ", "),
                            "Required volumes are missing" + suffix,
                            null);
                }
            default:
                throw new IOException("Archive reader failed (status " + status + ")" + suffix);
        }
    }
}
