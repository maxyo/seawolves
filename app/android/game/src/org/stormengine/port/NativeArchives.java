package org.stormengine.port;

import java.io.IOException;

/**
 * The engine's archive reader (libstorm_archive). Reads inert data only; nothing
 * found in a source is run. Publication into a game is a separate transaction.
 * Contract: engine android/archive/README.md.
 */
final class NativeArchives {
    /** Stages: 1 reading, 2 nested containers, 3 installing. Return true to cancel. */
    interface Progress {
        boolean onProgress(int stage, String path, long bytes, long totalBytes, long files);
    }

    private static final Throwable failure;

    static {
        Throwable error = null;
        try {
            System.loadLibrary("storm_archive");
        } catch (UnsatisfiedLinkError | SecurityException unavailable) {
            error = unavailable;
        }
        failure = error;
    }

    static void requireAvailable() throws IOException {
        if (failure != null)
            throw new ArchiveInstaller.Failure(
                    ArchiveInstaller.Failure.UNAVAILABLE,
                    "Archive reader is unavailable in this build",
                    failure);
    }

    /**
     * [status, "", error, json]: every game root in a private source directory, each identified against the
     * script patch layer directory with the engine's own rule (null: not identified).
     */
    static native String[] discover(
            String sourceDir,
            String emptyScratchDir,
            String password,
            String patchLayer,
            long[] limits,
            Progress progress);

    /** [status, "", error, json]: the sets of a script patch layer directory and the game files they identify a copy by. */
    static native String[] patchLayer(String patchLayer);

    /** [status, "", error, json]: one discovered root into an empty staging directory. */
    static native String[] extractRoot(
            String sourceDir,
            String scratchDir,
            String password,
            String rootId,
            String emptyStaging,
            long[] limits,
            Progress progress);
}
