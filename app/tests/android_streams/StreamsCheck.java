package org.stormengine.port;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;

/** Streams: a bounded copy takes exactly its limit, refuses one byte more, and shows each chunk before writing it. */
public final class StreamsCheck {
    public static void main(String[] args) throws Exception {
        byte[] data = new byte[200_000];
        for (int i = 0; i < data.length; i++) data[i] = (byte) i;
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        List<Integer> chunks = new ArrayList<>();
        long copied =
                Streams.copy(new ByteArrayInputStream(data), out, data.length, n -> chunks.add(n));
        assert copied == data.length && java.util.Arrays.equals(out.toByteArray(), data);
        assert chunks.stream().mapToInt(Integer::intValue).sum() == data.length;
        try {
            Streams.read(new ByteArrayInputStream(data), data.length - 1);
            throw new AssertionError("one byte over the limit was accepted");
        } catch (Streams.TooLarge expected) {
        }
        ByteArrayOutputStream partial = new ByteArrayOutputStream();
        try {
            Streams.copy(
                    new ByteArrayInputStream(data),
                    partial,
                    data.length,
                    n -> {
                        if (partial.size() > 0) throw new java.io.IOException("refused");
                    });
            throw new AssertionError("a refusing chunk callback was ignored");
        } catch (java.io.IOException expected) {
            assert partial.size() == 65536 : "the refused chunk is not written";
        }
        System.out.println(
                "PASS: Streams copies to its limit, refuses more, shows each chunk first");
    }
}
