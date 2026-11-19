package org.stormengine.port;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

/** Tombstone reads debuggerd's protobuf (tombstone.proto field numbers) into the crash log's text. */
public final class TombstoneCheck {
    private static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    /** A minimal protobuf writer: fields in the order given. */
    static final class Proto {
        final ByteArrayOutputStream out = new ByteArrayOutputStream();

        private void varint(long value) {
            while ((value & ~0x7fL) != 0) {
                out.write((int) ((value & 0x7f) | 0x80));
                value >>>= 7;
            }
            out.write((int) value);
        }

        Proto number(int field, long value) {
            varint((long) field << 3);
            varint(value);
            return this;
        }

        Proto bytes(int field, byte[] value) {
            varint((long) field << 3 | 2);
            varint(value.length);
            out.write(value, 0, value.length);
            return this;
        }

        Proto string(int field, String value) {
            return bytes(field, value.getBytes(StandardCharsets.UTF_8));
        }

        Proto message(int field, Proto value) {
            return bytes(field, value.out.toByteArray());
        }

        Proto fixed64(int field, long value) {
            varint((long) field << 3 | 1);
            for (int i = 0; i < 8; ++i) out.write((int) (value >>> (8 * i)) & 0xff);
            return this;
        }
    }

    static Proto frame(long relPc, String file, String function, long offset, String buildId) {
        Proto frame =
                new Proto()
                        .number(1, relPc)
                        .number(2, 0x7000000000L + relPc)
                        .string(6, file)
                        .string(8, buildId);
        if (function != null) frame.string(4, function).number(5, offset);
        return frame;
    }

    public static void main(String[] args) {
        String engine = "/data/app/~~x/org.stormengine.port.game-y/lib/arm64/libengine.so";
        Proto crashing =
                new Proto()
                        .number(1, 1250)
                        .string(2, "SDLThread")
                        .message(4, frame(0x1234ab, engine, "Lights::Update(float)", 44, "0a1b2c"))
                        .message(4, frame(0x2000, engine, null, 0, "0a1b2c"))
                        .message(
                                4,
                                frame(
                                        0x5c3f0,
                                        "/apex/com.android.runtime/lib64/bionic/libc.so",
                                        "__start_thread",
                                        64,
                                        "ffee"));
        Proto other =
                new Proto()
                        .number(1, 1234)
                        .string(2, "main")
                        .message(4, frame(0x10, "/system/lib64/libart.so", "art::Run", 4, "aa"));
        Proto tombstone =
                new Proto()
                        .number(1, 1) // arm64
                        .string(2, "google/sdk_gphone64/emu64a:14/UE1A/1:userdebug/dev-keys")
                        .string(4, "2026-11-15 10:00:00.000000000+0000")
                        .number(5, 1234)
                        .number(6, 1250)
                        .number(7, 10123)
                        .string(9, "org.stormengine.port.game:game")
                        .message(
                                10,
                                new Proto()
                                        .number(1, 11)
                                        .string(2, "SIGSEGV")
                                        .number(3, 1)
                                        .string(4, "SEGV_MAPERR")
                                        .number(8, 1)
                                        .number(9, 0))
                        .string(14, "")
                        .message(15, new Proto().string(1, "null pointer dereference"))
                        .message(16, new Proto().number(1, 1234).message(2, other))
                        .message(16, new Proto().number(1, 1250).message(2, crashing))
                        .message(
                                17,
                                new Proto()
                                        .number(1, 0x7000000000L)
                                        .number(2, 0x7000800000L)
                                        .number(3, 0)
                                        .number(4, 1)
                                        .number(6, 1)
                                        .string(7, engine)
                                        .string(8, "0a1b2c"))
                        .message(
                                17,
                                new Proto()
                                        .number(1, 0x7100000000L)
                                        .number(2, 0x7100100000L)
                                        .number(4, 1)
                                        .number(6, 0)
                                        .string(7, engine))
                        .fixed64(
                                22,
                                4096); // page_size as a fixed field: an unknown wire type 1 field
        // is skipped
        String text = Tombstone.describe(tombstone.out.toByteArray());
        check(
                text.contains(
                        "ABI: arm64, pid 1234, tid 1250, cmdline:"
                                + " org.stormengine.port.game:game\n"),
                "process line:\n" + text);
        check(
                text.contains("signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0\n"),
                "signal line:\n" + text);
        check(text.contains("Cause: null pointer dereference\n"), "cause");
        check(
                text.contains(
                        "backtrace of thread 1250 (SDLThread):\n"
                                + "    #00 pc 00000000001234ab  "
                                + engine
                                + " (Lights::Update(float)+44) (BuildId: 0a1b2c)\n"
                                + "    #01 pc 0000000000002000  "
                                + engine
                                + " (BuildId: 0a1b2c)\n"
                                + "    #02 pc 000000000005c3f0 "
                                + " /apex/com.android.runtime/lib64/bionic/libc.so"
                                + " (__start_thread+64) (BuildId: ffee)\n"),
                "the crashing thread's frames, debuggerd's format:\n" + text);
        check(!text.contains("art::Run"), "only the crashing thread");
        check(
                text.contains(
                        "libraries in the backtrace (load address, build id):\n"
                                + "    0x7000000000-0x7000800000 "
                                + engine
                                + " (BuildId: 0a1b2c)\n"),
                "the engine library's load address:\n" + text);
        check(
                text.contains("libc.so (BuildId: ffee), no mapping recorded"),
                "a frame's library without a mapping");
        boolean refused = false;
        try {
            Tombstone.describe(new byte[] {0x0a, 0x7f});
        } catch (IllegalArgumentException expected) {
            refused = true;
        }
        check(refused, "a truncated message is refused");
        System.out.println(
                "PASS: tombstone protobuf -> crashing thread backtrace, signal, cause, library load"
                        + " addresses");
    }
}
