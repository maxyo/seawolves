package org.stormengine.port;

import android.content.res.AssetManager;

/**
 * The engine's archive reader (libstorm_archive). Reads inert data only; nothing
 * found in a source is run. Publication into a game is a separate transaction.
 * Contract: engine android/archive/README.md; result[0] is one of {@link ArchiveStatus}.
 */
final class NativeArchives {
    /** Stages: 1 reading, 2 nested containers, 3 installing. Return true to cancel. */
    interface Progress {
        boolean onProgress(int stage, String path, long bytes, long totalBytes, long files);
    }

    // Every APK packages the reader (build_apk.py, write_manifest.py): a failed load is an error.
    static {
        System.loadLibrary("storm_archive");
    }

    /**
     * [status, error, json]: every game root in a private source directory, each identified with the engine's own
     * rule against the script patch layer the APK carries, read through `assets` (null: not identified).
     */
    static native String[] discover(
            String sourceDir,
            String emptyScratchDir,
            String password,
            AssetManager assets,
            long[] limits,
            Progress progress);

    /** [status, error, json]: the sets of the APK's script patch layer and the game files they identify a copy by. */
    static native String[] patchLayer(AssetManager assets);

    /** [status, error]: removes a file or tree by the bytes of its names, never following a link. */
    static native String[] removeTree(String absolutePath);

    /** null when the engine would load the save at `path`, else the reason, naming the field at fault. Reads only. */
    static native String checkSave(String path);

    /** [status, error, json]: one discovered root into an empty staging directory. */
    static native String[] extractRoot(
            String sourceDir,
            String scratchDir,
            String password,
            String rootId,
            String emptyStaging,
            long[] limits,
            Progress progress);
}
