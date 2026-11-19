package org.stormengine.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * The one description of the save archive that «Отправить сохранение» / «Отправить все сохранения» write
 * ({@link SaveExport}) and «Восстановить сохранение» reads ({@link SaveRestore}): a ZIP whose every entry is one save
 * file, named "&lt;profile&gt;/&lt;file&gt;" exactly as it lies under {@code <filesDir>/storm/SaveData}, with the save's
 * modification time as the entry time. Nothing else is in it: no folders, no game data, no manifest. Plain Java (API
 * 24): checked on the host (tests/android_save_export, tests/android_save_restore).
 *
 * <p>The names follow the engine: saves are {@code SAVE\<profile>\<file>} (engine android_writable_storage.hpp maps
 * them to SaveData/&lt;profile&gt;/&lt;file&gt; and compares names ASCII-case-insensitively), "netsave" is not a profile
 * (mobile_save_facts.cpp SaveRecordsOfAllProfiles), and the suffixes of the atomic save machinery (save_file.hpp
 * .storm-tmp / .storm-bak, save_trash.hpp .storm-deleted) are never saves. A save file is the container of
 * save_file.hpp; {@link #checkSave} reads it the way the engine's readState and readExtData do.
 */
final class SaveArchive {
    static final String[] RESERVED = {".storm-tmp", ".storm-bak", ".storm-deleted"};
    static final String TEMP_SUFFIX = ".storm-tmp";

    /** The game's profile when nothing names another (selectCharacter.c DEFAULT_NAME, option_sl.c patch). */
    static final String DEFAULT_PROFILE = "Player";

    /** save_file.hpp: kMaxSectionSize, and readFile refuses files over twice that. */
    static final long MAX_SECTION = 0x8000000L, MAX_SAVE_BYTES = 2 * MAX_SECTION;

    /** More entries than any player has saves: a guard against a crafted archive, not a limit anyone meets. */
    static final int MAX_ENTRIES = 4096;

    private static final int HEADER = 40, FIXED = HEADER + 8;

    private SaveArchive() {}

    /** Why a file cannot become a save; {@link #reason} is the player-facing message key (about.restore.&lt;reason&gt;). */
    static final class Refused extends IOException {
        static final String NOT_SAVE = "not_save",
                DAMAGED = "damaged",
                UNSAFE = "unsafe",
                NO_SPACE = "no_space";
        final String reason;

        /** NO_SPACE: the bytes still to free. */
        final long bytes;

        Refused(String reason, String detail) {
            this(reason, detail, 0, null);
        }

        Refused(String reason, String detail, long bytes, Throwable cause) {
            super(reason + ": " + detail, cause);
            this.reason = reason;
            this.bytes = bytes;
        }
    }

    static File root(File filesDir) {
        return new File(filesDir, "storm/SaveData");
    }

    static boolean reserved(String name) {
        for (String suffix : RESERVED) if (name.endsWith(suffix)) return true;
        return false;
    }

    /** The engine's comparison of save path components (android_writable_storage.hpp folded()). */
    static String folded(String name) {
        StringBuilder out = new StringBuilder(name.length());
        for (int i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            out.append(c >= 'A' && c <= 'Z' ? (char) (c + ('a' - 'A')) : c);
        }
        return out.toString();
    }

    /** A component a save path may have: one plain name, never hidden, never the engine's own, a file name's length. */
    private static boolean plain(String name) {
        return name != null
                && !name.isEmpty()
                && name.indexOf('/') < 0
                && name.indexOf('\\') < 0
                && name.indexOf('\0') < 0
                && !name.startsWith(".")
                && !reserved(name)
                && name.getBytes(StandardCharsets.UTF_8).length <= 255;
    }

    static void checkProfile(String profile) throws Refused {
        if (!plain(profile) || folded(profile).equals("netsave"))
            throw new Refused(Refused.UNSAFE, "not a profile name: " + profile);
    }

    static void checkName(String name) throws Refused {
        if (!plain(name)) throw new Refused(Refused.UNSAFE, "not a save name: " + name);
    }

    /** The entry (and picker key) of a save. */
    static String entry(String profile, String name) {
        return profile + "/" + name;
    }

    /** {profile, name} of an entry; anything but exactly two plain components is refused (zip-slip, folders, links). */
    static String[] split(String entry) throws Refused {
        int slash = entry == null ? -1 : entry.indexOf('/');
        if (slash <= 0) throw new Refused(Refused.UNSAFE, "not a save entry: " + entry);
        String profile = entry.substring(0, slash), name = entry.substring(slash + 1);
        checkProfile(profile);
        checkName(name);
        return new String[] {profile, name};
    }

    /** Writes `saves` (keys of {@link #entry}, files below `root`) into the new archive `out`. */
    static void write(File root, List<String> keys, File out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(out))) {
            byte[] buffer = new byte[65536];
            for (String key : keys) {
                File save = SaveExport.resolve(root, key);
                long size = save.length(), written = save.lastModified(), copied = 0;
                ZipEntry entry = new ZipEntry(key);
                entry.setTime(written);
                zip.putNextEntry(entry);
                try (InputStream in = new FileInputStream(save)) {
                    for (int n; (n = in.read(buffer)) != -1; ) {
                        zip.write(buffer, 0, n);
                        copied += n;
                    }
                }
                zip.closeEntry();
                if (copied != size || save.length() != size || save.lastModified() != written)
                    throw new IOException("The save changed while it was being exported: " + key);
            }
        }
    }

    /**
     * Refuses (NOT_SAVE) a file the engine would not load: save_file.hpp readFile's size limit, readState (the script
     * state inflates cleanly to at most its declared size) and readExtData (the description, when there is one).
     */
    static void checkSave(File file) throws IOException {
        long size = file.length();
        if (!file.isFile() || size < FIXED || size > MAX_SAVE_BYTES)
            throw new Refused(Refused.NOT_SAVE, file.getName() + ": " + size + " bytes");
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            byte[] head = new byte[FIXED];
            in.readFully(head);
            long extOffset = u32(head, 32),
                    extSize = u32(head, 36),
                    rawSize = u32(head, 40),
                    packedSize = u32(head, 44);
            if (packedSize > size - FIXED)
                throw new Refused(Refused.NOT_SAVE, file.getName() + ": the state is truncated");
            inflate(in, FIXED, packedSize, rawSize, file.getName() + " state");
            if (extSize == 0) return;
            if (extOffset < HEADER || extOffset > size - 4)
                throw new Refused(
                        Refused.NOT_SAVE, file.getName() + ": description outside the file");
            byte[] word = new byte[4];
            in.seek(extOffset);
            in.readFully(word);
            long extPacked = u32(word, 0);
            if (extPacked > size - extOffset - 4)
                throw new Refused(
                        Refused.NOT_SAVE, file.getName() + ": the description is truncated");
            inflate(in, extOffset + 4, extPacked, extSize, file.getName() + " description");
        }
    }

    private static long u32(byte[] data, int at) {
        return (data[at] & 0xffL)
                | (data[at + 1] & 0xffL) << 8
                | (data[at + 2] & 0xffL) << 16
                | (data[at + 3] & 0xffL) << 24;
    }

    /** save_file.hpp unpack(): a zlib stream (its Adler-32 checked) that ends within `packed` bytes and gives at most `declared`. */
    private static void inflate(
            RandomAccessFile in, long offset, long packed, long declared, String what)
            throws IOException {
        if (packed == 0 || packed > MAX_SECTION || declared == 0 || declared > MAX_SECTION)
            throw new Refused(Refused.NOT_SAVE, what + ": implausible section size");
        in.seek(offset);
        Inflater inflater = new Inflater();
        try {
            byte[] input = new byte[65536], output = new byte[65536];
            long left = packed, produced = 0;
            while (!inflater.finished()) {
                if (inflater.needsInput()) {
                    if (left == 0)
                        throw new Refused(
                                Refused.NOT_SAVE, what + ": the compressed data is truncated");
                    int n = (int) Math.min(input.length, left);
                    in.readFully(input, 0, n);
                    left -= n;
                    inflater.setInput(input, 0, n);
                }
                int n = inflater.inflate(output);
                produced += n;
                if (produced > declared)
                    throw new Refused(Refused.NOT_SAVE, what + ": larger than declared");
                if (n == 0 && !inflater.finished() && !inflater.needsInput())
                    throw new Refused(
                            Refused.NOT_SAVE, what + ": the compressed data needs a dictionary");
            }
        } catch (DataFormatException corrupt) {
            throw new Refused(
                    Refused.NOT_SAVE, what + ": the compressed data is corrupt", 0, corrupt);
        } finally {
            inflater.end();
        }
    }
}
