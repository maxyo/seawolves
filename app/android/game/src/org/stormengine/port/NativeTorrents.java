package org.stormengine.port;

import java.io.IOException;

/** Loads only the engine's isolated downloader, never SDL or the game engine. */
final class NativeTorrents {
    // Every APK packages the downloader (build_apk.py, write_manifest.py): a failed load is an
    // error.
    static {
        System.loadLibrary("storm_torrent");
    }

    static native String inspect(String path) throws IOException;

    static native long open(String source, String privateSetDirectory) throws IOException;

    /** Status only; metadataReady says when {@link #metadata} has the file list. */
    static native String poll(long session) throws IOException;

    static native String metadata(long session) throws IOException;

    static native void start(long session, int[] selectedFileIndices) throws IOException;

    static native void pause(long session) throws IOException;

    /**
     * Blocks this (worker) thread until no resume checkpoint is pending: true when the last one was written or failed
     * (poll then reports the error), false when millis passed first.
     */
    static native boolean awaitCheckpoint(long session, int millis) throws IOException;

    static native void close(long session);
}
