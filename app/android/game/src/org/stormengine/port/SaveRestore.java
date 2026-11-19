package org.stormengine.port;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipFile;

/**
 * «Восстановить сохранение» (about screen): a file the player picked becomes saves of the game again. It is either an
 * archive of {@link SaveArchive} (one save or all of them, from «Отправить сохранение») or one bare save file. First
 * {@link #inspect} stages every save in the app's cache and checks it as the engine would load it, so nothing reaches
 * SaveData before the whole file is known to be good; then {@link #commit} writes each save the way the engine's
 * save_file.hpp writeAtomic does: "&lt;name&gt;.storm-tmp" (hidden from the game's lists), synced, read back and checked,
 * renamed over the name in one step, the folder synced. A restored file is an ordinary save record: the game lists it
 * with every profile's saves (mobile_save_facts.cpp SaveRecordsOfAllProfiles).
 *
 * <p>Where a save goes: the profile its archive entry records, else (a bare file) the profile of the last new game
 * (options StartGameParam.PlayerProfile, the game's own choice in option_sl.c), else the game's default "Player".
 * Profile folders and save names are matched the engine's way, ASCII-case-insensitively (android_writable_storage.hpp
 * child() refuses two names that fold alike), so an existing "player" folder takes the saves of "Player" and a save
 * whose name folds like an existing one is a conflict the player answers ({@link Choice}). Plain Java: checked on the
 * host (tests/android_save_restore).
 */
final class SaveRestore {
    /** Room kept free beyond the saves themselves (the engine's logs and option writes go on beside them). */
    static final long MARGIN = 8L << 20;

    private SaveRestore() {}

    /** Flushes a folder's entries to storage (Android: Os.fsync on the folder). */
    interface Durable {
        void syncDirectory(File directory) throws IOException;
    }

    /** The player's answer when a restored save has the name of one already there. */
    enum Choice {
        REPLACE,
        KEEP_BOTH
    }

    /** One save to restore. */
    static final class Item {
        final String profile, name;
        final File staged;

        /** The entry's time (the save's own modification time), or -1 for a bare file: written now. */
        final long written;

        /** The folder already on disk for the profile (its spelling), or null: created. */
        final String folder;

        /** The name of the entry already there that folds like {@link #name}, or null. */
        final String existing;

        Item(
                String profile,
                String name,
                File staged,
                long written,
                String folder,
                String existing) {
            this.profile = profile;
            this.name = name;
            this.staged = staged;
            this.written = written;
            this.folder = folder;
            this.existing = existing;
        }
    }

    static final class Plan {
        final List<Item> items;
        final long bytes;

        Plan(List<Item> items, long bytes) {
            this.items = Collections.unmodifiableList(items);
            this.bytes = bytes;
        }

        List<Item> conflicts() {
            List<Item> found = new ArrayList<>();
            for (Item item : items) if (item.existing != null) found.add(item);
            return found;
        }
    }

    /** Commit stopped at a save after `restored` others were written (each of them complete). */
    static final class Stopped extends IOException {
        final List<String> restored;
        final int total;

        Stopped(List<String> restored, int total, IOException cause) {
            super(
                    "Restored "
                            + restored.size()
                            + " of "
                            + total
                            + " saves before: "
                            + cause.getMessage(),
                    cause);
            this.restored = restored;
            this.total = total;
        }
    }

