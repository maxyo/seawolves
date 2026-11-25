package org.stormengine.port;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * «Отправить сохранение» / «Отправить все сохранения»: the player's saves zipped ({@link SaveArchive}) for the share
 * sheet, never game data.
 */
final class SaveExport {
    private SaveExport() {}

    /** One save: {@code key} is "<profile>/<file>", the value the picker sends back. */
    static final class Save {
        final String key, name;
        final long written;

        Save(String key, String name, long written) {
            this.key = key;
            this.name = name;
            this.written = written;
        }
    }

    /** Every profile's saves, newest first; none before the game ever ran (no SaveData yet). */
    static List<Save> list(File root) throws IOException {
        List<Save> saves = new ArrayList<>();
        if (!root.exists()) return saves;
        for (File profile : children(root)) {
            if (!profile.isDirectory()) continue;
            for (File file : children(profile))
                if (file.isFile()) {
                    String key = SaveArchive.entry(profile.getName(), file.getName());
                    try {
                        resolve(root, key);
                    } catch (IOException invalid) {
                        continue;
                    } // hidden and reserved names, links: not saves
                    saves.add(new Save(key, file.getName(), file.lastModified()));
                }
        }
        Collections.sort(saves, (a, b) -> Long.compare(b.written, a.written));
        return saves;
    }

    /** Zips the saves `keys` (from {@link #list}) into `directory`; the archive is returned. */
    static File zip(File root, List<String> keys, File directory, String stamp) throws IOException {
        if (keys.isEmpty()) throw new IOException("No save to export");
        for (String key : keys)
            resolve(root, key); // nothing is allocated for a key that is not a save
        // Unique files keep a second export from replacing a URI a receiver still reads. Only cache
        // is written.
        File archive =
                File.createTempFile(
                        (keys.size() == 1 ? "seawolves-save-" : "seawolves-saves-") + stamp + "-",
                        ".zip",
                        directory);
        boolean complete = false;
        try {
            SaveArchive.write(root, keys, archive);
            complete = true;
        } finally {
            if (!complete && !archive.delete()) archive.deleteOnExit();
        }
        return archive;
    }

    /** The save file of `key`: exactly one profile folder and one save file below the root, nothing else. */
    static File resolve(File root, String key) throws IOException {
        String[] parts;
        try {
            parts = SaveArchive.split(key);
        } catch (SaveArchive.Refused refused) {
            throw new IOException("Not a save: " + key, refused);
        }
        File file = new File(new File(root, parts[0]), parts[1]);
        File expected = new File(new File(root.getCanonicalFile(), parts[0]), parts[1]);
        if (!file.getCanonicalFile().equals(expected))
            throw new IOException("A save must not be a link: " + key);
        if (!file.isFile()) throw new IOException("The save is gone: " + file);
        return file;
    }

    private static File[] children(File folder) throws IOException {
        File[] children = folder.listFiles();
        if (children == null) throw new IOException("Cannot list " + folder);
        return children;
    }
}
