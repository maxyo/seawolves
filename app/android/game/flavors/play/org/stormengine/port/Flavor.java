package org.stormengine.port;

import android.app.Activity;
import android.content.Context;

import java.io.File;
import java.io.IOException;

/**
 * Google Play build: no download library, torrent client, foreground service or
 * network access. Players import their own copy (folder, ZIP, archive or
 * installer) through the system file picker only.
 */
final class Flavor {
    static final String NAME = "play";
    static final boolean DOWNLOAD_LIBRARY = false;

    private Flavor() {}

    static void openLibrary(Activity activity) {}

    static File downloadedSet(Context context, String id) throws IOException {
        throw new IOException("This build has no download library");
    }

    static void removeDownloadedSet(Context context, String id) {
        throw new IllegalStateException("This build has no download library");
    }
}
