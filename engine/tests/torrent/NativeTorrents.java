package org.stormengine.port;

import java.io.IOException;

/** Host regression loads the actual production JNI implementation. */
public final class NativeTorrents {
    public static native String inspect(String path) throws IOException;
    public static native long open(String source, String staging) throws IOException;
    public static native String poll(long handle) throws IOException;
    public static native String metadata(long handle) throws IOException;
    public static native void start(long handle, int[] indices) throws IOException;
    public static native void pause(long handle) throws IOException;
    public static native boolean awaitCheckpoint(long handle, int millis) throws IOException;
    public static native void close(long handle);
    interface Operation { void run() throws IOException; }
    static void expectIOException(Operation operation, String expected) throws Exception {
        try { operation.run(); }
        catch (IOException error) {
            if (error.getMessage() == null || !error.getMessage().contains(expected))
                throw new AssertionError("Wrong IOException text: " + error.getMessage());
            return;
        }
        throw new AssertionError("Expected IOException");
    }
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        for (int i=0; i<100; ++i) {
            String json=inspect(args[1]);
            if (!json.contains("Корсары 🏴‍☠️") || !json.contains("setup.exe") ||
                !json.contains("\"verifiedGame\":false") || !json.contains("installer-candidate"))
                throw new AssertionError("Unicode metadata did not round-trip: " + json);
        }
        expectIOException(() -> inspect(null), "null string");
        expectIOException(() -> inspect("bad\u0000path"), "U+0000 in text at unit 3");
        expectIOException(() -> inspect("bad\ud800path"), "unpaired UTF-16 surrogate at unit 3");
        expectIOException(() -> inspect("bad\udfffpath"), "unpaired UTF-16 surrogate at unit 3");
        expectIOException(() -> inspect(args[1] + "-нет-😀"), "нет-😀");
        expectIOException(() -> inspect(args[2]), "Torrent must be a dictionary");
        expectIOException(() -> poll(987654), "Torrent session is closed");
        expectIOException(() -> metadata(987654), "Torrent session is closed");
        expectIOException(() -> start(987654, new int[] {0}), "Torrent session is closed");
        expectIOException(() -> awaitCheckpoint(987654, 0), "Torrent session is closed");
        expectIOException(() -> start(987654, null), "Missing file selection");
        expectIOException(() -> start(987654, new int[0]), "Select between 1 and 100000 files");
        expectIOException(() -> pause(987654), "Torrent session is closed");
        close(987654);
        System.out.println("PASS actual production JNI under -Xcheck:jni: Unicode paths/JSON/errors, malformed UTF-16, nulls, closed handles");
    }
}