    /**
     * Reads the picked file (copied to `picked` inside `staging`, whose other files are removed): its saves staged in
     * `staging` and checked, their profiles and conflicts below `root`. `displayName` names a bare save file.
     */
    static Plan inspect(File picked, String displayName, File root, File options, File staging)
            throws IOException {
        byte[] magic = new byte[4];
        int read;
        try (InputStream in = new FileInputStream(picked)) {
            read = in.read(magic);
        }
        boolean zip =
                read == 4
                        && magic[0] == 'P'
                        && magic[1] == 'K'
                        && (magic[2] == 3 && magic[3] == 4 || magic[2] == 5 && magic[3] == 6);
        List<Item> items = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        long bytes = 0;
        if (!zip) {
            SaveArchive.checkName(displayName);
            SaveArchive.checkSave(picked);
            String profile = profile(options);
            items.add(item(root, profile, displayName, picked, -1));
            return new Plan(items, picked.length());
        }
        // Every name is judged before ZipFile opens the archive: Android 14+ ZipFile itself refuses
        // ".." and absolute
        // names (ZipPathValidator) with a ZipException that would read as a damaged archive, not an
        // unsafe one.
        for (String name : centralNames(picked)) {
            if (seen.size() == SaveArchive.MAX_ENTRIES)
                throw new SaveArchive.Refused(
                        SaveArchive.Refused.UNSAFE,
                        "more than " + SaveArchive.MAX_ENTRIES + " entries");
            String[] parts = SaveArchive.split(name);
            if (!seen.add(SaveArchive.folded(parts[0]) + "/" + SaveArchive.folded(parts[1])))
                throw new SaveArchive.Refused(
                        SaveArchive.Refused.UNSAFE, "two entries for one save: " + name);
        }
        try (ZipFile archive = new ZipFile(picked)) {
            List<ZipEntry> entries = new ArrayList<>();
            for (Enumeration<? extends ZipEntry> all = archive.entries(); all.hasMoreElements(); ) {
                ZipEntry entry = all.nextElement();
                SaveArchive.split(entry.isDirectory() ? null : entry.getName());
                if (entry.getSize() < 0 || entry.getCrc() < 0)
                    throw new SaveArchive.Refused(
                            SaveArchive.Refused.DAMAGED,
                            "no size in the archive: " + entry.getName());
                if (entry.getSize() > SaveArchive.MAX_SAVE_BYTES)
                    throw new SaveArchive.Refused(
                            SaveArchive.Refused.NOT_SAVE,
                            entry.getName() + ": " + entry.getSize() + " bytes");
                bytes += entry.getSize();
                entries.add(entry);
            }
            if (entries.size() != seen.size())
                throw new SaveArchive.Refused(
                        SaveArchive.Refused.DAMAGED,
                        "the entries differ from the central directory");
            if (entries.isEmpty())
                throw new SaveArchive.Refused(SaveArchive.Refused.NOT_SAVE, "an empty archive");
            requireSpace(staging, bytes);
            for (int i = 0; i < entries.size(); ++i) {
                ZipEntry entry = entries.get(i);
                String[] parts = SaveArchive.split(entry.getName());
                File staged = new File(staging, "save-" + i);
                extract(archive, entry, staged);
                SaveArchive.checkSave(staged);
                items.add(item(root, parts[0], parts[1], staged, entry.getTime()));
            }
        } catch (ZipException damaged) {
            throw new SaveArchive.Refused(
                    SaveArchive.Refused.DAMAGED, damaged.getMessage(), 0, damaged);
        }
        if (!picked.delete()) throw new IOException("Cannot delete " + picked);
        return new Plan(items, bytes);
    }

    /** Writes the plan's saves; `choice` answers its conflicts (null only when there are none). Returns the keys written. */
    static List<String> commit(Plan plan, File root, Choice choice, Durable durable)
            throws IOException {
        if (choice == null && !plan.conflicts().isEmpty())
            throw new IllegalStateException("Restore conflicts need an answer");
        requireSpace(root, plan.bytes);
        List<String> restored = new ArrayList<>();
        for (Item item : plan.items) {
            try {
                restored.add(write(root, item, choice, durable));
            } catch (IOException error) {
                if (restored.isEmpty()) throw error;
                throw new Stopped(restored, plan.items.size(), error);
            }
        }
        return restored;
    }

    /**
     * The profile of a bare save: StartGameParam.PlayerProfile of the root options file (the engine's
     * XINTERFACE::SaveOptionsFile writes "a.b=value" lines; attribute names compare case-insensitively), else "Player".
     */
    static String profile(File options) throws IOException {
        if (!options.exists()) return SaveArchive.DEFAULT_PROFILE;
        String found = null;
        try (BufferedReader in =
                new BufferedReader(
                        new InputStreamReader(
                                new FileInputStream(options), StandardCharsets.UTF_8))) {
            for (String line; (line = in.readLine()) != null; ) {
                int equals = line.indexOf('=');
                if (equals > 0
                        && line.substring(0, equals)
                                .trim()
                                .equalsIgnoreCase("StartGameParam.PlayerProfile"))
                    found = line.substring(equals + 1).trim();
            }
        }
        if (found == null || found.isEmpty()) return SaveArchive.DEFAULT_PROFILE;
        try {
            SaveArchive.checkProfile(found);
        } catch (SaveArchive.Refused refused) {
            throw new IOException("The options name an unusable profile: " + found, refused);
        }
        return found;
    }

