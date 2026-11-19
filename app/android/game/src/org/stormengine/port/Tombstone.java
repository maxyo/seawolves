package org.stormengine.port;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The native crash report the platform keeps for {@code ApplicationExitInfo.getTraceInputStream()} of a
 * REASON_CRASH_NATIVE exit (Android 12+): debuggerd's tombstone, protobuf (AOSP
 * system/core/debuggerd/proto/tombstone.proto), read here into the text debuggerd writes to the crash log:
 * the signal, the abort message, the crashing thread's backtrace with each frame's relative pc, library and
 * build id (what tools/android/symbolize.py resolves against the kept symbols), and the build ids and load
 * addresses of the libraries in those frames. A translated (native bridge) process also gets its guest
 * backtrace. Unknown fields are skipped; a malformed message is an IllegalArgumentException.
 */
final class Tombstone {
    private Tombstone() {}

    /** Frames printed per backtrace: a crash's cause is in its first frames; a runaway recursion is not printed whole. */
    static final int MAX_FRAMES = 64;

    private static final String[] ARCHES = {"arm", "arm64", "x86", "x86_64", "riscv64"};

    /** One protobuf message: field number to its values in order (Long for varints and fixed, byte[] for length-delimited). */
    static final class Message {
        final Map<Integer, List<Object>> fields = new TreeMap<>();

        static Message parse(byte[] data) {
            return parse(data, 0, data.length);
        }

        static Message parse(byte[] data, int from, int to) {
            Message message = new Message();
            int[] at = {from};
            while (at[0] < to) {
                long key = varint(data, at, to);
                int field = (int) (key >>> 3), type = (int) (key & 7);
                if (field <= 0)
                    throw new IllegalArgumentException(
                            "tombstone: field number " + field + " at byte " + at[0]);
                Object value;
                switch (type) {
                    case 0:
                        value = varint(data, at, to);
                        break;
                    case 1:
                        value = fixed(data, at, to, 8);
                        break;
                    case 5:
                        value = fixed(data, at, to, 4);
                        break;
                    case 2:
                        {
                            long length = varint(data, at, to);
                            if (length < 0 || length > to - at[0])
                                throw new IllegalArgumentException(
                                        "tombstone: field " + field + " runs past its message");
                            byte[] bytes = new byte[(int) length];
                            System.arraycopy(data, at[0], bytes, 0, bytes.length);
                            at[0] += bytes.length;
                            value = bytes;
                            break;
                        }
                    default:
                        throw new IllegalArgumentException(
                                "tombstone: wire type " + type + " of field " + field);
                }
                List<Object> values = message.fields.get(field);
                if (values == null) message.fields.put(field, values = new ArrayList<>());
                values.add(value);
            }
            return message;
        }

        private static long varint(byte[] data, int[] at, int to) {
            long result = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (at[0] >= to) throw new IllegalArgumentException("tombstone: truncated varint");
                int b = data[at[0]++] & 0xff;
                result |= (long) (b & 0x7f) << shift;
                if ((b & 0x80) == 0) return result;
            }
            throw new IllegalArgumentException("tombstone: varint longer than 64 bits");
        }

        private static long fixed(byte[] data, int[] at, int to, int size) {
            if (to - at[0] < size)
                throw new IllegalArgumentException("tombstone: truncated fixed field");
            long result = 0;
            for (int i = 0; i < size; ++i) result |= (long) (data[at[0]++] & 0xff) << (8 * i);
            return result;
        }

        long number(int field) {
            List<Object> v = fields.get(field);
            return v == null ? 0 : (Long) v.get(v.size() - 1);
        }

        String string(int field) {
            List<Object> v = fields.get(field);
            return v == null
                    ? ""
                    : new String((byte[]) v.get(v.size() - 1), StandardCharsets.UTF_8);
        }

        Message message(int field) {
            List<Object> v = fields.get(field);
            return v == null ? new Message() : parse((byte[]) v.get(v.size() - 1));
        }

