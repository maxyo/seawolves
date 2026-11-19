package org.stormengine.port;

import java.io.*;
import java.nio.channels.FileLock;

/** Recoverable publication of an extracted game directory; also tested on the host JVM. */
public final class ResourceImporter {
    /** Import steps in order; a step may be skipped (a single game root needs no choice). */
    public enum Stage {
        COPYING,
        READING,
        NESTED,
        CHOOSING,
        INSTALLING,
        VERIFYING,
        FINISHING,
        DELETING
    }

    public interface Monitor {
        boolean cancelled();

        /** Files written and bytes processed within the current stage. */
        void progress(int files, long bytes);

        /** Entering a stage; totalBytes is 0 when the amount of work is unknown. */
        default void stage(Stage stage, long totalBytes) {}

        /** Copying selected source documents into private input storage before extraction. */
        default void copying(long copied, long total) {}

        default void committing() {}
    }

    /** Trusted in-process producer; writes only into the supplied empty private stage. */
    public interface StageExtractor {
        int extract(File emptyStage, Monitor monitor) throws IOException;
    }

    public static final class CancelledException extends IOException {
        public CancelledException() {
            super("Import cancelled");
        }
    }

    /** Not enough free storage; neededBytes/availableBytes are 0 when unknown. */
    public static final class StorageException extends IOException {
        public final long neededBytes, availableBytes;

        StorageException(String message) {
            this(message, 0, 0);
        }

        StorageException(String message, long neededBytes, long availableBytes) {
            super(message);
            this.neededBytes = neededBytes;
            this.availableBytes = availableBytes;
        }
    }

    public static final class MissingGameRootException extends IOException {
        MissingGameRootException() {
            super("Game folder must contain engine.ini, PROGRAM and RESOURCE at its root.");
        }
    }

    public static final class RecoveryRequiredException extends IOException {
        RecoveryRequiredException() {
            super("An interrupted import needs recovery. Reopen the launcher.");
        }
    }

    private ResourceImporter() {}

    static File sibling(File game, String suffix) {
        return new File(
                game.getAbsoluteFile().getParentFile(), "." + game.getName() + "-import-" + suffix);
    }

    /** Private copies of picked source documents; owned by the running transaction, removed by recovery. */
    static File input(File game) {
        return sibling(game, "input");
    }

    /** Nested containers unpacked while reading a source; owned by the running transaction, removed by recovery. */
    static File scratch(File game) {
        return sibling(game, "scratch");
    }

    private static void check(Monitor monitor) throws CancelledException {
        if (monitor != null && monitor.cancelled()) throw new CancelledException();
    }

    public static void validate(File directory) throws IOException {
        if (!directory.isAbsolute()
                || !directory.isDirectory()
                || !directory.canRead()
                || !new File(directory, "engine.ini").isFile()
                || !new File(directory, "PROGRAM").isDirectory()
                || !new File(directory, "RESOURCE").isDirectory())
            throw new MissingGameRootException();
    }

    /** A pending publication must be recovered before any imported game is launched. */
    public static void requirePublished(File game) throws IOException {
        if (sibling(game, "pending").exists()) throw new RecoveryRequiredException();
    }

    /** Cleans up after an interrupted import; true when there was one (said once to the player). */
    public static boolean recover(File game) throws IOException {
        return withLock(game, () -> recoverLocked(game) ? 1 : 0) == 1;
    }

    /** Removes the imported game (a recoverable rename first: an interruption never leaves half a game behind). */
    public static void discard(File game) throws IOException {
        withLock(
                game,
                () -> {
                    recoverLocked(game);
                    if (game.exists()) move(game, sibling(game, "trash"));
                    deleteTree(sibling(game, "trash"));
                    return 0;
                });
    }

    private interface Operation {
        int run() throws IOException;
    }

    private static int withLock(File game, Operation operation) throws IOException {
        File parent = game.getAbsoluteFile().getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create import directory");
        try (RandomAccessFile lockFile = new RandomAccessFile(sibling(game, "lock"), "rw");
                FileLock lock = lockFile.getChannel().tryLock()) {
            if (lock == null) throw new IOException("Another import is already running");
            return operation.run();
        } catch (java.nio.channels.OverlappingFileLockException e) {
            throw new IOException("Another import is already running", e);
        }
    }

