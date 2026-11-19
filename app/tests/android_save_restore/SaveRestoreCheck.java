package org.stormengine.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Host checks of {@link SaveRestore} with {@link SaveExport} and {@link SaveArchive} (tests/android_save_restore/run.py). */
public final class SaveRestoreCheck {
    private static File temp;
    private static int syncs;
    private static final SaveRestore.Durable DURABLE =
            directory -> {
                if (!directory.isDirectory())
                    throw new AssertionError("synced a folder that is not there: " + directory);
                ++syncs;
            };

    /** A save container as engine save_file.hpp writes it: header, zlib state, zlib description at extOffset. */
    static byte[] save(String state, String description) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] raw = state.getBytes(StandardCharsets.UTF_8), packed = deflate(raw);
        byte[] header = new byte[40];
        System.arraycopy("save".getBytes(StandardCharsets.US_ASCII), 0, header, 0, 4);
        out.write(header, 0, 40);
        word(out, raw.length);
        word(out, packed.length);
        out.write(packed, 0, packed.length);
        byte[] image = out.toByteArray();
        if (description == null) return image;
        byte[] ext = description.getBytes(StandardCharsets.UTF_8), extPacked = deflate(ext);
        int offset = image.length;
        out.reset();
        out.write(image, 0, image.length);
        word(out, extPacked.length);
        out.write(extPacked, 0, extPacked.length);
        image = out.toByteArray();
        put(image, 32, offset);
        put(image, 36, ext.length);
        return image;
    }

    private static byte[] deflate(byte[] data) {
        Deflater deflater = new Deflater(4);
        deflater.setInput(data);
        deflater.finish();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        while (!deflater.finished()) out.write(buffer, 0, deflater.deflate(buffer));
        deflater.end();
        return out.toByteArray();
    }

    private static void word(ByteArrayOutputStream out, int value) {
        byte[] b = new byte[4];
        put(b, 0, value);
        out.write(b, 0, 4);
    }

    private static void put(byte[] b, int at, int value) {
        for (int i = 0; i < 4; ++i) b[at + i] = (byte) (value >>> (8 * i));
    }

    private static File write(File file, byte[] data, long time) throws IOException {
        file.getParentFile().mkdirs();
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(data);
        }
        if (time > 0 && !file.setLastModified(time)) throw new IOException("Cannot date " + file);
        return file;
    }

    /** relative path → sha256:size (and the time to 2 s, the ZIP's resolution, when `times`). */
    private static Map<String, String> tree(File root, boolean times) throws IOException {
        Map<String, String> result = new TreeMap<>();
        if (!root.exists()) return result;
        try (java.util.stream.Stream<java.nio.file.Path> paths = Files.walk(root.toPath())) {
            for (java.nio.file.Path p : (Iterable<java.nio.file.Path>) paths::iterator) {
                if (!Files.isRegularFile(p)) continue;
                try {
                    byte[] hash =
                            MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(p));
                    result.put(
                            root.toPath().relativize(p).toString(),
                            Arrays.toString(hash)
                                    + ":"
                                    + Files.size(p)
                                    + (times ? ":" + p.toFile().lastModified() / 2000 : ""));
                } catch (java.security.NoSuchAlgorithmException impossible) {
                    throw new AssertionError(impossible);
                }
            }
        }
        return result;
    }

    private static File staging() throws IOException {
        File staging = Files.createTempDirectory(temp.toPath(), "staging").toFile();
        return staging;
    }

    /** Copies `source` as the picker's copy would be ("picked" in a fresh staging folder) and inspects it. */
    private static SaveRestore.Plan inspect(File source, String name, File root, File options)
            throws IOException {
        File staging = staging(), picked = new File(staging, "picked");
        Files.copy(source.toPath(), picked.toPath());
        return SaveRestore.inspect(picked, name, root, options, staging);
    }

    private static List<String> restore(
            File source, String name, File root, File options, SaveRestore.Choice choice)
            throws IOException {
        return SaveRestore.commit(inspect(source, name, root, options), root, choice, DURABLE);
    }

    private static void refused(String reason, File source, String name, File root, File options)
            throws IOException {
        Map<String, String> before = tree(root, true);
        try {
            restore(source, name, root, options, null);
        } catch (SaveArchive.Refused expected) {
            if (!expected.reason.equals(reason))
                throw new AssertionError(
                        name
                                + ": "
                                + expected.reason
                                + " instead of "
                                + reason
                                + " ("
                                + expected.getMessage()
                                + ")");
            if (!before.equals(tree(root, true)))
                throw new AssertionError(name + ": a refused restore changed SaveData");
            return;
        }
        throw new AssertionError(name + " restored, expected " + reason);
    }

    private static File zip(String name, String[] entries, byte[][] data) throws IOException {
        File file = new File(temp, name);
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(file))) {
            for (int i = 0; i < entries.length; ++i) {
                zip.putNextEntry(new ZipEntry(entries[i]));
                if (data[i] != null) zip.write(data[i]);
                zip.closeEntry();
            }
        }
        return file;
    }

    private static void noTemp(File root) {
        for (String path : new TreeMap<>(treeUnchecked(root)).keySet())
            if (SaveArchive.reserved(path)) throw new AssertionError("left behind: " + path);
    }

    private static Map<String, String> treeUnchecked(File root) {
        try {
            return tree(root, false);
        } catch (IOException e) {
            throw new AssertionError(e);
        }
    }

    public static void main(String[] args) throws IOException {
        temp = Files.createTempDirectory("save-restore").toFile();
        File phone = new File(temp, "phone"),
                root = SaveArchive.root(phone),
                options = new File(phone, "storm/options");
        byte[] a = save("state of A", "Шарль · Сен-Пьер"),
                map = save("карта мира", "Шарль · Карибы"),
                b = save("second", null);
        write(new File(root, "Player/A"), a, 1_790_000_000_000L);
        write(new File(root, "Player/Карта мира"), map, 1_790_100_000_000L);
        write(new File(root, "Second/B"), b, 1_780_000_000_000L);
        for (String real : args)
            write(
                    new File(root, "Player/" + new File(real).getName()),
                    Files.readAllBytes(new File(real).toPath()),
                    new File(real).lastModified());
        for (SaveExport.Save listed : SaveExport.list(root))
            SaveArchive.checkSave(SaveExport.resolve(root, listed.key));

        // 1. Export → a new phone (no SaveData, no options: the game never ran) → restore: byte for
        // byte, times kept.
        List<String> all = new ArrayList<>();
        for (SaveExport.Save listed : SaveExport.list(root)) all.add(listed.key);
        File out = new File(temp, "cache");
        out.mkdirs();
        File every = SaveExport.zip(root, all, out, "all"),
                one = SaveExport.zip(root, Collections.singletonList("Player/A"), out, "one");
        Map<String, String> original = tree(root, true);
        File fresh = new File(temp, "fresh"), freshRoot = SaveArchive.root(fresh);
        int syncsBefore = syncs;
        List<String> restored =
                restore(every, every.getName(), freshRoot, new File(fresh, "storm/options"), null);
        if (!tree(freshRoot, true).equals(original))
            throw new AssertionError(
                    "round trip differs:\n" + tree(freshRoot, true) + "\n" + original);
        if (restored.size() != all.size() || syncs - syncsBefore != all.size())
            throw new AssertionError("each save written and its folder synced: " + restored);
        List<String> listed = new ArrayList<>();
        for (SaveExport.Save save : SaveExport.list(freshRoot)) listed.add(save.key);
        if (!listed.equals(all))
            throw new AssertionError(
                    "the restored saves list as the originals did: " + listed + " vs " + all);
        noTemp(freshRoot);

        // 2. The same save again: a conflict the player answers; nothing is written before the
        // answer.
        SaveRestore.Plan again = inspect(one, one.getName(), freshRoot, null);
        if (again.conflicts().size() != 1 || !again.conflicts().get(0).existing.equals("A"))
            throw new AssertionError("conflict not found");
        try {
            SaveRestore.commit(again, freshRoot, null, DURABLE);
            throw new AssertionError("a conflict written without an answer");
        } catch (IllegalStateException expected) {
        }
        if (!tree(freshRoot, true).equals(original))
            throw new AssertionError("an unanswered conflict wrote");
        if (!SaveRestore.commit(again, freshRoot, SaveRestore.Choice.KEEP_BOTH, DURABLE)
                .equals(Collections.singletonList("Player/A (2)")))
            throw new AssertionError("keep both: a new name");
        if (!restore(one, one.getName(), freshRoot, null, SaveRestore.Choice.KEEP_BOTH)
                .equals(Collections.singletonList("Player/A (3)")))
            throw new AssertionError("keep both twice: the next free name");
        // Replace: the old bytes go, the archive's come, under the name the engine finds (folded
        // alike: "a" is "A").
        write(new File(freshRoot, "Player/A"), save("played on", "newer"), 1_791_770_000_000_000L);
        File lower = zip("lower.zip", new String[] {"PLAYER/a"}, new byte[][] {a});
        SaveRestore.Plan replace = inspect(lower, "lower.zip", freshRoot, null);
        if (replace.conflicts().size() != 1 || !replace.conflicts().get(0).existing.equals("A"))
            throw new AssertionError("names compare the engine's way");
        if (!SaveRestore.commit(replace, freshRoot, SaveRestore.Choice.REPLACE, DURABLE)
                .equals(Collections.singletonList("Player/A")))
            throw new AssertionError("replace keeps the existing folder and name spelling");
        if (!Arrays.equals(Files.readAllBytes(new File(freshRoot, "Player/A").toPath()), a))
            throw new AssertionError("replace wrote the archive's bytes");
        if (new File(freshRoot, "PLAYER").exists()
                && !new File(freshRoot, "PLAYER")
                        .getCanonicalFile()
                        .equals(new File(freshRoot, "Player").getCanonicalFile()))
            throw new AssertionError("a second folder that folds like Player");
        noTemp(freshRoot);

        // 3. Which profile: the archive's; an existing folder of another spelling; a bare file:
        // options, else Player.
        File p3 = new File(temp, "p3"),
                r3 = SaveArchive.root(p3),
                o3 = new File(p3, "storm/options");
        new File(r3, "player").mkdirs();
        if (!restore(one, one.getName(), r3, o3, null)
                .equals(Collections.singletonList("player/A")))
            throw new AssertionError("the existing «player» folder");
        File bare = write(new File(temp, "Карта мира"), map, 0);
        if (!restore(bare, "Карта мира", r3, o3, null)
                .equals(Collections.singletonList("player/Карта мира")))
            throw new AssertionError("a bare save: Player");
        write(
                o3,
                "controls.x=1\nstartgameparam.playerprofile=Captain\r\nstartgameparam.mod_skill_enemy_rate=4\n"
                        .getBytes(StandardCharsets.UTF_8),
                0);
        if (!SaveRestore.profile(o3).equals("Captain"))
            throw new AssertionError("options name the profile: " + SaveRestore.profile(o3));
        if (!restore(bare, "Карта мира", r3, o3, null)
                .equals(Collections.singletonList("Captain/Карта мира")))
            throw new AssertionError("a bare save: the options' profile");
        if (!Arrays.equals(Files.readAllBytes(new File(r3, "Captain/Карта мира").toPath()), map))
            throw new AssertionError("bare bytes");
        write(o3, "StartGameParam.PlayerProfile=\n".getBytes(StandardCharsets.UTF_8), 0);
        if (!SaveRestore.profile(o3).equals("Player"))
            throw new AssertionError("an empty profile is the default");
        write(o3, "StartGameParam.PlayerProfile=../x\n".getBytes(StandardCharsets.UTF_8), 0);
        try {
            SaveRestore.profile(o3);
            throw new AssertionError("an unusable profile name accepted");
        } catch (IOException expected) {
        }

        // 4. Refusals: SaveData stays exactly as it was.
        File r4 = SaveArchive.root(new File(temp, "p4"));
        write(new File(r4, "Player/keep"), a, 1_770_000_000_000L);
        refused(
                "not_save",
                write(
                        new File(temp, "text"),
                        "hello, not a save at all, long enough for a header...."
                                .getBytes(StandardCharsets.UTF_8),
                        0),
                "notes.txt",
                r4,
                null);
        refused("not_save", write(new File(temp, "short"), new byte[10], 0), "short", r4, null);
        byte[] corrupt = a.clone();
        corrupt[corrupt.length - 20] ^= 0x55; // inside the description stream
        refused("not_save", write(new File(temp, "corrupt-ext"), corrupt, 0), "corrupt", r4, null);
        byte[] corruptState = a.clone();
        corruptState[50] ^= 0x55;
        refused(
                "not_save",
                write(new File(temp, "corrupt-state"), corruptState, 0),
                "corrupt",
                r4,
                null);
        byte[] truncated = Arrays.copyOf(a, 60);
        refused(
                "not_save",
                write(new File(temp, "truncated"), truncated, 0),
                "truncated",
                r4,
                null);
        refused(
                "not_save",
                zip(
                        "text.zip",
                        new String[] {"Player/readme"},
                        new byte[][] {"text".getBytes(StandardCharsets.UTF_8)}),
                "text.zip",
                r4,
                null);
        refused(
                "not_save",
                zip("empty.zip", new String[] {}, new byte[][] {}),
                "empty.zip",
                r4,
                null);
        for (String unsafe :
                new String[] {
                    "../x", "/abs", "Player/../../x", "Player/x.storm-tmp", "Player/x.storm-bak",
                            "Player/x.storm-deleted",
                    "a/b/c", "NetSave/x", "Player/.hidden", "Player\\x", "loose", "Player/"
                })
            refused(
                    "unsafe",
                    zip(
                            "unsafe.zip",
                            new String[] {unsafe},
                            new byte[][] {unsafe.endsWith("/") ? null : a}),
                    unsafe,
                    r4,
                    null);
        refused(
                "unsafe",
                zip("twice.zip", new String[] {"Player/x", "player/X"}, new byte[][] {a, a}),
                "twice",
                r4,
                null);
        refused("unsafe", bare, "x.storm-bak", r4, null);
        refused("unsafe", bare, "../x", r4, null);
        File truncatedZip = new File(temp, "truncated.zip");
        byte[] whole = Files.readAllBytes(every.toPath());
        write(truncatedZip, Arrays.copyOf(whole, whole.length / 2), 0);
        refused("damaged", truncatedZip, "truncated.zip", r4, null);
        File flipped = new File(temp, "flipped.zip");
        byte[] bytes = Files.readAllBytes(one.toPath());
        int data =
                30
                        + (bytes[26] & 0xff | (bytes[27] & 0xff) << 8)
                        + (bytes[28] & 0xff | (bytes[29] & 0xff) << 8);
        bytes[data + 8] ^=
                0x10; // inside the deflated data of the one entry: it no longer inflates to its CRC
        write(flipped, bytes, 0);
        try {
            restore(flipped, "flipped.zip", r4, null, null);
            throw new AssertionError("a flipped byte restored");
        } catch (SaveArchive.Refused expected) {
            if (!expected.reason.equals("damaged")) throw new AssertionError(expected.reason);
        }
        try {
            SaveRestore.requireSpace(r4, Long.MAX_VALUE / 4);
            throw new AssertionError("no space not seen");
        } catch (SaveArchive.Refused expected) {
            if (!expected.reason.equals("no_space") || expected.bytes <= 0)
                throw new AssertionError(expected.reason + " " + expected.bytes);
        }

        // 5. A commit that stops: the saves before it are complete, nothing half-written is left.
        File r5 = SaveArchive.root(new File(temp, "p5"));
        SaveRestore.Plan plan = inspect(every, "every.zip", r5, null);
        if (!plan.items.get(1).staged.delete()) throw new AssertionError("cannot break the plan");
        try {
            SaveRestore.commit(plan, r5, null, DURABLE);
            throw new AssertionError("a broken plan committed");
        } catch (SaveRestore.Stopped stopped) {
            if (stopped.restored.size() != 1 || stopped.total != all.size())
                throw new AssertionError(stopped.getMessage());
        }
        noTemp(r5);
        if (SaveExport.list(r5).size() != 1)
            throw new AssertionError("only the complete save is listed");
        // The written file is read back before it takes the name: a temp file that cannot be
        // written leaves no trace.
        File r6 = SaveArchive.root(new File(temp, "p6"));
        new File(r6, "Player/A.storm-tmp")
                .mkdirs(); // the temp name is taken by a folder: the write fails
        try {
            restore(one, "one.zip", r6, null, null);
            throw new AssertionError("wrote through a blocked temp");
        } catch (IOException expected) {
        }
        if (new File(r6, "Player/A").exists())
            throw new AssertionError("a save appeared after a failed write");

        // A real save keeps loading as the engine reads it (no-op without --save).
        for (String real : args) {
            File copy = new File(freshRoot, "Player/" + new File(real).getName());
            if (!Arrays.equals(
                    Files.readAllBytes(copy.toPath()), Files.readAllBytes(new File(real).toPath())))
                throw new AssertionError("real save " + real);
            try (RandomAccessFile check = new RandomAccessFile(copy, "r")) {
                if (check.length() < 48) throw new AssertionError(real);
            }
        }
        System.out.println(
                "PASS save restore: export → restore byte for byte (one / all"
                        + (args.length > 0 ? ", " + args.length + " real saves" : "")
                        + "), times, listing, profiles (archive / folder spelling / options /"
                        + " Player), conflicts (keep both, replace, folded names), refusals (not a"
                        + " save, damaged, unsafe names, no space) leave SaveData unchanged, no"
                        + " temp files left");
    }
}
