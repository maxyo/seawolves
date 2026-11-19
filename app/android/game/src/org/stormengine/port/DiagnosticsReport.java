package org.stormengine.port;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The text of the diagnostics file «Поделиться отчётом» shares (host-testable, no Android types):
 * fact sections, the renderer facts the engine already logged (bgfx caps, the effects host's
 * device support and what it turns off), the system's exit history of our processes, the tails of the engine's
 * log files of the latest game process and of the ones before it (each labelled with its start time, pid and how
 * it ended), preceded for a file longer than its tail by a digest of the rest (the session's periodic summaries,
 * stall reports and errors) and our log lines.
 * {@link DiagnosticsExport} collects the inputs on the device and opens the share sheet.
 */
final class DiagnosticsReport {
    /** Newest log lines kept (logcat holds a few MiB at most; a share target takes a few hundred KiB). */
    static final int MAX_LOG_LINES = 20000;

    /** Newest bytes of each engine log file. */
    static final int MAX_FILE_BYTES = 256 * 1024;

    /**
     * Bytes of a long engine log's digest: its lines before the tail that tell the whole session's history (a hang at
     * the end of a long session otherwise leaves only its last minutes). Errors repeated word for word are folded
     * into their first line; when the digest is still longer, the periodic summaries are thinned evenly per kind.
     */
    static final int MAX_DIGEST_BYTES = 192 * 1024;

    /**
     * The engine's periodic summaries and diagnostics the digest keeps (their messages start so, after the level and
     * the renderer's "[storm-bgfx] " tag): frame times and work every 30 s (shim frame_pacing.cpp, context.cpp,
     * core compiler.cpp), memory (context.cpp), thermal state and dynamic resolution (effects.cpp, thermal.cpp),
     * the upscaler (upscale.cpp), texture realization (draw path), the start line and the render backend.
     */
    static final String[] DIGEST_PERIODIC = {
        "frame pacing: ",
        "frame work: ",
        "script work: ",
        "memory: ",
        "effects: thermal status",
        "effects: dynamic resolution",
        "effects: background work",
        "effects upscale: ",
        "texture realize",
        "render thread: ",
        "surface lost"
    };

    /** The engine keeps the logs of the last game processes as <name>.prev<k>.log, k sessions back (engine log_rotation.hpp). */
    private static final Pattern GENERATION = Pattern.compile("^(.+)\\.prev([1-9][0-9]{0,2})?$");

    private static final Pattern GENERATION_BASE =
            Pattern.compile("^system(\\.prev([1-9][0-9]{0,2})?)?\\.log$");

    /** Left by the engine while a session runs in the foreground (log_rotation.hpp kRunningMarker). */
    static final String RUNNING_MARKER = "session-running.txt";

    /** The engine's record, at a start, of the session before it that ended without a clean exit (engine main.cpp). */
    private static final String UNCLEAN_LINE = "Previous session ended without a clean exit";

    /** The engine's first line of a session (engine main.cpp): "[time] [system] [info] Logging system initialized. Running on X, pid N". */
    private static final String START_LINE = "Logging system initialized";

    private static final Pattern START =
            Pattern.compile("^\\[([^\\]]+)\\].*" + START_LINE + ".*?(?:, pid (\\d+))?$");

    /** Digest lines always kept (before any periodic line when the budget is short), besides errors: the start, the stall watchdog. */
    static final String[] DIGEST_IMPORTANT = {
        START_LINE, "stall watchdog", "render backend: ", UNCLEAN_LINE
    };

    private static final int START_SEARCH_LINES = 64;
    private static final Set<String> KEPT_TAGS =
            new HashSet<>(Arrays.asList("AndroidRuntime", "libc", "DEBUG"));

    private final StringBuilder text = new StringBuilder();
    private final ArrayDeque<String> log = new ArrayDeque<>();
    private int logDropped;
    private final List<String> logProblems = new ArrayList<>();

    DiagnosticsReport(String title) {
        text.append(title).append('\n');
    }

    DiagnosticsReport section(String title) {
        text.append("\n== ").append(title).append(" ==\n");
        return this;
    }