    /** On interruption before commit, restore the previous complete tree whenever it exists; true when there was one. */
    private static boolean recoverLocked(File game) throws IOException {
        File pending = sibling(game, "pending"),
                backup = sibling(game, "backup"),
                stage = sibling(game, "stage");
        boolean interrupted =
                pending.exists()
                        || stage.exists()
                        || input(game).exists()
                        || scratch(game).exists();
        int hadOriginal = -1;
        if (pending.exists())
            try (FileInputStream in = new FileInputStream(pending)) {
                hadOriginal = in.read();
            }
        giveBack(game, hadOriginal == '0' || backup.exists());
        if (pending.exists()) {
            if (backup.exists()) {
                deleteTree(game);
                move(backup, game);
            } else if (hadOriginal == '0') {
                deleteTree(game);
            } else if (!game.exists()) {
                throw new IOException(
                        "Cannot recover interrupted import; previous resources were retained for"
                                + " inspection");
            }
            remove(pending);
        }
        // A backup without a journal is from a completed publication. Never delete the only tree.
        if (backup.exists() && !game.exists()) move(backup, game);
        deleteTree(backup);
        deleteTree(stage);
        deleteTree(input(game));
        deleteTree(scratch(game));
        deleteTree(sibling(game, "trash"));
        remove(sibling(game, "journal-temp"));
        return interrupted;
    }

    /**
     * Parts of a finished download moved into the stage ({@link #moveInto}) go back to the download when the import
     * did not commit: from the stage, or from the game once the stage was renamed to it (`stagePublished`).
     */
    private static void giveBack(File game, boolean stagePublished) throws IOException {
        File moved = sibling(game, "moved");
        if (!moved.exists()) return;
        boolean committed = !sibling(game, "pending").exists() && !sibling(game, "stage").exists();
        if (!committed) {
            try (BufferedReader in =
                    new BufferedReader(
                            new InputStreamReader(
                                    new FileInputStream(moved),
                                    java.nio.charset.StandardCharsets.UTF_8))) {
                for (String line; (line = in.readLine()) != null; ) {
                    int tab = line.indexOf('\t');
                    if (tab < 0)
                        throw new IOException("Damaged move journal of an interrupted import");
                    File source = new File(line.substring(tab + 1));
                    File placed = new File(sibling(game, "stage"), line.substring(0, tab));
                    if (!placed.exists() && stagePublished)
                        placed = new File(game, line.substring(0, tab));
                    if (placed.exists() && !source.exists()) move(placed, source);
                }
            }
        }
        remove(moved);
    }

