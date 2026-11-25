package org.stormengine.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Host checks of {@link SaveExport} (tests/android_save_export/run.py). */
public final class SaveExportCheck {
    private static File write(File file, String text, long time) throws IOException {
        file.getParentFile().mkdirs();
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        if (!file.setLastModified(time)) throw new IOException("Cannot date " + file);
        return file;
    }

    private static void refused(File root, String key) {
        try {
            SaveExport.resolve(root, key);
        } catch (IOException expected) {
            return;
        }
        throw new AssertionError("accepted " + key);
    }

    private static java.util.Map<String, String> hashes(java.nio.file.Path root)
            throws IOException {
        java.util.Map<String, String> result = new java.util.TreeMap<>();
        try (java.util.stream.Stream<java.nio.file.Path> paths = Files.walk(root)) {
            for (java.nio.file.Path p : (Iterable<java.nio.file.Path>) paths::iterator) {
                if (Files.isSymbolicLink(p)) {
                    result.put(p.toString(), "link:" + Files.readSymbolicLink(p));
                    continue;
                }
                if (!Files.isRegularFile(p)) continue;
                try {
                    byte[] hash =
                            java.security.MessageDigest.getInstance("SHA-256")
                                    .digest(Files.readAllBytes(p));
                    result.put(
                            p.toString(),
                            java.util.Arrays.toString(hash)
                                    + ":"
                                    + Files.size(p)
                                    + ":"
                                    + Files.getLastModifiedTime(p));
                } catch (java.security.NoSuchAlgorithmException impossible) {
                    throw new AssertionError(impossible);
                }
            }
        }
        return result;
    }