    DiagnosticsReport fact(String key, Object value) {
        text.append(key).append(": ").append(value).append('\n');
        return this;
    }

    /**
     * The Game section's "script patch set" fact: the set that identifies the installed game (engine script_patches::identify,
     * run by the archive reader as for the launcher), or that none does and the game runs its scripts unpatched.
     */
    static String patchSet(
            boolean identified, String setId, String setVersion, List<String> testedVersions) {
        if (!setId.isEmpty()) return setId + " (" + setVersion + ")";
        if (!identified) return "not identified (its identify files were not read)";
        return "no set matched: scripts run unpatched; the sets are for "
                + (testedVersions.isEmpty() ? "no version" : String.join(", ", testedVersions));
    }

    /** A "logcat -d -v threadtime" line: "MM-DD HH:MM:SS.mmm  PID  TID L TAG: message" → {level, tag, message}, or null. */
    static String[] parse(String line) {
        String[] head = line.trim().split("\\s+", 6);
        if (head.length < 6 || head[4].length() != 1) return null;
        int colon = head[5].indexOf(':');
        if (colon < 0) return null;
        String message = head[5].substring(colon + 1);
        return new String[] {
            head[4],
            head[5].substring(0, colon).trim(),
            message.startsWith(" ") ? message.substring(1) : message
        };
    }

    /**
     * Our tags (Java "Storm*", native "storm-*"/"Storm*", SDL), Java and native crashes, and every
     * fatal or error line. logd hands an app only its own uid's lines, so all of them come from
     * our processes (launcher, :game, :downloads).
     */
    static boolean keep(String line) {
        String[] entry = parse(line);
        if (entry == null) return false;
        String level = entry[0], tag = entry[1];
        return level.equals("F")
                || level.equals("E")
                || tag.startsWith("Storm")
                || tag.startsWith("storm")
                || tag.startsWith("SDL")
                || KEPT_TAGS.contains(tag);
    }

    /**
     * The renderer facts the engine logs (shim context.cpp: the backend it chose, OpenGL ES or Vulkan, with the
     * reason, and the device's caps; fx capabilities.cpp, effects.cpp once per start; the frame pacing's rate choice
     * and its 30 s frame-time summaries, shim frame_pacing.cpp).
     */
    static boolean rendererFact(String line) {
        String[] entry = parse(line);
        if (entry == null || !entry[1].equals("storm-bgfx")) return false;
        String message = entry[2];
        return message.startsWith("bgfx renderer=")
                || message.contains("render backend: ")
                || message.contains("3D colour table:")
                || message.startsWith("effects: preset")
                || message.startsWith("frame pacing: ");
    }

    void readLog(BufferedReader reader) throws IOException {
        for (String line; (line = reader.readLine()) != null; ) {
            if (!keep(line)) continue;
            log.addLast(line);
            if (log.size() > MAX_LOG_LINES) {
                log.removeFirst();
                ++logDropped;
            }
        }
    }

    void logProblem(String problem) {
        logProblems.add(problem);
    }

    /** One entry of the system's exit history of our processes (ActivityManager.getHistoricalProcessExitReasons). */
    static final class Exit {
        final long time;
        final String process;
        final int pid, reason, status, importance;
        final long pssKb, rssKb;
        final String description, trace;

        /** `trace`: the head of an ANR's trace, a native crash's backtrace (ExitDiagnostics), or null. */
        Exit(
                long time,
                String process,
                int pid,
                int reason,
                int status,
                int importance,
                long pssKb,
                long rssKb,
                String description,
                String trace) {
            this.time = time;
            this.process = process;
            this.pid = pid;
            this.reason = reason;
            this.status = status;
            this.importance = importance;
            this.pssKb = pssKb;
            this.rssKb = rssKb;
            this.description = description;
            this.trace = trace;
        }
    }

    /** ApplicationExitInfo.REASON_* by value (API 30..35). */
    private static final String[] REASONS = {
        "unknown",
        "exit_self",
        "signaled",
        "low_memory",
        "crash",
        "crash_native",
        "anr",
        "initialization_failure",
        "permission_change",
        "excessive_resource_usage",
        "user_requested",
        "user_stopped",
        "dependency_died",
        "other",
        "freezer",
        "package_state_change",
        "package_updated"
    };

