package org.stormengine.port;

import android.content.res.AssetManager;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/**
 * Runs the real ArchiveInstaller and ResourceImporter transaction. The reader is a
 * scripted boundary that returns the engine's documented discovery/extraction
 * results (engine android/archive/README.md); native decoding is tested in the engine.
 */
public final class ArchiveInstallerCheck {
    interface Checked {
        void run() throws Exception;
    }

    static File root, game;
    static byte[] old;

    static String candidate(String id, String root, String location, String missing) {
        return "{\"id\":\""
                + id
                + "\",\"kind\":\"archive\",\"format\":\"zip\",\"location\":["
                + location
                + "],\"root\":\""
                + root
                + "\",\"title\":\"Корсары: Каждому Свое\",\"version\":\"ver. 1.7.3\",\"languages\":[\"english\",\"russian\"],\"default_language\":\"russian\",\"file_count\":3,\"unpacked_bytes\":30,\"patch_markers\":[\"Patch_1.3.txt\"],\"ready\":"
                + (missing.isEmpty() ? "true" : "false")
                + ",\"missing_volumes\":["
                + missing
                + "],\"identified\":true,\"patch_set\":null}";
    }

    static String discovery(String candidates, String issues, String partial) {
        return "{\"version\":2,\"candidates\":["
                + candidates
                + "],\"patch_sets\":"
                + PATCH_SETS
                + ",\"issues\":["
                + issues
                + "],\"partial_roots\":["
                + partial
                + "],\"scratch_bytes\":0}";
    }

    static final String PATCH_SETS =
            "[{\"id\":\"teho-1.7.3-x64\",\"game\":\"G\",\"version\":\"ver. 1.7.3 (26.11.21) x64\",\"identify\":[\"program/_lsc_on_off.h\"]}]";

    /** The APK's assets every identifying discovery is given: the reader takes the script patch layer from them. */
    static final AssetManager assets = new AssetManager();

    static String[] ok(String json) {
        return new String[] {"0", "", json};
    }