        List<Message> messages(int field) {
            List<Message> result = new ArrayList<>();
            List<Object> v = fields.get(field);
            if (v != null) for (Object bytes : v) result.add(parse((byte[]) bytes));
            return result;
        }

        List<String> strings(int field) {
            List<String> result = new ArrayList<>();
            List<Object> v = fields.get(field);
            if (v != null)
                for (Object bytes : v)
                    result.add(new String((byte[]) bytes, StandardCharsets.UTF_8));
            return result;
        }
    }

    // Field numbers of tombstone.proto.
    private static final int ARCH = 1,
            FINGERPRINT = 2,
            TIMESTAMP = 4,
            PID = 5,
            TID = 6,
            COMMAND_LINE = 9,
            SIGNAL = 10,
            ABORT = 14,
            CAUSES = 15,
            THREADS = 16,
            MAPPINGS = 17,
            GUEST_ARCH = 24,
            GUEST_THREADS = 25;
    private static final int SIGNAL_NUMBER = 1,
            SIGNAL_NAME = 2,
            SIGNAL_CODE = 3,
            SIGNAL_CODE_NAME = 4,
            HAS_SENDER = 5,
            SENDER_UID = 6,
            SENDER_PID = 7,
            HAS_FAULT_ADDRESS = 8,
            FAULT_ADDRESS = 9;
    private static final int THREAD_NAME = 2, BACKTRACE = 4, BACKTRACE_NOTE = 7, UNREADABLE_ELF = 9;
    private static final int REL_PC = 1, FUNCTION = 4, FUNCTION_OFFSET = 5, FILE = 6, BUILD_ID = 8;
    private static final int MAP_BEGIN = 1,
            MAP_END = 2,
            MAP_OFFSET = 3,
            MAP_EXECUTE = 6,
            MAP_NAME = 7,
            MAP_BUILD_ID = 8,
            MAP_LOAD_BIAS = 9;

    static String describe(byte[] proto) {
        Message tombstone = Message.parse(proto);
        StringBuilder out = new StringBuilder();
        long tid = tombstone.number(TID);
        out.append("ABI: ")
                .append(arch(tombstone.number(ARCH)))
                .append(", pid ")
                .append(tombstone.number(PID))
                .append(", tid ")
                .append(tid)
                .append(", cmdline: ")
                .append(String.join(" ", tombstone.strings(COMMAND_LINE)))
                .append('\n');
        if (!tombstone.string(TIMESTAMP).isEmpty())
            out.append("Timestamp: ").append(tombstone.string(TIMESTAMP)).append('\n');
        if (!tombstone.string(FINGERPRINT).isEmpty())
            out.append("Build fingerprint: ").append(tombstone.string(FINGERPRINT)).append('\n');
        Message signal = tombstone.message(SIGNAL);
        out.append("signal ")
                .append(signal.number(SIGNAL_NUMBER))
                .append(" (")
                .append(signal.string(SIGNAL_NAME))
                .append("), code ")
                .append((int) signal.number(SIGNAL_CODE))
                .append(" (")
                .append(signal.string(SIGNAL_CODE_NAME))
                .append(")");
        if (signal.number(HAS_SENDER) != 0)
            out.append(", sent by pid ")
                    .append(signal.number(SENDER_PID))
                    .append(" uid ")
                    .append(signal.number(SENDER_UID));
        out.append(", fault addr ")
                .append(
                        signal.number(HAS_FAULT_ADDRESS) != 0
                                ? hex(signal.number(FAULT_ADDRESS))
                                : "--------")
                .append('\n');
        if (!tombstone.string(ABORT).isEmpty())
            out.append("Abort message: '").append(tombstone.string(ABORT)).append("'\n");
        for (Message cause : tombstone.messages(CAUSES))
            out.append("Cause: ").append(cause.string(1)).append('\n');
        Map<String, String> libraries = new LinkedHashMap<>();
        thread(out, "backtrace", threads(tombstone, THREADS), tid, libraries);
        if (tombstone.fields.containsKey(GUEST_THREADS))
            thread(
                    out,
                    "guest backtrace (" + arch(tombstone.number(GUEST_ARCH)) + ", native bridge)",
                    threads(tombstone, GUEST_THREADS),
                    tid,
                    libraries);
        if (!libraries.isEmpty()) {
            out.append("libraries in the backtrace (load address, build id):\n");
            for (Message map : tombstone.messages(MAPPINGS)) {
                String name = map.string(MAP_NAME);
                if (map.number(MAP_EXECUTE) == 0 || !libraries.containsKey(name)) continue;
                out.append("    ")
                        .append(hex(map.number(MAP_BEGIN) - map.number(MAP_OFFSET)))
                        .append('-')
                        .append(hex(map.number(MAP_END)))
                        .append(' ')
                        .append(name)
                        .append(" (BuildId: ")
                        .append(map.string(MAP_BUILD_ID))
                        .append(")");
                if (map.number(MAP_LOAD_BIAS) != 0)
                    out.append(" load bias ").append(hex(map.number(MAP_LOAD_BIAS)));
                out.append('\n');
                libraries.remove(name);
            }
            for (Map.Entry<String, String> left : libraries.entrySet())
                out.append("    ")
                        .append(left.getKey())
                        .append(" (BuildId: ")
                        .append(left.getValue())
                        .append("), no mapping recorded\n");
        }
        return out.toString();
    }

    private static Map<Long, Message> threads(Message tombstone, int field) {
        Map<Long, Message> threads = new TreeMap<>();
        for (Message entry : tombstone.messages(field))
            threads.put(entry.number(1), entry.message(2));
        return threads;
    }

    private static void thread(
            StringBuilder out,
            String label,
            Map<Long, Message> threads,
            long tid,
            Map<String, String> libraries) {
        Message thread = threads.get(tid);
        if (thread == null) {
            out.append(label)
                    .append(": NONE: the tombstone has no thread ")
                    .append(tid)
                    .append(" (threads ")
                    .append(threads.keySet())
                    .append(")\n");
            return;
        }
        String name =
                thread.string(
                        THREAD_NAME); // a native bridge's guest thread may carry neither id nor
        // name
        out.append(label)
                .append(" of thread ")
                .append(tid)
                .append(name.isEmpty() ? "" : " (" + name + ")")
                .append(":\n");
        for (String note : thread.strings(BACKTRACE_NOTE))
            out.append("    NOTE: ").append(note).append('\n');
        for (String elf : thread.strings(UNREADABLE_ELF))
            out.append("    unreadable ELF: ").append(elf).append('\n');
        List<Message> frames = thread.messages(BACKTRACE);
        for (int i = 0; i < frames.size() && i < MAX_FRAMES; ++i) {
            Message frame = frames.get(i);
            String file = frame.string(FILE),
                    function = frame.string(FUNCTION),
                    buildId = frame.string(BUILD_ID);
            out.append(
                    String.format(
                            java.util.Locale.ROOT,
                            "    #%02d pc %016x  %s",
                            i,
                            frame.number(REL_PC),
                            file.isEmpty() ? "<unknown>" : file));
            if (!function.isEmpty())
                out.append(" (")
                        .append(function)
                        .append('+')
                        .append(frame.number(FUNCTION_OFFSET))
                        .append(')');
            if (!buildId.isEmpty()) {
                out.append(" (BuildId: ").append(buildId).append(')');
                libraries.put(file, buildId);
            }
            out.append('\n');
        }
        if (frames.size() > MAX_FRAMES)
            out.append("    ... ").append(frames.size() - MAX_FRAMES).append(" more frames\n");
    }

    private static String arch(long arch) {
        return arch >= 0 && arch < ARCHES.length ? ARCHES[(int) arch] : "arch " + arch;
    }

    private static String hex(long value) {
        return String.format(java.util.Locale.ROOT, "0x%x", value);
    }
}