    /** The reasons that end a session abnormally (crash, ANR, a kill for memory or resources). */
    private static final Set<String> ABNORMAL =
            new HashSet<>(
                    Arrays.asList(
                            "crash",
                            "crash_native",
                            "anr",
                            "low_memory",
                            "excessive_resource_usage",
                            "initialization_failure"));

    static String reasonName(int reason) {
        return reason >= 0 && reason < REASONS.length ? REASONS[reason] : "reason " + reason;
    }

    private final List<Exit> exits = new ArrayList<>();
    private String exitProblem;

    void exit(Exit exit) {
        exits.add(exit);
    }

    /** The exit history could not be read (or this Android has none): said in its section. */
    void exitProblem(String problem) {
        exitProblem = problem;
    }

    /** Appends the renderer facts, the exit history, the engine's log files and the log lines; returns the whole text. */
    String finish(File engineLogs) {
        section("Renderer (logged by the engine)");
        int facts = 0;
        for (String line : log)
            if (rendererFact(line)) {
                text.append(line).append('\n');
                ++facts;
            }
        if (facts == 0)
            text.append(
                    "NOT IN THE LOG: the game has not started since the log buffer began; start it,"
                            + " then export again\n");
        exitHistory();
        File[] files = engineLogs.listFiles((dir, name) -> generation(name) >= 0);
        if (files == null) section("Engine logs").fact("MISSING", engineLogs.getPath());
        else {
            Arrays.sort(files);
            TreeMap<Integer, List<File>> sessions = new TreeMap<>();
            for (File file : files) {
                List<File> session = sessions.get(generation(file.getName()));
                if (session == null)
                    sessions.put(generation(file.getName()), session = new ArrayList<>());
                session.add(file);
            }
            if (!sessions.containsKey(0)) sessions.put(0, new ArrayList<File>());
            if (sessions.size() == 1)
                section("Engine logs, earlier sessions")
                        .text
                        .append("NONE: no engine logs of earlier sessions\n");
            // Oldest first: the session a hang or crash ended, then the starts after it. The engine
            // records a
            // session's unclean end in the next session's system log.
            for (int generation : sessions.descendingKeySet()) {
                Head newer =
                        sessions.containsKey(generation - 1)
                                ? head(system(sessions.get(generation - 1)))
                                : null;
                session(
                        generation,
                        sessions.get(generation),
                        newer,
                        new File(engineLogs, RUNNING_MARKER));
            }
        }
        section("Log (logcat, our processes)");
        for (String problem : logProblems)
            text.append("LOGCAT FAILED: ").append(problem).append('\n');
        if (logDropped > 0) text.append("(oldest ").append(logDropped).append(" lines dropped)\n");
        for (String line : log) text.append(line).append('\n');
        return text.toString();
    }