    static byte[] hash(File directory) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (java.util.stream.Stream<Path> paths = Files.walk(directory.toPath())) {
            for (Path path : (Iterable<Path>) paths.sorted()::iterator) {
                digest.update(
                        directory
                                .toPath()
                                .relativize(path)
                                .toString()
                                .getBytes(StandardCharsets.UTF_8));
                if (Files.isRegularFile(path)) digest.update(Files.readAllBytes(path));
            }
        }
        return digest.digest();
    }

    static void oldUnchanged() throws Exception {
        ResourceImporter.requirePublished(game);
        ResourceImporter.validate(game);
        if (!Arrays.equals(old, hash(game)))
            throw new AssertionError("Live install byte hash changed");
        for (String sibling : new String[] {"stage", "input", "scratch"})
            if (new File(root, ".game-import-" + sibling).exists())
                throw new AssertionError("Transaction directory kept: " + sibling);
    }

    static IOException rejected(Checked action) throws Exception {
        try {
            action.run();
            throw new AssertionError("Expected installation failure");
        } catch (IOException expected) {
            oldUnchanged();
            return expected;
        }
    }

    static File source(File root) throws IOException {
        File file = new File(root, "not-a.zip");
        Files.write(file.toPath(), new byte[4096]);
        return file;
    }

    static String reason(IOException error) {
        return error instanceof ArchiveInstaller.Failure
                ? ((ArchiveInstaller.Failure) error).reason
                : error.getClass().getSimpleName();
    }

    static String detail(IOException error) {
        return error instanceof ArchiveInstaller.Failure
                ? ((ArchiveInstaller.Failure) error).detail
                : "";
    }

    static final class Doc implements ArchiveInstaller.Document {
        final String name;
        final byte[] data;
        long size, unpacked = -1;
        int opens;

        Doc(String name, int... bytes) {
            this.name = name;
            data = new byte[bytes.length];
            for (int i = 0; i < bytes.length; i++) data[i] = (byte) bytes[i];
            size = data.length;
        }

        public String name() {
            return name;
        }

        public long size() {
            return size;
        }

        public InputStream open() {
            opens++;
            return new ByteArrayInputStream(data);
        }

        public long unpackedBytes() {
            return unpacked;
        }
    }

    /** Records the questions; answers come from the scripted queues. */
    static final class Answers implements ArchiveInstaller.Choices {
        final Deque<Object> choices = new ArrayDeque<>(),
                passwords = new LinkedList<>(); // null answers cancel
        List<ArchiveInstaller.GameRoot> offered;
        final List<String> asked = new ArrayList<>();

        public ArchiveInstaller.GameRoot choose(List<ArchiveInstaller.GameRoot> roots)
                throws IOException {
            offered = roots;
            if (choices.isEmpty()) throw new AssertionError("Unexpected choice");
            Object answer = choices.pop();
            if (answer instanceof IOException) throw (IOException) answer;
            return roots.get((Integer) answer);
        }

        public String password(String container, boolean retry) {
            asked.add(container + (retry ? " (retry)" : ""));
            if (passwords.isEmpty()) throw new AssertionError("Unexpected password question");
            Object answer = passwords.pop();
            return answer == null ? null : (String) answer;
        }
    }

    static class Stages implements ResourceImporter.Monitor {
        final List<ResourceImporter.Stage> seen = new ArrayList<>();
        boolean cancelAt;

        public boolean cancelled() {
            return cancelAt;
        }

        public void progress(int files, long bytes) {}

        public void stage(ResourceImporter.Stage stage, long total) {
            if (seen.isEmpty() || seen.get(seen.size() - 1) != stage) seen.add(stage);
        }
    }

    static int install(Answers answers, ResourceImporter.Monitor monitor, Doc... docs)
            throws IOException {
        return ArchiveInstaller.installDocuments(
                Arrays.asList(docs), game, assets, monitor, answers);
    }

    public static void main(String[] args) throws Exception {
        root = new File(args[0]);
        root.mkdirs();
        game = new File(root, "game");
        ResourceImporter.importExtracted(
                (stage, monitor) -> {
                    new File(stage, "PROGRAM").mkdirs();
                    new File(stage, "RESOURCE").mkdirs();
                    Files.write(new File(stage, "engine.ini").toPath(), new byte[] {1, 2, 3});
                    Files.write(new File(stage, "old-only").toPath(), new byte[] {4, 5, 6});
                    return 2;
                },
                game,
                null);
        old = hash(game);

        // One game: installed directly, no question; copies, scratch and stage are private and
        // removed.
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(discovery(candidate("A", "Game/", "\"game.zip\"", ""), "", "")));
        Stages stages = new Stages();
        int count = install(new Answers(), stages, new Doc("game.zip", 1, 2, 3));
        if (count != 3 || new File(game, "old-only").exists())
            throw new AssertionError("Single game was not installed");
        if (!NativeArchives.extracted.equals(Arrays.asList("A")))
            throw new AssertionError("Wrong root extracted " + NativeArchives.extracted);
        if (!new File(NativeArchives.sources.get(0)).getName().equals(".game-import-input")
                || !NativeArchives.inputNames.equals(Arrays.asList("game.zip")))
            throw new AssertionError(
                    "Reader did not read the private copies: "
                            + NativeArchives.sources
                            + NativeArchives.inputNames);
        if (!Arrays.equals(NativeArchives.limits, ArchiveInstaller.limits())
                || ArchiveInstaller.limits()[5] != 3)
            throw new AssertionError(
                    "Limits not passed: " + Arrays.toString(NativeArchives.limits));
        if (!stages.seen.equals(
                Arrays.asList(
                        ResourceImporter.Stage.COPYING,
                        ResourceImporter.Stage.READING,
                        ResourceImporter.Stage.NESTED,
                        ResourceImporter.Stage.INSTALLING,
                        ResourceImporter.Stage.VERIFYING,
                        ResourceImporter.Stage.FINISHING)))
            throw new AssertionError("Stages " + stages.seen);
        for (String sibling : new String[] {"input", "scratch", "stage"})
            if (new File(root, ".game-import-" + sibling).exists())
                throw new AssertionError("Kept " + sibling);
        old = hash(game);

        // Two copies: the player chooses; metadata and the script patch identification reach the
        // choice.
        NativeArchives.reset();
        String tested =
                candidate("A", "Original/", "\"both.zip\"", "")
                        .replace(
                                "\"patch_set\":null",
                                "\"patch_set\":{\"id\":\"teho-1.7.3-x64\",\"version\":\"ver. 1.7.3 (26.11.21) x64\"}");
        NativeArchives.discoveries.add(
                ok(
                        discovery(
                                tested + "," + candidate("B", "Patched/", "\"both.zip\"", ""),
                                "",
                                "")));
        Answers answers = new Answers();
        answers.choices.add(1);
        stages = new Stages();
        install(answers, stages, new Doc("both.zip", 1));
        ArchiveInstaller.GameRoot offered = answers.offered.get(1);
        if (answers.offered.size() != 2 || !NativeArchives.extracted.equals(Arrays.asList("B")))
            throw new AssertionError("Choice ignored");
        if (!offered.name().equals("Корсары: Каждому Свое")
                || !offered.version.equals("ver. 1.7.3")
                || !offered.place().equals("both.zip › Patched")
                || !offered.languages.equals(Arrays.asList("english", "russian"))
                || offered.patchMarkers.isEmpty()
                || offered.bytes != 30
                || !offered.ready()) throw new AssertionError("Metadata lost: " + offered.place());
        ArchiveInstaller.Identification first = answers.offered.get(0).identification,
                second = offered.identification;
        if (!first.identified
                || !first.setId.equals("teho-1.7.3-x64")
                || !first.setVersion.equals("ver. 1.7.3 (26.11.21) x64")
                || !second.identified
                || !second.setId.isEmpty()
                || !second.testedVersions.equals(Arrays.asList("ver. 1.7.3 (26.11.21) x64")))
            throw new AssertionError(
                    "Identification lost: " + second.setId + " " + second.testedVersions);
        if (!NativeArchives.layers.equals(Arrays.asList(assets)))
            throw new AssertionError("The reader did not get the layer: " + NativeArchives.layers);
        if (!stages.seen.contains(ResourceImporter.Stage.CHOOSING))
            throw new AssertionError("No choosing stage");
        old = hash(game);

        // Cancelled choice keeps the old game.
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(
                        discovery(
                                candidate("A", "One/", "\"x.zip\"", "")
                                        + ","
                                        + candidate("B", "Two/", "\"x.zip\"", ""),
                                "",
                                "")));
        Answers cancelChoice = new Answers();
        cancelChoice.choices.add(new ResourceImporter.CancelledException());
        if (!(rejected(() -> install(cancelChoice, null, new Doc("x.zip", 1)))
                instanceof ResourceImporter.CancelledException)) throw new AssertionError();
        if (!NativeArchives.extracted.isEmpty())
            throw new AssertionError("Extracted without a choice");

        // A not-ready copy is shown but cannot be installed.
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(
                        discovery(
                                candidate("A", "", "\"setup.exe\"", "\"setup-1c.bin\"")
                                        + ","
                                        + candidate("B", "Game/", "\"x.zip\"", ""),
                                "",
                                "")));
        Answers notReady = new Answers();
        notReady.choices.add(0);
        rejected(() -> install(notReady, null, new Doc("x.zip", 1)));
        if (notReady.offered.size() != 2 || !NativeArchives.extracted.isEmpty())
            throw new AssertionError("Not-ready root installed");

        // Nothing ready: the most specific explanation, without asking or extracting.
        Object[][] explanations = {
            {
                discovery(
                        candidate("A", "", "\"setup.exe\"", "\"setup-1b.bin\",\"setup-1c.bin\""),
                        "",
                        ""),
                "volumes",
                "setup-1b.bin, setup-1c.bin"
            },
            {
                discovery(
                        "",
                        "{\"location\":[\"game.7z.001\"],\"status\":8,\"error\":\"e\",\"missing_volumes\":[\"game.7z.002\"]}",
                        ""),
                "volumes",
                "game.7z.002"
            },
            {
                discovery(
                        "",
                        "{\"location\":[\"a.zip\",\"b.zip\",\"c.zip\",\"d.zip\",\"e.zip\"],\"status\":6,\"error\":\"deep\",\"missing_volumes\":[]}",
                        ""),
                "too_deep",
                "a.zip › b.zip › c.zip › d.zip › e.zip"
            },
            {
                discovery(
                        "",
                        "{\"location\":[\"game.ace\"],\"status\":2,\"error\":\"u\",\"missing_volumes\":[]},{\"location\":[\"menu.exe\"],\"status\":3,\"error\":\"x\",\"missing_volumes\":[]}",
                        ""),
                "unsupported",
                "game.ace"
            },
            {
                discovery(
                        "",
                        "{\"location\":[\"evil.zip\"],\"status\":5,\"error\":\"Unsafe archive path\",\"missing_volumes\":[]}",
                        ""),
                "damaged",
                "evil.zip: Unsafe archive path"
            },
            {
                discovery(
                        "",
                        "",
                        "{\"location\":[\"half.zip\"],\"root\":\"Game/\",\"has_engine_ini\":false,\"has_program\":true,\"has_resource\":true}"),
                "no_game",
                "half.zip › Game − engine.ini"
            },
            {
                discovery(
                        "",
                        "{\"location\":[\"menu.exe\"],\"status\":3,\"error\":\"x\",\"missing_volumes\":[]}",
                        ""),
                "no_game",
                ""
            },
        };
        for (Object[] explanation : explanations) {
            NativeArchives.reset();
            NativeArchives.discoveries.add(ok((String) explanation[0]));
            IOException error = rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)));
            if (!reason(error).equals(explanation[1]) || !detail(error).equals(explanation[2]))
                throw new AssertionError(
                        "Explained "
                                + reason(error)
                                + " '"
                                + detail(error)
                                + "' instead of "
                                + explanation[1]
                                + " '"
                                + explanation[2]
                                + "'");
            if (!NativeArchives.extracted.isEmpty()) throw new AssertionError("Extraction started");
        }

        // ZIP password: asked, retried after a wrong one, and cancellable.
        String encrypted =
                "{\"encrypted\":{\"location\":[\"outer.zip\",\"locked.zip\"],\"password_given\":false}}";
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                new String[] {"4", "Encrypted archives need a password", encrypted});
        NativeArchives.discoveries.add(new String[] {"4", "Incorrect passphrase", encrypted});
        NativeArchives.discoveries.add(
                ok(discovery(candidate("A", "", "\"outer.zip\",\"locked.zip\"", ""), "", "")));
        Answers passwords = new Answers();
        passwords.passwords.add("wrong");
        passwords.passwords.add("secret");
        install(passwords, null, new Doc("outer.zip", 1));
        if (!passwords.asked.equals(
                        Arrays.asList("outer.zip › locked.zip", "outer.zip › locked.zip (retry)"))
                || !NativeArchives.passwords.equals(
                        Arrays.asList(null, "wrong", "secret", "secret")))
            throw new AssertionError("Password flow " + passwords.asked + NativeArchives.passwords);
        old = hash(game);
        NativeArchives.reset();
        NativeArchives.discoveries.add(new String[] {"4", "Encrypted", encrypted});
        Answers giveUp = new Answers();
        giveUp.passwords.add(null);
        if (!(rejected(() -> install(giveUp, null, new Doc("outer.zip", 1)))
                instanceof ResourceImporter.CancelledException)) throw new AssertionError();
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                new String[] {
                    "11",
                    "Encrypted archive this reader cannot open",
                    "{\"encrypted\":{\"location\":[\"locked.7z\"],\"password_given\":false}}"
                });
        IOException unsupported =
                rejected(() -> install(new Answers(), null, new Doc("locked.7z", 1)));
        if (!reason(unsupported).equals("encrypted") || !detail(unsupported).equals("locked.7z"))
            throw new AssertionError("7z encryption: " + unsupported);
        // A wrong ZIP password found only while extracting: asked again, stage cleared.
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(discovery(candidate("A", "", "\"locked.zip\"", ""), "", "")));
        NativeArchives.extractions.add(new String[] {"4", "Incorrect passphrase", ""});
        Answers late = new Answers();
        late.passwords.add("secret");
        install(late, null, new Doc("locked.zip", 1));
        if (!late.asked.equals(Arrays.asList("locked.zip")) || NativeArchives.extracted.size() != 2)
            throw new AssertionError("Extraction password retry");
        old = hash(game);

        // Storage: needed and available bytes reach the message.
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                new String[] {
                    "10",
                    "Not enough free storage",
                    "{\"no_space\":{\"step\":\"nested\",\"needed_bytes\":5000,\"available_bytes\":1000}}"
                });
        IOException space = rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)));
        if (!(space instanceof ResourceImporter.StorageException)
                || ((ResourceImporter.StorageException) space).neededBytes != 5000
                || ((ResourceImporter.StorageException) space).availableBytes != 1000)
            throw new AssertionError("Space " + space);
        NativeArchives.reset();
        NativeArchives.discoveries.add(ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
        NativeArchives.extractions.add(
                new String[] {
                    "10",
                    "Not enough free storage",
                    "{\"no_space\":{\"step\":\"install\",\"needed_bytes\":9,\"available_bytes\":1}}"
                });
        if (((ResourceImporter.StorageException)
                                rejected(() -> install(new Answers(), null, new Doc("x.zip", 1))))
                        .neededBytes
                != 9) throw new AssertionError();

        // Reader verdicts become explicit reasons; nothing falls back to another path.
        String[][] verdicts = {
            {"1", "IOException"},
            {"2", "unsupported"},
            {"3", "unsupported"},
            {"5", "damaged"},
            {"6", "limit"},
            {"11", "encrypted"},
            {"12", "memory"},
            {"8", "volumes"},
            {"9", "MissingGameRootException"},
            {"7", "CancelledException"},
            {"41", "IOException"}
        };
        for (String[] verdict : verdicts) {
            NativeArchives.reset();
            NativeArchives.discoveries.add(
                    ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
            NativeArchives.extractions.add(
                    new String[] {
                        verdict[0],
                        "synthetic",
                        verdict[0].equals("8") ? "{\"missing_volumes\":[\"setup-1c.bin\"]}" : ""
                    });
            IOException error = rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)));
            if (!reason(error).equals(verdict[1]))
                throw new AssertionError("Status " + verdict[0] + " mapped to " + reason(error));
            if (verdict[0].equals("8") && !detail(error).equals("setup-1c.bin"))
                throw new AssertionError("Missing volume name " + detail(error));
        }
        NativeArchives.reset();
        NativeArchives.discoveries.add(new String[] {"6", "Too many archive entries", ""});
        if (!reason(rejected(() -> install(new Answers(), null, new Doc("x.zip", 1))))
                .equals("limit"))
            throw new AssertionError("A limit is not memory, whatever its message");
        NativeArchives.reset();
        NativeArchives.oom = true;
        NativeArchives.discoveries.add(ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
        if (!reason(rejected(() -> install(new Answers(), null, new Doc("x.zip", 1))))
                .equals("memory")) throw new AssertionError("OOM escaped");
        NativeArchives.reset();
        NativeArchives.discoveries.add(new String[] {"0", "", "{not json"});
        rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)));
        NativeArchives.reset();
        NativeArchives.filesWritten = 2;
        NativeArchives.discoveries.add(ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
        rejected(
                () ->
                        install(
                                new Answers(),
                                null,
                                new Doc("x.zip", 1))); // Installed count differs from discovery.
        NativeArchives.reset();
        NativeArchives.missingRoot = true;
        NativeArchives.discoveries.add(ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
        if (!(rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)))
                instanceof ResourceImporter.MissingGameRootException))
            throw new AssertionError("Stage was not validated");

        // Cancellation through the progress callback (true cancels).
        NativeArchives.reset();
        NativeArchives.discoveries.add(ok(discovery(candidate("A", "", "\"x.zip\"", ""), "", "")));
        Stages cancelling =
                new Stages() {
                    public void progress(int files, long bytes) {
                        if (files > 0) cancelAt = true;
                    }
                };
        if (!(rejected(() -> install(new Answers(), cancelling, new Doc("x.zip", 1)))
                        instanceof ResourceImporter.CancelledException)
                || !NativeArchives.callbackCancelled) throw new AssertionError("Callback cancel");

        // Picked documents: names, folder paths, size changes, copy cancellation.
        NativeArchives.reset();
        IOException duplicate =
                rejected(
                        () ->
                                install(
                                        new Answers(),
                                        null,
                                        new Doc("game.zip", 1),
                                        new Doc("GAME.zip", 2)));
        if (!reason(duplicate).equals("selection") || !NativeArchives.sources.isEmpty())
            throw new AssertionError("Duplicate names");
        for (String unsafe :
                new String[] {"../x.zip", "/abs.zip", "a//b.zip", "C:x.zip", "dir/", "x.zip "})
            if (!reason(rejected(() -> install(new Answers(), null, new Doc(unsafe, 1))))
                    .equals("selection")) throw new AssertionError("Unsafe name " + unsafe);
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(discovery(candidate("A", "", "\"Game/setup.exe\"", ""), "", "")));
        install(
                new Answers(),
                null,
                new Doc("Game/setup.exe", 42),
                new Doc("Game/setup-1a.bin", 7));
        if (!NativeArchives.inputNames.equals(Arrays.asList("Game/setup-1a.bin", "Game/setup.exe")))
            throw new AssertionError("Folder paths " + NativeArchives.inputNames);
        old = hash(game);
        NativeArchives.reset();
        Doc truncated = new Doc("x.zip", 1, 2);
        truncated.size = 5;
        rejected(() -> install(new Answers(), null, truncated));
        if (!NativeArchives.sources.isEmpty()) throw new AssertionError("Truncated copy was read");
        NativeArchives.reset();
        Stages copyCancel =
                new Stages() {
                    public void copying(long copied, long total) {
                        cancelAt = true;
                    }
                };
        if (!(rejected(
                        () ->
                                install(
                                        new Answers(),
                                        copyCancel,
                                        new Doc("x.zip", 1),
                                        new Doc("y.zip", 2)))
                instanceof ResourceImporter.CancelledException))
            throw new AssertionError("Copy cancel");
        if (root.getUsableSpace() < (60L << 30)) {
            NativeArchives.reset();
            Doc huge = new Doc("x.zip", 1);
            huge.size = 60L << 30;
            IOException precheck = rejected(() -> install(new Answers(), null, huge));
            if (!(precheck instanceof ResourceImporter.StorageException) || huge.opens != 0)
                throw new AssertionError("Copy space precheck " + precheck);
        } else System.out.println("SKIP copy storage precheck: host has more than 60 GiB free");

        // A ZIP's unpacked size from its central directory, before anything is copied.
        File zip = new File(root, "sizes.zip");
        try (java.util.zip.ZipOutputStream out =
                new java.util.zip.ZipOutputStream(new FileOutputStream(zip))) {
            for (int i = 0; i < 3; i++) {
                out.putNextEntry(new java.util.zip.ZipEntry("f" + i));
                out.write(new byte[1000 * (i + 1)]);
                out.closeEntry();
            }
        }
        try (FileInputStream in = new FileInputStream(zip);
                FileInputStream other = new FileInputStream(source(root))) {
            if (ArchiveInstaller.zipUnpackedBytes(in.getChannel()) != 6000
                    || ArchiveInstaller.zipUnpackedBytes(other.getChannel()) != -1)
                throw new AssertionError("ZIP central directory sizes");
        }
        if (root.getUsableSpace() < (60L << 30)) {
            NativeArchives.reset();
            Doc unpacks = new Doc("small.zip", 1);
            unpacks.unpacked = 60L << 30;
            IOException precheck = rejected(() -> install(new Answers(), null, unpacks));
            if (!(precheck instanceof ResourceImporter.StorageException) || unpacks.opens != 0)
                throw new AssertionError("Unpacked space precheck " + precheck);
        }

        // Download library: a finished set's directory is read in place and never changed.
        File set = new File(root, "game-downloads/one/torrent/payload");
        set.mkdirs();
        File source = new File(set, "setup.exe");
        Files.write(source.toPath(), new byte[] {42});
        NativeArchives.reset();
        NativeArchives.discoveries.add(
                ok(
                        discovery(
                                candidate("A", "", "\"setup.exe\"", "")
                                        + ","
                                        + candidate("B", "x/", "\"setup.exe\"", ""),
                                "",
                                "")));
        Answers chooseSecond = new Answers();
        chooseSecond.choices.add(1);
        if (ArchiveInstaller.installDirectory(set, game, assets, null, chooseSecond) != 3
                || !NativeArchives.sources.get(0).equals(set.getCanonicalPath()))
            throw new AssertionError("Download set source " + NativeArchives.sources);
        if (chooseSecond.offered.size() != 2
                || Files.readAllBytes(source.toPath())[0] != 42
                || !source.isFile())
            throw new AssertionError("Download set choice or source changed");
        File overlapping = new File(game, "downloads");
        overlapping.mkdirs();
        old = hash(game);
        rejected(
                () ->
                        ArchiveInstaller.installDirectory(
                                overlapping, game, assets, null, new Answers()));
        rejected(
                () ->
                        ArchiveInstaller.installDirectory(
                                new File(root, "missing-set"), game, assets, null, new Answers()));
        // Copies are never identified without the APK's layer.
        NativeArchives.reset();
        rejected(() -> ArchiveInstaller.installDirectory(set, game, null, null, new Answers()));
        rejected(
                () ->
                        ArchiveInstaller.installDocuments(
                                Arrays.asList(new Doc("y.zip", 1)),
                                game,
                                null,
                                null,
                                new Answers()));
        if (!NativeArchives.sources.isEmpty()) throw new AssertionError("Read without a layer");
        // A reader without the identification fields is refused, never taken as «no set».
        NativeArchives.discoveries.add(
                ok(
                        discovery(
                                candidate("A", "", "\"x.zip\"", "")
                                        .replace(",\"identified\":true,\"patch_set\":null", ""),
                                "",
                                "")));
        rejected(() -> install(new Answers(), null, new Doc("x.zip", 1)));

        // A loose game in a set moves into place (no copy); a failed or interrupted import gives
        // its parts back.
        String folder =
                candidate("F", "Game/", "", "")
                        .replace("\"kind\":\"archive\"", "\"kind\":\"folder\"");
        File loose = new File(root, "game-downloads/two/torrent/payload"),
                looseGame = new File(loose, "Game");
        java.util.function.Consumer<String> fill =
                marker -> {
                    try {
                        for (String name :
                                new String[] {
                                    "Engine.ini",
                                    "Program/a.c",
                                    "Resource/x.txt",
                                    "Resource/bad.dll",
                                    "readme.txt"
                                }) {
                            File file = new File(looseGame, name);
                            file.getParentFile().mkdirs();
                            Files.write(file.toPath(), marker.getBytes(StandardCharsets.UTF_8));
                        }
                    } catch (IOException e) {
                        throw new UncheckedIOException(e);
                    }
                };
        fill.accept("moved");
        ResourceImporter.Monitor cancelAfterMove =
                new ResourceImporter.Monitor() {
                    int verifying;

                    public boolean cancelled() {
                        return verifying >= 2;
                    }

                    public void progress(int files, long bytes) {}

                    public void stage(ResourceImporter.Stage stage, long total) {
                        if (stage == ResourceImporter.Stage.VERIFYING) verifying++;
                    }
                };
        old = hash(game);
        NativeArchives.reset();
        NativeArchives.discoveries.add(ok(discovery(folder, "", "")));
        rejected(
                () ->
                        ArchiveInstaller.installDirectory(
                                loose, game, assets, cancelAfterMove, new Answers()));
        if (!new File(looseGame, "Resource/x.txt").isFile()
                || !new File(looseGame, "Engine.ini").isFile()
                || new File(looseGame, "Resource/bad.dll").exists())
            throw new AssertionError("A cancelled move gives the parts back (programs left out)");
        NativeArchives.reset();
        NativeArchives.discoveries.add(ok(discovery(folder, "", "")));
        if (ArchiveInstaller.installDirectory(loose, game, assets, null, new Answers()) != 3)
            throw new AssertionError("Moved file count");
        if (!Files.readString(new File(game, "RESOURCE/x.txt").toPath()).equals("moved")
                || !new File(game, "engine.ini").isFile()
                || !new File(game, "PROGRAM/a.c").isFile()
                || new File(looseGame, "Resource").exists()
                || !new File(looseGame, "readme.txt").isFile()
                || new File(root, ".game-import-moved").exists()
                || !NativeArchives.extracted.isEmpty())
            throw new AssertionError(
                    "A loose game of a set moves into place, nothing extracted, nothing else taken");
        fill.accept("again");
        old = hash(game);
        File stage = new File(root, ".game-import-stage");
        stage.mkdir();
        ResourceImporter.moveInto(looseGame, game, stage, null); // the process dies here
        if (ResourceImporter.recover(game) != ResourceImporter.Recovery.CLEANED
                || !new File(looseGame, "Resource/x.txt").isFile()
                || stage.exists()) throw new AssertionError("Interrupted move given back");
        oldUnchanged();
        if (ResourceImporter.recover(game) != ResourceImporter.Recovery.NONE)
            throw new AssertionError("A clean start reports no interruption");

        // Interrupted import: recovery removes stale copies and scratch.
        File stale = new File(root, ".game-import-input"),
                scratch = new File(root, ".game-import-scratch");
        if (!stale.mkdir() || !scratch.mkdir() || !new File(scratch, "1").mkdir())
            throw new AssertionError();
        ResourceImporter.recover(game);
        if (stale.exists() || scratch.exists())
            throw new AssertionError("Recovery kept stale transaction directories");
        System.out.println(
                "PASS ArchiveInstaller discovery flow on the real transaction: direct install of one game, choice among several with metadata, not-ready copies, specific explanations (volumes/depth/unsupported/damaged/partial), ZIP password ask/retry/cancel, unsupported encryption, storage needs (a ZIP's unpacked size before copying), reader verdicts, OOM, cancellation, document names and folder paths, download set read in place, a loose game moved into place and given back on failure or interruption, cleanup and recovery; old game SHA-256 kept on every failure");
        System.out.println(
                "BOUNDARY: reader results are scripted from the engine contract; no Android/JNI/native decoding runtime claimed");
    }
}

