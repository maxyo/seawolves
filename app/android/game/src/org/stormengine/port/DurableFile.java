package org.stormengine.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** Replaces a small file whole: a same-directory rename is atomic, so a reader sees the old or the new bytes. */
final class DurableFile {
    static void write(File target, byte[] bytes) throws IOException {
        File temp = new File(target.getPath() + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temp)) {
            output.write(bytes);
            output.getFD().sync();
        }
        if (!temp.renameTo(target)) throw new IOException("Cannot replace " + target);
    }

    private DurableFile() {}
}
