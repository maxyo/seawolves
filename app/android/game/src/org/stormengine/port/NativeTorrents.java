package org.stormengine.port;

import java.io.IOException;

/** Loads only the engine's isolated downloader, never SDL or the game engine. */
final class NativeTorrents {
    private static final Throwable failure;

    static {
        Throwable error = null;
        try {
            System.loadLibrary("storm_torrent");
        } catch (UnsatisfiedLinkError | SecurityException unavailable) {
            error = unavailable;
        }
        failure = error;
    }

    static void requireAvailable() throws IOException {
        if (failure != null)
            throw new IOException("Torrent downloader is unavailable in this build", failure);
    }

    static native String inspect(String path) throws IOException;

    static native long open(String source, String privateSetDirectory) throws IOException;

    /** Status only; metadataReady says when {@link #metadata} has the file list. */
    static native String poll(long session) throws IOException;

    static native String metadata(long session) throws IOException;

    static native void start(long session, int[] selectedFileIndices) throws IOException;

    static native void pause(long session) throws IOException;

    static native void close(long session);
}