/** Scripted reader boundary returning engine-contract results. */
final class NativeArchives {
    interface Progress {
        boolean onProgress(int stage, String path, long bytes, long totalBytes, long files);
    }

    static boolean oom, missingRoot, callbackCancelled;
    static int filesWritten;
    static final Deque<String[]> discoveries = new ArrayDeque<>(), extractions = new ArrayDeque<>();
    static final List<String> sources = new ArrayList<>(),
            extracted = new ArrayList<>(),
            passwords = new ArrayList<>(),
            inputNames = new ArrayList<>();
    static long[] limits;

    static void reset() {
        oom = missingRoot = callbackCancelled = false;
        filesWritten = 3;
        discoveries.clear();
        extractions.clear();
        sources.clear();
        extracted.clear();
        passwords.clear();
        inputNames.clear();
        layers.clear();
        limits = null;
    }

    static final List<AssetManager> layers = new ArrayList<>();

    static String[] removeTree(String path) {
        return HostTreeRemoval.removeTree(path);
    }

    static String[] discover(
            String source,
            String scratch,
            String password,
            AssetManager layer,
            long[] bounds,
            Progress progress) {
        sources.add(source);
        passwords.add(password);
        layers.add(layer);
        limits = bounds;
        File scratchDir = new File(scratch);
        if (!scratchDir.isDirectory() || scratchDir.list().length != 0)
            throw new AssertionError("Scratch not empty");
        File input = new File(source);
        if (input.isDirectory())
            try (java.util.stream.Stream<Path> files = Files.walk(input.toPath())) {
                inputNames.clear();
                files.filter(Files::isRegularFile)
                        .map(p -> input.toPath().relativize(p).toString())
                        .sorted()
                        .forEach(inputNames::add);
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        if (progress.onProgress(1, "", 0, 0, 0) || progress.onProgress(2, "nested.zip", 10, 10, 0))
            return new String[] {"7", "Import cancelled", ""};
        if (discoveries.isEmpty()) throw new AssertionError("Unexpected discovery");
        return discoveries.pop();
    }

    static String[] extractRoot(
            String source,
            String scratch,
            String password,
            String id,
            String staging,
            long[] bounds,
            Progress progress) {
        extracted.add(id);
        passwords.add(password);
        File stage = new File(staging);
        if (!stage.isAbsolute() || !stage.isDirectory() || stage.list().length != 0)
            throw new AssertionError("Reader did not receive an empty stage");
        if (!new File(scratch).isDirectory())
            throw new AssertionError("Scratch removed before extraction");
        try {
            String[] names = {"engine.ini", "PROGRAM/code.c", "RESOURCE/data.txt"};
            for (int i = 0; i < filesWritten; i++) {
                String name =
                        missingRoot && names[i].startsWith("RESOURCE") ? "extra.txt" : names[i];
                File file = new File(stage, name);
                file.getParentFile().mkdirs();
                Files.write(file.toPath(), new byte[] {17});
                if (oom) throw new OutOfMemoryError("Native test boundary allocation failure");
                if (progress.onProgress(3, name, i + 1, 30, i + 1)) {
                    callbackCancelled = true;
                    return new String[] {"7", "Import cancelled", ""};
                }
            }
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        return extractions.isEmpty() ? new String[] {"0", "", ""} : extractions.pop();
    }
}