    /**
     * Moves a complete game root of this app's own storage (a finished download, on the volume of the game) into the
     * empty stage by renaming its parts: nothing is copied, so the import needs no room for a second copy. A journal
     * gives the parts back to {@code root} when the import does not commit. Members the folder import leaves out
     * (programs) are deleted first. Returns the number of game files.
     */
    static int moveInto(File root, File game, File stage, Monitor monitor) throws IOException {
        if (!sibling(game, "stage").getAbsoluteFile().equals(stage.getAbsoluteFile()))
            throw new IOException("Not the import stage of " + game);
        java.util.LinkedHashMap<String, File> parts = new java.util.LinkedHashMap<>();
        File[] children = root.listFiles();
        if (children == null) throw new IOException("Cannot list the downloaded game");
        for (File child : children) {
            String key = child.getName().toLowerCase(java.util.Locale.ROOT);
            String target =
                    child.isDirectory()
                            ? (key.equals("resource")
                                    ? "RESOURCE"
                                    : key.equals("program") ? "PROGRAM" : null)
                            : child.isFile() && (key.equals("engine.ini") || key.equals("options"))
                                    ? key
                                    : null;
            if (target == null) continue;
            if (parts.put(target, child) != null)
                throw new IOException("The downloaded game has two " + target + " (letter case)");
        }
        if (!parts.containsKey("engine.ini")
                || !parts.containsKey("PROGRAM")
                || !parts.containsKey("RESOURCE")) throw new MissingGameRootException();
        if (monitor != null) monitor.stage(Stage.VERIFYING, 0);
        int[] files = {0};
        for (File part : parts.values()) prune(part, files, monitor);
        StringBuilder journal = new StringBuilder();
        for (java.util.Map.Entry<String, File> part : parts.entrySet())
            journal.append(part.getKey())
                    .append('\t')
                    .append(part.getValue().getAbsolutePath())
                    .append('\n');
        File moved = sibling(game, "moved"), temporary = sibling(game, "moved-temp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(journal.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        move(temporary, moved);
        for (java.util.Map.Entry<String, File> part : parts.entrySet())
            if (!part.getValue().renameTo(new File(stage, part.getKey())))
                throw new IOException(
                        "Cannot move the downloaded "
                                + part.getKey()
                                + " into place (the download and the game must share a volume)");
        return files[0];
    }

    private static final java.util.Set<String> PROGRAMS =
            new java.util.HashSet<>(
                    java.util.Arrays.asList(
                            ".exe", ".dll", ".com", ".bat", ".cmd", ".msi", ".scr", ".ps1", ".sh",
                            ".so", ".apk", ".jar", ".lnk"));

    /** Counts the files under `file` and deletes the programs among them (the archive reader's own rule). */
    private static void prune(File file, int[] files, Monitor monitor) throws IOException {
        check(monitor);
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null)
                throw new IOException("Cannot list the downloaded game: " + file.getName());
            for (File child : children) prune(child, files, monitor);
            return;
        }
        String name = file.getName().toLowerCase(java.util.Locale.ROOT);
        int dot = name.lastIndexOf('.');
        if (dot >= 0 && PROGRAMS.contains(name.substring(dot))) remove(file);
        else if (++files[0] % 500 == 0 && monitor != null) monitor.progress(files[0], 0);
    }

    /** Extract once into an empty stage, validate, then publish by a recoverable rename transaction. */
    public static int importExtracted(StageExtractor extractor, File game, Monitor monitor)
            throws IOException {
        if (extractor == null) throw new IOException("No resource extractor was provided");
        return withLock(
                game,
                () -> {
                    recoverLocked(game);
                    File stage = sibling(game, "stage"),
                            backup = sibling(game, "backup"),
                            pending = sibling(game, "pending");
                    try {
                        check(monitor);
                        if (!stage.mkdir())
                            throw new IOException("Cannot create import staging directory");
                        int files = extractor.extract(stage, monitor);
                        if (files < 0) throw new IOException("Invalid extracted file count");
                        if (monitor != null) monitor.stage(Stage.VERIFYING, 0);
                        validate(stage);
                        check(monitor);
                        if (monitor != null) {
                            monitor.stage(Stage.FINISHING, 0);
                            monitor.committing();
                        }
                        // Persist intent before moving either tree. Cancellation stops before this
                        // short commit section.
                        File journalTemp = sibling(game, "journal-temp");
                        try (FileOutputStream journal = new FileOutputStream(journalTemp)) {
                            journal.write(game.exists() ? '1' : '0');
                            journal.getFD().sync();
                        }
                        move(journalTemp, pending);
                        if (game.exists()) move(game, backup);
                        move(stage, game);
                        remove(pending); // Commit point; backup cleanup can safely resume after
                        // process death.
                        remove(sibling(game, "moved")); // moved download parts are the game's now
                        // Cleanup failure must not report a failed replacement after it has already
                        // committed.
                        try {
                            deleteTree(backup);
                        } catch (IOException ignored) {
                        }
                        return files;
                    } catch (IOException | RuntimeException failure) {
                        try {
                            recoverLocked(game);
                        } catch (IOException cleanup) {
                            failure.addSuppressed(cleanup);
                        }
                        throw failure;
                    }
                });
    }

    private static void move(File from, File to) throws IOException {
        if (!from.renameTo(to))
            throw new IOException("Cannot publish game resources (directory rename failed)");
    }

    private static void remove(File file) throws IOException {
        if (file.exists() && !file.delete())
            throw new IOException("Cannot remove import transaction file: " + file.getName());
    }

    static void deleteTree(File file) throws IOException {
        if (!file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null)
                throw new IOException("Cannot clean interrupted import: " + file.getName());
            for (File child : children) deleteTree(child);
        }
        remove(file);
    }
}
