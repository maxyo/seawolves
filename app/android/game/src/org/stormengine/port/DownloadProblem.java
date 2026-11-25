package org.stormengine.port;

import java.io.IOException;

/**
 * A download library failure the player can act on, named by its kind; the launcher's download
 * screen says it in the player's words (pack strings downloads.problem.&lt;kind&gt;) and logs the
 * message. Kinds: storage-full, storage, transfer, metadata (the torrent client's), magnet-prefix,
 * magnet-damaged, open-failed, too-large, files-missing, refresh-first, import-running,
 * pause-first, exit-game-first. Any other exception is shown as kind "failed".
 */
final class DownloadProblem extends IOException {
    final String kind;

    DownloadProblem(String kind, String message) {
        super(message);
        this.kind = kind;
    }
}
