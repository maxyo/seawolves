package org.stormengine.port;

import android.content.Context;

import java.io.File;
import java.io.IOException;

/**
 * Game resources live only in app-specific storage and are imported through the
 * Storage Access Framework. Saves and logs always remain in internal storage.
 */
final class GameStorage {
    static File imported(Context context) {
        File root = context.getExternalFilesDir(null);
        return new File(root == null ? context.getFilesDir() : root, "game");
    }

    static void validate(File directory) throws IOException {
        ResourceImporter.validate(directory);
    }

    static String readyPath(Context context) throws IOException {
        File directory = imported(context);
        ResourceImporter.requirePublished(directory);
        validate(directory);
        return directory.getCanonicalPath();
    }
}
