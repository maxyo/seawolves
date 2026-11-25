package org.stormengine.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * The save archive «Отправить сохранение» writes ({@link SaveExport}) and «Восстановить сохранение» reads
 * ({@link SaveRestore}), and the save names the engine uses; android/game/README.md. {@link #checkSave} asks the
 * engine's own save reader whether a file would load.
 */
final class SaveArchive {
    static final String[] RESERVED = {".storm-tmp", ".storm-bak", ".storm-deleted"};
    static final String TEMP_SUFFIX = ".storm-tmp";

    /** The game's profile when nothing names another (selectCharacter.c DEFAULT_NAME, option_sl.c patch). */
    static final String DEFAULT_PROFILE = "Player";

    /** The largest save the engine reads (save_file.hpp kMaxFileSize): bounds what is copied before the check. */
    static final long MAX_SAVE_BYTES = 0x10000000L;

    /** More entries than any player has saves: a guard against a crafted archive, not a limit anyone meets. */
    static final int MAX_ENTRIES = 4096;

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
            for (String key : keys) {
                File save = SaveExport.resolve(root, key);
                long size = save.length(), written = save.lastModified(), copied;
                ZipEntry entry = new ZipEntry(key);
                entry.setTime(written);
                zip.putNextEntry(entry);
                try (InputStream in = new FileInputStream(save)) {
                    copied = Streams.copy(in, zip, Long.MAX_VALUE);
                }
                zip.closeEntry();
                if (copied != size || save.length() != size || save.lastModified() != written)
                    throw new IOException("The save changed while it was being exported: " + key);
            }
        }
    }

    /** Refuses (NOT_SAVE) a file the engine would not load, with the engine's reason. Reads the file only. */
    static void checkSave(File file) throws Refused {
        String problem = NativeArchives.checkSave(file.getPath());
        if (problem != null) throw new Refused(Refused.NOT_SAVE, file.getName() + ": " + problem);
    }
}