    public static void main(String[] args) throws IOException {
        File temp = Files.createTempDirectory("save-export").toFile();
        File files = new File(temp, "files"), root = SaveArchive.root(files);
        if (!SaveExport.list(root).isEmpty()) throw new AssertionError("no SaveData yet: no saves");
        if (!root.getPath().endsWith("files/storm/SaveData".replace('/', File.separatorChar)))
            throw new AssertionError(root.getPath());

        write(new File(root, "Player/Карта мира"), "map", 3_000_000L);
        write(new File(root, "Player/Old"), "old", 1_000_000L);
        write(new File(root, "Second/Newest"), "newest", 4_000_000L);
        write(new File(root, "Player/Old.storm-tmp"), "x", 5_000_000L);
        write(new File(root, "Player/Old.storm-bak"), "x", 5_000_000L);
        write(new File(root, "Player/Gone.storm-deleted"), "x", 5_000_000L);
        write(new File(root, "NetSave/Net"), "x", 5_000_000L);
        write(new File(files, "storm/options"), "x", 5_000_000L);
        write(new File(files, "storm/Logs/system.log"), "x", 5_000_000L);
        write(new File(root, "loose"), "not in a profile", 5_000_000L);

        List<String> keys = new ArrayList<>();
        for (SaveExport.Save save : SaveExport.list(root))
            keys.add(save.key + "|" + save.name + "|" + save.written);
        String expected =
                "[Second/Newest|Newest|4000000, Player/Карта мира|Карта мира|3000000, Player/Old|Old|1000000]";
        if (!keys.toString().equals(expected))
            throw new AssertionError("saves, newest first, without reserved files: " + keys);

        for (String key :
                new String[] {
                    "",
                    "Player",
                    "Player/",
                    "/Old",
                    "Player/../Second/Newest",
                    "../storm/options",
                    "Player/Old.storm-tmp",
                    "NetSave/Net",
                    "Player/Missing",
                    ".hidden/x",
                    "Player/sub/x"
                }) refused(root, key);

        // Links (including another profile linked elsewhere), hidden files and Windows ZIP paths
        // are not saves.
        File outside = write(new File(temp, "outside"), "private", 6_000_000L);
        Files.createSymbolicLink(new File(root, "Player/linked").toPath(), outside.toPath());
        Files.createSymbolicLink(
                new File(root, "LinkedProfile").toPath(), new File(root, "Player").toPath());
        write(new File(root, "Player/.hidden"), "hidden", 6_000_000L);
        refused(root, "Player/linked");
        refused(root, "LinkedProfile/Old");
        refused(root, "Player/..\\outside");
        refused(root, null);
        if (SaveExport.list(root).size() != 3)
            throw new AssertionError("a link or hidden file was listed");
        java.util.Map<String, String> before = hashes(root.toPath());
        File out = new File(temp, "cache/saves");
        out.mkdirs();
        File archive =
                SaveExport.zip(
                        root,
                        java.util.Collections.singletonList("Player/Карта мира"),
                        out,
                        "20261114-120000");
        if (!archive.getName().startsWith("seawolves-save-20261114-120000-")
                || !archive.getName().endsWith(".zip")) throw new AssertionError(archive.getName());
        try (ZipFile zip = new ZipFile(archive)) {
            List<String> names = new ArrayList<>();
            for (ZipEntry entry : java.util.Collections.list(zip.entries()))
                names.add(entry.getName());
            if (!names.toString().equals("[Player/Карта мира]"))
                throw new AssertionError("only the picked save: " + names);
            byte[] data = new byte[3];
            if (zip.getInputStream(zip.getEntry("Player/Карта мира")).read(data) != 3
                    || !new String(data, StandardCharsets.UTF_8).equals("map"))
                throw new AssertionError("the save's bytes");
            if (zip.getEntry("Player/Карта мира").getTime() / 2000 != 3_000_000L / 2000)
                throw new AssertionError("the entry keeps the save's time");
        }
        // «Отправить все сохранения»: every listed save, one entry each, the same format.
        List<String> all = new ArrayList<>();
        for (SaveExport.Save save : SaveExport.list(root)) all.add(save.key);
        File every = SaveExport.zip(root, all, out, "20261114-120000");
        if (!every.getName().startsWith("seawolves-saves-20261114-120000-"))
            throw new AssertionError(every.getName());
        try (ZipFile zip = new ZipFile(every)) {
            List<String> names = new ArrayList<>();
            for (ZipEntry entry : java.util.Collections.list(zip.entries()))
                names.add(entry.getName());
            if (!names.toString().equals("[Second/Newest, Player/Карта мира, Player/Old]"))
                throw new AssertionError("every save, nothing else: " + names);
        }
        if (!every.delete()) throw new AssertionError("cannot remove " + every);
        try {
            SaveExport.zip(root, java.util.Collections.<String>emptyList(), out, "none");
            throw new AssertionError("an empty export");
        } catch (IOException expectedError) {
        }
        try {
            SaveExport.zip(
                    root, java.util.Arrays.asList("Player/Old", "Player/Missing"), out, "partial");
            throw new AssertionError("a missing save among all");
        } catch (IOException expectedError) {
        }
        File second =
                SaveExport.zip(
                        root,
                        java.util.Collections.singletonList("Player/Карта мира"),
                        out,
                        "20261114-120000");
        if (second.equals(archive) || !archive.isFile())
            throw new AssertionError("a second share replaced a live URI");
        try {
            SaveExport.zip(
                    root, java.util.Collections.singletonList("Player/Missing"), out, "missing");
            throw new AssertionError("missing save exported");
        } catch (IOException expectedError) {
        }
        File notDirectory = write(new File(temp, "not-directory"), "x", 0);
        try {
            SaveExport.zip(
                    root, java.util.Collections.singletonList("Player/Old"), notDirectory, "error");
            throw new AssertionError("invalid destination accepted");
        } catch (IOException expectedError) {
        }
        try {
            SaveExport.list(notDirectory);
            throw new AssertionError("listing error hidden");
        } catch (IOException expectedError) {
        }
        File brokenRoot = new File(temp, "broken-saves");
        File unreadable = write(new File(brokenRoot, "Player/Unreadable"), "private", 1_000_000L);
        if (!unreadable.setReadable(false, false))
            throw new AssertionError("cannot prepare unreadable save");
        try {
            SaveExport.zip(
                    brokenRoot,
                    java.util.Collections.singletonList("Player/Unreadable"),
                    out,
                    "unreadable");
            throw new AssertionError("unreadable save accepted");
        } catch (IOException expectedError) {
            // The output was allocated before opening the source; its partial ZIP must be removed.
        } finally {
            unreadable.setReadable(true, true);
        }
        if (!before.equals(hashes(root.toPath())))
            throw new AssertionError("save bytes or metadata changed");
        if (out.list().length != 2) throw new AssertionError("an unsuccessful export left a file");
        System.out.println(
                "PASS save ZIP (one / all) exact contents, Unicode keys, traversal/symlinks, empty/error cases, unique shares; source SHA256+size+mtime unchanged");
    }
}
