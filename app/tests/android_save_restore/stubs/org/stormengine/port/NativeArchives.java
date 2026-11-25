package org.stormengine.port;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** The engine's save check as SaveArchive calls it: a file the check writes starting with "bad:" is refused. The
 *  engine's reader itself is checked by the engine's save_check. */
final class NativeArchives {
    static final String REFUSED = "bad:";

    private NativeArchives() {}

    static String checkSave(String path) {
        byte[] head = new byte[REFUSED.length()];
        try (InputStream in = new FileInputStream(path)) {
            int read = in.read(head);
            return read == head.length
                            && new String(head, StandardCharsets.US_ASCII).equals(REFUSED)
                    ? "rawSize 0 is out of range"
                    : null;
        } catch (IOException missing) {
            return "cannot open file";
        }
    }
}
