package org.stormengine.port;

import android.util.Log;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

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

    /** What start-up recovery found (said once to the player). */
    public enum Recovery {
        /** No interrupted import. */
        NONE,
        /** An interrupted import was rolled back and its leftovers removed. */
        CLEANED,
        /** An interrupted import was rolled back, but a leftover could not be removed: it was set aside (logged). */
        QUARANTINED
    }

    /** Cleans up after an interrupted import. */
    public static Recovery recover(File game) throws IOException {
        return Recovery.values()[withLock(game, () -> recoverLocked(game).ordinal())];
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

    /**
     * Held by the importing process for an import's whole span (ImportCoordinator: from its first copy until its
     * result), so that the game and the download library, in their own processes, leave the game's files alone
     * meanwhile (importing). The kernel releases it with the process: a killed import leaves nothing stale. Closing
     * the channel releases it.
     */
    static FileChannel holdImport(File game) throws IOException {
        File parent = game.getAbsoluteFile().getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create import directory");
        FileChannel channel =
                FileChannel.open(
                        sibling(game, "running").toPath(),
                        StandardOpenOption.CREATE,
                        StandardOpenOption.WRITE);
        try {
            channel.lock();
            return channel;
        } catch (IOException | RuntimeException error) {
            channel.close();
            throw error;
        }
    }

    /** True while a process holds holdImport. An unreadable lock file is reported and counts as an import. */
    static boolean importing(File game) {
        File file = sibling(game, "running");
        if (!file.exists()) return false;
        try (FileChannel channel = FileChannel.open(file.toPath(), StandardOpenOption.WRITE);
                FileLock lock = channel.tryLock()) {
            return lock == null;
        } catch (OverlappingFileLockException heldHere) {
            return true;
        } catch (IOException error) {
            Log.e("StormImport", "Cannot check whether an import is running: " + error);
            return true;
        }
    }

    private static int withLock(File game, Operation operation) throws IOException {
        File parent = game.getAbsoluteFile().getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Cannot create import directory");
        try (RandomAccessFile lockFile = new RandomAccessFile(sibling(game, "lock"), "rw");
                FileLock lock = lockFile.getChannel().tryLock()) {
            if (lock == null) throw new IOException("Another import is already running");
            return operation.run();
        } catch (OverlappingFileLockException e) {
            throw new IOException("Another import is already running", e);
        }
    }

    /** On interruption before commit, restore the previous complete tree whenever it exists. */
    private static Recovery recoverLocked(File game) throws IOException {
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
                        "Cannot recover interrupted import; previous resources were retained for inspection");
            }
            remove(pending);
        }
        // A backup without a journal is from a completed publication. Never delete the only tree.
        if (backup.exists() && !game.exists()) move(backup, game);
        removeQuarantined(game);
        boolean removed = true;
        for (File leftover :
                new File[] {backup, stage, input(game), scratch(game), sibling(game, "trash")})
            removed &= removeOrQuarantine(game, leftover);
        remove(sibling(game, "journal-temp"));
        return !removed ? Recovery.QUARANTINED : interrupted ? Recovery.CLEANED : Recovery.NONE;
    }

    private static final String QUARANTINE = "quarantine-";

    /**
     * Removes a leftover of an interrupted import, or, when it cannot be removed, renames it out of the import's way
     * (false): a leftover that is not removable must not block every later import and every start. Each start tries
     * to remove what was set aside again.
     */
    private static boolean removeOrQuarantine(File game, File leftover) throws IOException {
        try {
            deleteTree(leftover);
            return true;
        } catch (IOException failure) {
            String part = leftover.getName().substring(sibling(game, "").getName().length());
            File aside = sibling(game, QUARANTINE + System.currentTimeMillis() + "-" + part);
            Log.e("StormImport", "Cannot remove " + leftover + "; set aside as " + aside, failure);
            move(leftover, aside);
            return false;
        }
    }

    /** Removes the leftovers earlier starts set aside; a failure is logged and retried on the next start. */
    private static void removeQuarantined(File game) {
        String prefix = sibling(game, QUARANTINE).getName();
        // The import's own directory: every name in it was made by this class.
        File[] children = game.getAbsoluteFile().getParentFile().listFiles();
        if (children == null) {
            Log.e("StormImport", "Cannot list " + game.getAbsoluteFile().getParent());
            return;
        }
        for (File child : children) {
            if (!child.getName().startsWith(prefix)) continue;
            try {
                deleteTree(child);
            } catch (IOException failure) {
                Log.e(
                        "StormImport",
                        "Set-aside import leftover still not removable: " + child,
                        failure);
            }
        }
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
                                    new FileInputStream(moved), StandardCharsets.UTF_8))) {
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
        LinkedHashMap<String, File> parts = new LinkedHashMap<>();
        File[] children = root.listFiles();
        if (children == null) throw new IOException("Cannot list the downloaded game");
        for (File child : children) {
            String key = child.getName().toLowerCase(Locale.ROOT);
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
        for (Map.Entry<String, File> part : parts.entrySet())
            journal.append(part.getKey())
                    .append('\t')
                    .append(part.getValue().getAbsolutePath())
                    .append('\n');
        File moved = sibling(game, "moved"), temporary = sibling(game, "moved-temp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(journal.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        move(temporary, moved);
        for (Map.Entry<String, File> part : parts.entrySet())
            if (!part.getValue().renameTo(new File(stage, part.getKey())))
                throw new IOException(
                        "Cannot move the downloaded "
                                + part.getKey()
                                + " into place (the download and the game must share a volume)");
        return files[0];
    }

    private static final Set<String> PROGRAMS =
            new HashSet<>(
                    Arrays.asList(
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
        String name = file.getName().toLowerCase(Locale.ROOT);
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
                        // Commit point; backup cleanup can safely resume after process death.
                        remove(pending);
                        remove(sibling(game, "moved")); // moved download parts are the game's now
                        // Cleanup failure must not report a failed replacement after it has already
                        // committed. The next recovery (app start or import) removes the backup.
                        try {
                            deleteTree(backup);
                        } catch (IOException cleanup) {
                            Log.w(
                                    "StormImport",
                                    "Previous game copy left at " + backup + " until next start",
                                    cleanup);
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

    /**
     * Removes a file or tree (nothing to do when absent) by the bytes of its names, never following a link: a tree an
     * older archive reader wrote may hold names that are not UTF-8, which File.list cannot list (ART aborts under
     * CheckJNI and garbles them otherwise, so they could never be deleted).
     */
    static void deleteTree(File file) throws IOException {
        String[] result = NativeArchives.removeTree(file.getAbsolutePath());
        if (Integer.parseInt(result[0]) != ArchiveStatus.OK)
            throw new IOException("Cannot remove " + file.getName() + ": " + result[1]);
    }
}
