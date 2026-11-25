package org.stormengine.port;

import android.content.Context;

import java.io.File;
import java.io.IOException;

/**
 * Game resources live only in app-specific storage and are imported through the
 * Storage Access Framework. Saves and logs always remain in internal storage.
 */
final class GameStorage {
    /** The APK's layers under the engine's cache root. */
    static MobileResources layers(Context context) {
        return new MobileResources(
                GameCache.root(context.getExternalCacheDir(), context.getCacheDir()),
                context.getAssets()::open);
    }

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
