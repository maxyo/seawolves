package org.stormengine.port;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;

import java.io.File;
import java.io.IOException;

/** GitHub build: full download library, including torrents in the :downloads process. */
final class Flavor {
    static final String NAME = "github";
    static final boolean DOWNLOAD_LIBRARY = true;

    private Flavor() {}

    /** The finished files of a download set, for ImportCoordinator.startTree(File). */
    static File downloadedSet(Context context, String id) throws IOException {
        return TorrentDownloadService.catalog(context).importSource(id);
    }

    /** A set the game was installed from is used up: removed by the download process (which owns the library). */
    static void removeDownloadedSet(Context context, String id) {
        TorrentDownloadService.command(context, TorrentDownloadService.DELETE, id, null);
    }

    static void openLibrary(Activity activity) {
        activity.startActivity(new Intent(activity, DownloadLibraryActivity.class));
    }
}