    private static Item item(File root, String profile, String name, File staged, long written)
            throws IOException {
        String folder = match(root, profile, true);
        String existing = folder == null ? null : match(new File(root, folder), name, false);
        return new Item(profile, name, staged, written, folder, existing);
    }

    /** The entry of `parent` whose name folds like `name` (a folder when `folder`), or null; never two, never a link. */
    private static String match(File parent, String name, boolean folder) throws IOException {
        if (!parent.exists()) return null;
        File[] children = parent.listFiles();
        if (children == null) throw new IOException("Cannot list " + parent);
        String wanted = SaveArchive.folded(name), found = null;
        for (File child : children) {
            if (!SaveArchive.folded(child.getName()).equals(wanted)) continue;
            if (found != null)
                throw new IOException(
                        "Two names fold alike in "
                                + parent
                                + ": "
                                + found
                                + ", "
                                + child.getName());
            if (!child.getCanonicalFile()
                    .equals(new File(parent.getCanonicalFile(), child.getName())))
                throw new IOException("A link in the saves: " + child);
            if (folder && !child.isDirectory())
                throw new IOException("Not a profile folder: " + child);
            found = child.getName();
        }
        return found;
    }

    private static String write(File root, Item item, Choice choice, Durable durable)
            throws IOException {
        String folderName = item.folder != null ? item.folder : match(root, item.profile, true);
        if (folderName == null) folderName = item.profile;
        File folder = new File(root, folderName);
        if (!folder.isDirectory() && !folder.mkdirs())
            throw new IOException("Cannot create " + folder);
        String existing = match(folder, item.name, false), name = item.name;
        if (existing != null) {
            if (choice == null)
                throw new IOException(
                        "A save " + existing + " appeared in " + folder + " meanwhile");
            if (choice == Choice.REPLACE) {
                name = existing; // the one name the engine finds for it
                if (!new File(folder, existing).isFile())
                    throw new IOException("Not a save file: " + new File(folder, existing));
            } else name = freeName(folder, item.name);
        }
        File target = new File(folder, name),
                temp = new File(folder, name + SaveArchive.TEMP_SUFFIX);
        boolean done = false;
        try {
            copyDurably(item.staged, temp);
            if (!sameBytes(item.staged, temp))
                throw new IOException("The written save differs from " + item.staged);
            SaveArchive.checkSave(temp);
            if (item.written > 0 && !temp.setLastModified(item.written))
                throw new IOException("Cannot date " + temp);
            if (!temp.renameTo(target))
                throw new IOException("Cannot rename " + temp + " to " + target);
            durable.syncDirectory(folder);
            done = true;
        } finally {
            if (!done && temp.exists() && !temp.delete())
                throw new IOException("Cannot remove " + temp);
        }
        return SaveArchive.entry(folderName, name);
    }

    /** "&lt;name&gt; (2)", "(3)"…: the first that folds like no entry of the folder. */
    private static String freeName(File folder, String name) throws IOException {
        for (int n = 2; n < 10000; ++n) {
            String candidate = name + " (" + n + ")";
            SaveArchive.checkName(candidate);
            if (match(folder, candidate, false) == null) return candidate;
        }
        throw new IOException("No free name for " + name + " in " + folder);
    }

    /**
     * The entry names of the archive's central directory (APPNOTE 4.3.12/4.3.16), read directly; a structure that is
     * not one is DAMAGED.
     */
    static List<String> centralNames(File zip) throws IOException {
        try (java.io.RandomAccessFile in = new java.io.RandomAccessFile(zip, "r")) {
            long length = in.length();
            int tail = (int) Math.min(length, 22 + 65535);
            byte[] end = new byte[tail];
            in.seek(length - tail);
            in.readFully(end);
            int at = -1;
            for (int i = tail - 22; i >= 0 && at < 0; --i)
                if (end[i] == 'P' && end[i + 1] == 'K' && end[i + 2] == 5 && end[i + 3] == 6)
                    at = i;
            if (at < 0)
                throw new SaveArchive.Refused(
                        SaveArchive.Refused.DAMAGED, "no end of central directory");
            int count = u16(end, at + 10);
            long size = u32(end, at + 12), offset = u32(end, at + 16);
            if (count > SaveArchive.MAX_ENTRIES)
                throw new SaveArchive.Refused(SaveArchive.Refused.UNSAFE, count + " entries");
            if (offset + size > length)
                throw new SaveArchive.Refused(
                        SaveArchive.Refused.DAMAGED, "the central directory is outside the file");
            byte[] directory = new byte[(int) size];
            in.seek(offset);
            in.readFully(directory);
            List<String> names = new ArrayList<>();
            int p = 0;
            for (int i = 0; i < count; ++i) {
                if (p + 46 > directory.length || u32(directory, p) != 0x02014b50L)
                    throw new SaveArchive.Refused(
                            SaveArchive.Refused.DAMAGED, "a broken central directory entry");
                int name = u16(directory, p + 28),
                        extra = u16(directory, p + 30),
                        comment = u16(directory, p + 32);
                if (p + 46 + name > directory.length)
                    throw new SaveArchive.Refused(
                            SaveArchive.Refused.DAMAGED, "a broken central directory entry");
                names.add(new String(directory, p + 46, name, StandardCharsets.UTF_8));
                p += 46 + name + extra + comment;
            }
            return names;
        }
    }