    private static String time(long millis) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(millis));
    }

    private void exitHistory() {
        section("Exit history (system, our processes, newest first)");
        if (exitProblem != null) {
            fact("UNAVAILABLE", exitProblem);
            return;
        }
        if (exits.isEmpty()) {
            text.append("NONE: no exits recorded\n");
            return;
        }
        for (Exit exit : exits) {
            String reason = reasonName(exit.reason);
            text.append(time(exit.time))
                    .append(' ')
                    .append(exit.process)
                    .append(" pid ")
                    .append(exit.pid)
                    .append(": ")
                    .append(reason)
                    .append(ABNORMAL.contains(reason) ? " (ABNORMAL)" : "")
                    .append(", status ")
                    .append(exit.status)
                    .append(", importance ")
                    .append(exit.importance)
                    .append(", pss ")
                    .append(exit.pssKb)
                    .append(" kB, rss ")
                    .append(exit.rssKb)
                    .append(" kB")
                    .append(exit.description != null ? ": " + exit.description : "")
                    .append('\n');
            if (exit.trace != null)
                text.append(
                                reason.equals("anr")
                                        ? "ANR trace head:\n"
                                        : "Native crash backtrace (tombstone; resolve with"
                                                + " tools/android/symbolize.py):\n")
                        .append(exit.trace)
                        .append(exit.trace.endsWith("\n") ? "" : "\n");
        }
    }

    /** One game process's engine logs, labelled with its start line (time, pid), how it ended and when they were last written. */
    private void session(int generation, List<File> files, Head newer, File marker) {
        String name =
                generation == 0
                        ? "latest session"
                        : generation == 1
                                ? "1 session before the latest"
                                : generation + " sessions before the latest";
        section("Engine logs, " + name);
        if (files.isEmpty()) {
            text.append("NONE: no engine logs of the ").append(name).append('\n');
            return;
        }
        File system = system(files);
        Head head = head(system);
        if (head.started == null)
            fact(
                    "started",
                    "NOT LOGGED: "
                            + (system != null
                                    ? system.getName() + " has no \"" + START_LINE + "\" line"
                                    : "this session has no system log"));
        else
            fact("started", head.started)
                    .fact(
                            "pid",
                            head.pid != null
                                    ? head.pid
                                    : "not logged (engine before the pid was logged)");
        long written = 0;
        for (File file : files) written = Math.max(written, file.lastModified());
        fact("last written", time(written));
        if (head.pid != null)
            for (Exit exit : exits)
                if (String.valueOf(exit.pid).equals(head.pid))
                    fact(
                            "exit",
                            reasonName(exit.reason)
                                    + " at "
                                    + time(exit.time)
                                    + (exit.description != null ? ": " + exit.description : ""));
        if (newer != null && newer.unclean != null)
            fact("ENDED WITHOUT A CLEAN EXIT", newer.unclean);
        if (generation == 0 && marker.isFile())
            fact(
                    "RUNNING MARKER",
                    read(marker).trim().replace('\n', ';')
                            + " (the game is running, or it ended and has not started since:"
                            + " without \"background\" it ended in the foreground or hangs;"
                            + " heartbeat/background are Unix seconds)");
        for (File file : files) {
            if (file.length() > MAX_FILE_BYTES) {
                section(
                        "Engine log "
                                + file.getName()
                                + " digest ("
                                + name
                                + "): the summaries, stall reports and errors before its tail");
                try {
                    digest(file, file.length() - MAX_FILE_BYTES, text);
                } catch (IOException error) {
                    text.append("UNREADABLE: ").append(error).append('\n');
                }
            }
            section("Engine log " + file.getName() + " (" + name + ")");
            try {
                tail(file, text);
            } catch (IOException error) {
                text.append("UNREADABLE: ").append(error).append('\n');
            }
        }
    }

    /** Sessions back of an engine log file name (engine log_rotation.hpp): 0 for <base>.log, k for <base>.prev<k>.log, 1 for <base>.prev.log; -1 for other files. */
    static int generation(String name) {
        if (!name.endsWith(".log") || name.length() <= 4) return -1;
        Matcher match = GENERATION.matcher(name.substring(0, name.length() - 4));
        if (!match.matches()) return 0;
        return match.group(2) == null ? 1 : Integer.parseInt(match.group(2));
    }

    private static File system(List<File> session) {
        for (File file : session)
            if (GENERATION_BASE.matcher(file.getName()).matches()) return file;
        return null;
    }

    /** What the head of a session's system log says: its start line, and the engine's record of the previous session's unclean end. */
    static final class Head {
        String started, pid, unclean;
    }

    static Head head(File system) {
        Head head = new Head();
        if (system == null) return head;
        try (BufferedReader in =
                new BufferedReader(
                        new InputStreamReader(
                                new FileInputStream(system), StandardCharsets.UTF_8))) {
            for (int i = 0; i < START_SEARCH_LINES; ++i) {
                String line = in.readLine();
                if (line == null) break;
                Matcher match = START.matcher(line);
                if (head.started == null && match.find()) {
                    head.started = match.group(1);
                    head.pid = match.group(2);
                }
                int unclean = line.indexOf(UNCLEAN_LINE);
                if (unclean >= 0)
                    head.unclean =
                            line.substring(unclean + UNCLEAN_LINE.length())
                                    .replaceFirst("^[^:]*: ", "");
            }
        } catch (IOException missing) {
            // said as NOT LOGGED by the caller
        }
        return head;
    }

    private static String read(File file) {
        try {
            return new String(
                    java.nio.file.Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException error) {
            return "UNREADABLE: " + error;
        }
    }

    /** The message of an engine log line: after "[time] [logger] [level] " and the renderer's "[storm-bgfx] " tag. */
    private static String message(String line) {
        int at = 0;
        for (int i = 0; i < 3 && at < line.length() && line.charAt(at) == '['; ++i) {
            int close = line.indexOf("] ", at);
            if (close < 0) break;
            at = close + 2;
        }
        String message = line.substring(at);
        return message.startsWith("[storm-bgfx] ") ? message.substring(13) : message;
    }

    private static boolean startsWithAny(String message, String[] prefixes) {
        for (String prefix : prefixes) if (message.startsWith(prefix)) return true;
        return false;
    }

    /** -1: not in the digest; 0: important (errors, stalls, the start); 1 + k: the periodic summary DIGEST_PERIODIC[k]. */
    static int digestKind(String line) {
        String message = message(line);
        if (line.contains("] [error] ")
                || line.contains("] [critical] ")
                || message.startsWith("ERROR")
                || startsWithAny(message, DIGEST_IMPORTANT)) return 0;
        for (int k = 0; k < DIGEST_PERIODIC.length; ++k)
            if (message.startsWith(DIGEST_PERIODIC[k])) return 1 + k;
        return -1;
    }

    /** The first `limit` bytes of a stream. */
    private static final class Bounded extends FilterInputStream {
        private long left;

        Bounded(InputStream in, long limit) {
            super(in);
            left = limit;
        }

        @Override
        public int read() throws IOException {
            if (left <= 0) return -1;
            int b = super.read();
            if (b >= 0) --left;
            return b;
        }

        @Override
        public int read(byte[] buffer, int offset, int length) throws IOException {
            if (left <= 0) return -1;
            int n = super.read(buffer, offset, (int) Math.min(length, left));
            if (n > 0) left -= n;
            return n;
        }
    }

    /**
     * The digest of a file's first `bytes` (the part its tail leaves out), at most {@link #MAX_DIGEST_BYTES}: its
     * important lines and periodic summaries in their order; an error repeated word for word is kept once with its
     * count; over the budget the important lines keep their first and last halves and each kind of summary is thinned
     * to every n-th line (its last one always kept), and the header says so.
     */
    static void digest(File file, long bytes, StringBuilder out) throws IOException {
        List<String> lines = new ArrayList<>();
        List<Integer> kinds = new ArrayList<>();
        Map<String, Integer> firstError = new HashMap<>(); // message -> index in lines
        Map<Integer, int[]> repeats = new LinkedHashMap<>(); // index -> {count}
        Map<Integer, String> lastRepeat = new HashMap<>();
        int matched = 0;
        try (BufferedReader in =
                new BufferedReader(
                        new InputStreamReader(
                                new Bounded(new FileInputStream(file), bytes),
                                StandardCharsets.UTF_8))) {
            for (String line; (line = in.readLine()) != null; ) {
                int kind = digestKind(line);
                if (kind < 0) continue;
                ++matched;
                if (kind == 0) {
                    String key = message(line);
                    Integer first = firstError.get(key);
                    if (first != null) {
                        int[] count = repeats.get(first);
                        if (count == null) repeats.put(first, count = new int[1]);
                        ++count[0];
                        int close = line.indexOf(']');
                        lastRepeat.put(first, close > 0 ? line.substring(1, close) : "?");
                        continue;
                    }
                    firstError.put(key, lines.size());
                }
                lines.add(line);
                kinds.add(kind);
            }
        }
        for (Map.Entry<Integer, int[]> entry : repeats.entrySet())
            lines.set(
                    entry.getKey(),
                    lines.get(entry.getKey())
                            + "  [repeated "
                            + entry.getValue()[0]
                            + " more times, the last at "
                            + lastRepeat.get(entry.getKey())
                            + "]");
        // The budget: important lines first (their first and last halves when they alone are over
        // half of it), then
        // the summaries, every n-th of each kind.
        long important = 0, periodic = 0;
        for (int i = 0; i < lines.size(); ++i) {
            long size = lines.get(i).length() + 1;
            if (kinds.get(i) == 0) important += size;
            else periodic += size;
        }
        boolean[] keep = new boolean[lines.size()];
        long importantKept = 0;
        int importantDropped = 0;
        if (important <= MAX_DIGEST_BYTES / 2) {
            for (int i = 0; i < lines.size(); ++i) if (kinds.get(i) == 0) keep[i] = true;
            importantKept = important;
        } else {
            long head = 0, tail = 0;
            for (int i = 0; i < lines.size(); ++i)
                if (kinds.get(i) == 0 && head + lines.get(i).length() + 1 <= MAX_DIGEST_BYTES / 4) {
                    keep[i] = true;
                    head += lines.get(i).length() + 1;
                }
            for (int i = lines.size() - 1; i >= 0; --i)
                if (kinds.get(i) == 0
                        && !keep[i]
                        && tail + lines.get(i).length() + 1 <= MAX_DIGEST_BYTES / 4) {
                    keep[i] = true;
                    tail += lines.get(i).length() + 1;
                } else if (kinds.get(i) == 0 && !keep[i]) break;
            for (int i = 0; i < lines.size(); ++i)
                if (kinds.get(i) == 0 && !keep[i]) ++importantDropped;
            importantKept = head + tail;
        }
        long room = MAX_DIGEST_BYTES - importantKept;
        int stride = periodic <= room ? 1 : (int) ((periodic + room - 1) / Math.max(room, 1));
        int[] seen = new int[DIGEST_PERIODIC.length + 1];
        int[] last = new int[DIGEST_PERIODIC.length + 1];
        Arrays.fill(last, -1);
        for (int i = 0; i < lines.size(); ++i) if (kinds.get(i) > 0) last[kinds.get(i)] = i;
        for (int i = 0; i < lines.size(); ++i) {
            int kind = kinds.get(i);
            if (kind > 0 && (seen[kind]++ % stride == 0 || i == last[kind])) keep[i] = true;
        }
        int kept = 0;
        for (boolean k : keep) if (k) ++kept;
        out.append("(the first ")
                .append(bytes / 1024)
                .append(" KiB, before the tail: ")
                .append(matched)
                .append(" summary, stall and error lines");
        if (!repeats.isEmpty())
            out.append(", errors repeated word for word folded into their first line");
        if (importantDropped > 0)
            out.append(", ")
                    .append(importantDropped)
                    .append(" error/stall lines in the middle left out");
        if (stride > 1)
            out.append(", every ").append(stride).append("th line of each kind of summary kept");
        out.append("; ").append(kept).append(" lines shown)\n");
        boolean gap = false;
        for (int i = 0; i < lines.size(); ++i) {
            if (keep[i]) {
                out.append(lines.get(i)).append('\n');
                gap = false;
            } else if (kinds.get(i) == 0 && !gap) {
                out.append("(... error/stall lines left out ...)\n");
                gap = true;
            }
        }
    }

    /** The newest {@link #MAX_FILE_BYTES} of a file, from its first whole line. */
    static void tail(File file, StringBuilder out) throws IOException {
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long size = in.length();
            int count = (int) Math.min(size, MAX_FILE_BYTES);
            byte[] data = new byte[count];
            in.seek(size - count);
            in.readFully(data);
            int start = 0;
            if (count < size) {
                out.append("(last ")
                        .append(count / 1024)
                        .append(" KiB of ")
                        .append(size / 1024)
                        .append(")\n");
                while (start < count && data[start++] != '\n') {}
            }
            out.append(new String(data, start, count - start, StandardCharsets.UTF_8));
            if (count > start && data[count - 1] != '\n') out.append('\n');
        }
    }
}
