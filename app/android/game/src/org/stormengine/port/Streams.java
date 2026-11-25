package org.stormengine.port;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** Stream copies with a size limit (the platform's InputStream.transferTo and readAllBytes are API 33). */
final class Streams {
    private Streams() {}

    /** More bytes than the copy's limit. */
    static final class TooLarge extends IOException {
        TooLarge(long limit) {
            super("more than " + limit + " bytes");
        }
    }

    /** Told each chunk before it is written; it may refuse the copy by throwing. */
    interface Chunks {
        void next(int bytes) throws IOException;
    }

    /** Copies all of `in` to `out` and returns the bytes copied; past `limit` bytes it throws TooLarge. */
    static long copy(InputStream in, OutputStream out, long limit, Chunks chunks)
            throws IOException {
        byte[] buffer = new byte[65536];
        long total = 0;
        for (int n; (n = in.read(buffer)) != -1; ) {
            if (n > limit - total) throw new TooLarge(limit);
            if (chunks != null) chunks.next(n);
            out.write(buffer, 0, n);
            total += n;
        }
        return total;
    }

    static long copy(InputStream in, OutputStream out, long limit) throws IOException {
        return copy(in, out, limit, null);
    }

    /** All of `in` (closed afterwards), at most `limit` bytes; more throws TooLarge. */
    static byte[] read(InputStream in, int limit) throws IOException {
        try (InputStream input = in;
                ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            copy(input, out, limit);
            return out.toByteArray();
        }
    }
}