    private static int u16(byte[] b, int at) {
        return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8;
    }

    private static long u32(byte[] b, int at) {
        return u16(b, at) | (long) u16(b, at + 2) << 16;
    }

    private static void extract(ZipFile archive, ZipEntry entry, File staged) throws IOException {
        CRC32 crc = new CRC32();
        long copied = 0;
        try (InputStream in = archive.getInputStream(entry);
                FileOutputStream out = new FileOutputStream(staged)) {
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) != -1; ) {
                copied += n;
                if (copied > entry.getSize()) break;
                crc.update(buffer, 0, n);
                out.write(buffer, 0, n);
            }
        } catch (ZipException | java.io.EOFException damaged) {
            throw new SaveArchive.Refused(
                    SaveArchive.Refused.DAMAGED,
                    entry.getName() + ": " + damaged.getMessage(),
                    0,
                    damaged);
        } catch (IOException error) {
            throw spaceOr(staged.getParentFile(), entry.getSize() - copied, error);
        }
        if (copied != entry.getSize() || crc.getValue() != entry.getCrc())
            throw new SaveArchive.Refused(
                    SaveArchive.Refused.DAMAGED,
                    entry.getName() + ": size or CRC differs from the archive");
    }

    private static void copyDurably(File from, File to) throws IOException {
        long copied = 0;
        try (InputStream in = new FileInputStream(from);
                FileOutputStream out = new FileOutputStream(to)) {
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) != -1; ) {
                out.write(buffer, 0, n);
                copied += n;
            }
            out.getFD().sync();
        } catch (IOException error) {
            throw spaceOr(to.getParentFile(), from.length() - copied, error);
        }
    }

    private static boolean sameBytes(File a, File b) throws IOException {
        if (a.length() != b.length()) return false;
        try (InputStream x = new FileInputStream(a);
                InputStream y = new FileInputStream(b)) {
            byte[] p = new byte[65536], q = new byte[65536];
            for (int n; (n = x.read(p)) != -1; ) {
                for (int got = 0; got < n; ) {
                    int m = y.read(q, got, n - got);
                    if (m < 0) return false;
                    got += m;
                }
                for (int i = 0; i < n; ++i) if (p[i] != q[i]) return false;
            }
            return y.read() == -1;
        }
    }

    /** NO_SPACE when the folder's storage lacks `bytes` and the margin. */
    static void requireSpace(File folder, long bytes) throws SaveArchive.Refused {
        File existing = folder;
        while (existing != null && !existing.exists()) existing = existing.getParentFile();
        long free = existing == null ? 0 : existing.getUsableSpace(), need = bytes + MARGIN;
        if (free < need)
            throw new SaveArchive.Refused(
                    SaveArchive.Refused.NO_SPACE,
                    folder + ": " + free + " bytes free, " + need + " needed",
                    need - free,
                    null);
    }

    /** A write that failed on a full storage is NO_SPACE (the player can act on it); any other error stays itself. */
    static IOException spaceOr(File folder, long remaining, IOException error) {
        long free = folder == null ? 0 : folder.getUsableSpace();
        if (free < Math.max(remaining, 1) + MARGIN)
            return new SaveArchive.Refused(
                    SaveArchive.Refused.NO_SPACE,
                    folder + ": " + error.getMessage(),
                    Math.max(remaining, 1) + MARGIN - free,
                    error);
        return error;
    }
}
