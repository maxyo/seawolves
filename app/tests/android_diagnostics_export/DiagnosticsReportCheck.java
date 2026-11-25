package org.stormengine.port;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public final class DiagnosticsReportCheck {
    private static void check(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    private static String line(String level, String tag, String message) {
        return "11-11 07:20:13.308  4242  4250 " + level + " " + tag + ": " + message;
    }

    public static void main(String[] args) throws IOException {
        filterChecks();
        rendererFactChecks();
        patchSetChecks();
        logChecks();
        System.out.println(
                "PASS: diagnostics report filter, renderer facts, log tails, whole-session digests and sections");
    }

    private static void filterChecks() throws IOException {
        // Filter: our tags at any level, every F/E line, crashes; nothing else.
        check(
                DiagnosticsReport.keep(line("I", "storm-bgfx", "bgfx renderer=OpenGLES")),
                "native renderer tag");
        check(DiagnosticsReport.keep(line("D", "StormUiState", "x")), "StormUi* tag");
        check(DiagnosticsReport.keep(line("I", "StormGame   ", "padded tag")), "padded tag");
        check(
                DiagnosticsReport.keep(line("I", "StormEngine", "[system] spdlog")),
                "engine spdlog sink");
        check(DiagnosticsReport.keep(line("I", "SDL", "SDL_main")), "SDL");
        check(
                DiagnosticsReport.keep(line("E", "AndroidRuntime", "FATAL EXCEPTION: main")),
                "Java crash");
        check(
                DiagnosticsReport.keep(line("F", "libc", "Fatal signal 11 (SIGSEGV)")),
                "native crash");
        check(
                DiagnosticsReport.keep(line("E", "OpenGLRenderer", "foreign error")),
                "error of another tag");
        check(
                !DiagnosticsReport.keep(line("I", "OpenGLRenderer", "chatty")),
                "foreign info dropped");
        check(
                !DiagnosticsReport.keep(line("W", "ActivityThread", "x: y")),
                "foreign warning dropped");
        check(!DiagnosticsReport.keep("--------- beginning of main"), "buffer marker dropped");
        check(!DiagnosticsReport.keep(""), "empty line dropped");
        String[] parsed =
                DiagnosticsReport.parse(
                        line("I", "storm-bgfx", "fx: OpenGLES: 3D colour table: yes"));
        check(
                parsed[0].equals("I")
                        && parsed[1].equals("storm-bgfx")
                        && parsed[2].equals("fx: OpenGLES: 3D colour table: yes"),
                "message keeps its own colons");
    }

    private static void rendererFactChecks() throws IOException {
        // Renderer facts are the engine's own lines, not recomputed.
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "I",
                                "storm-bgfx",
                                "bgfx renderer=OpenGLES 1280x720 originBottomLeft=1")),
                "caps line");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "I",
                                "storm-bgfx",
                                "fx: OpenGLES: 3D colour table: yes, shadow map: NO")),
                "support line");
        check(
                DiagnosticsReport.rendererFact(
                        line("I", "storm-bgfx", "effects: preset 2 on this device: MSAA off")),
                "disabled effects");
        check(
                !DiagnosticsReport.rendererFact(line("I", "storm-bgfx", "fx: water ready")),
                "other fx lines stay in the log only");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "I",
                                "storm-bgfx",
                                "render backend: Vulkan (render_backend = vulkan): render_backend asks for Vulkan; Vulkan device: Adreno (TM) 740, Vulkan 1.3.128")),
                "backend choice");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "I",
                                "storm-bgfx",
                                "ERROR: render backend: OpenGL ES (render_backend = vulkan): Vulkan failed (bgfx::init failed on Vulkan)")),
                "backend refused");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "E",
                                "storm-bgfx",
                                "ERROR: render backend: the previous Vulkan start never presented its first frames: the process died in it on Mali-G52, Vulkan 1.1.0")),
                "Vulkan crash guard");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "E",
                                "storm-bgfx",
                                "ERROR: render backend: OpenGL ES (render_backend = auto): an earlier Vulkan start on this device and driver crashed the process (kept until the driver changes or the cache is cleared)")),
                "auto kept off Vulkan after a crash");
        check(
                DiagnosticsReport.rendererFact(
                        line(
                                "I",
                                "storm-bgfx",
                                "frame pacing: Swappy GL 2.1.2 (games-frame-pacing 2.1.3); swap interval 16.67 ms")),
                "frame pacing choice");
        check(
                DiagnosticsReport.rendererFact(
                        line("E", "storm-bgfx", "frame pacing: Swappy GL unavailable")),
                "frame pacing failure");
        check(
                !DiagnosticsReport.rendererFact(line("I", "StormUi", "bgfx renderer=fake")),
                "only the renderer's tag");
    }

    private static void patchSetChecks() throws IOException {
        // The Game section names the script patch set the engine applies to the installed game, or
        // that none does.
        check(
                DiagnosticsReport.patchSet(
                                true,
                                "teho-1.7.3-x64",
                                "ver. 1.7.3 (26.11.21) x64",
                                java.util.Arrays.asList("ver. 1.7.3 (26.11.21) x64"))
                        .equals("teho-1.7.3-x64 (ver. 1.7.3 (26.11.21) x64)"),
                "the identified set");
        check(
                DiagnosticsReport.patchSet(
                                true, "", "", java.util.Arrays.asList("ver. 1.7.3 (26.11.21) x64"))
                        .equals(
                                "no set matched: scripts run unpatched; the sets are for ver. 1.7.3 (26.11.21) x64"),
                "no set matched");
        check(
                DiagnosticsReport.patchSet(false, "", "", java.util.Collections.<String>emptyList())
                        .startsWith("not identified"),
                "not read");
    }

    private static void logChecks() throws IOException {
        File logs = Files.createTempDirectory("storm-logs").toFile();
        try {
            write(
                    new File(logs, "system.log"),
                    "[2026-11-13 18:49:54.185] [system] [info] Logging system initialized. Running on X, pid 19252\n");
            write(new File(logs, "ff_keys.json"), "{}");
            StringBuilder big = new StringBuilder();
            for (int i = 0; big.length() <= DiagnosticsReport.MAX_FILE_BYTES + 1000; ++i)
                big.append("line ").append(i).append('\n');
            big.append("NEWEST");
            write(new File(logs, "compile.log"), big.toString());

            StringBuilder input = new StringBuilder("--------- beginning of main\n");
            input.append(line("I", "storm-bgfx", "bgfx renderer=OpenGLES 1280x720")).append('\n');
            input.append(line("I", "ActivityThread", "dropped")).append('\n');
            for (int i = 0; i < DiagnosticsReport.MAX_LOG_LINES + 5; ++i)
                input.append(line("I", "StormGame", "tick " + i)).append('\n');
            DiagnosticsReport report =
                    new DiagnosticsReport("Title").section("Device").fact("model", "Pixel");
            report.readLog(new BufferedReader(new StringReader(input.toString())));
            String text = report.finish(logs);
            check(
                    text.startsWith("Title\n\n== Device ==\nmodel: Pixel\n"),
                    "title and facts first");
            int renderer = text.indexOf("== Renderer"),
                    compile = text.indexOf("== Engine log compile.log (latest session) =="),
                    system = text.indexOf("== Engine log system.log (latest session) =="),
                    log = text.indexOf("== Log (logcat");
            check(
                    renderer > 0 && renderer < compile && compile < system && system < log,
                    "section order");
            check(!text.contains("ff_keys.json"), "only .log files");
            check(text.contains("(last 256 KiB of "), "a long file says it is cut");
            check(
                    text.contains("NEWEST\n\n== Engine log system.log (latest session)"),
                    "the newest bytes of a long file are kept");
            check(!text.contains("\nline 0\n"), "the oldest bytes are cut");
            check(text.contains("(oldest 6 lines dropped)"), "the log keeps the newest lines");
            check(
                    text.contains("tick " + (DiagnosticsReport.MAX_LOG_LINES + 4))
                            && !text.contains("tick 4\n"),
                    "newest log lines");
            check(!text.contains("ActivityThread"), "foreign lines filtered");
            // The caps line rotated out of the kept log: said, not silently missing.
            check(text.contains("NOT IN THE LOG"), "renderer facts missing are reported");
            check(
                    text.contains("== Engine logs, earlier sessions ==\nNONE: "),
                    "no earlier session is said");
            check(
                    text.contains(
                            "== Engine logs, latest session ==\nstarted: 2026-11-13 18:49:54.185\npid: 19252\nlast written: "),
                    "the latest session is labelled with its start line");
            check(
                    text.contains(
                            "== Exit history (system, our processes, newest first) ==\nNONE: "),
                    "an empty exit history is said");

            // A hang the player swiped away, then a crash, then the latest start: three sessions
            // kept (engine log_rotation.hpp: <name>.prev<k>.log is k sessions back), each tail cut
            // on its own.
            check(
                    DiagnosticsReport.generation("system.log") == 0
                            && DiagnosticsReport.generation("system.prev1.log") == 1
                            && DiagnosticsReport.generation("compile.prev3.log") == 3
                            && DiagnosticsReport.generation("odd.prev0.log") == 0
                            && DiagnosticsReport.generation("ff_keys.json") == -1,
                    "generations");
            write(
                    new File(logs, "system.prev2.log"),
                    "[2026-11-13 18:30:01.002] [system] [info] Logging system initialized. Running on X, pid 4242\nHUNG HERE\n");
            StringBuilder bigPrevious = new StringBuilder();
            for (int i = 0; bigPrevious.length() <= 3 * DiagnosticsReport.MAX_FILE_BYTES; ++i)
                bigPrevious.append("old ").append(i).append('\n');
            bigPrevious.append("PREVIOUS NEWEST");
            write(new File(logs, "compile.prev2.log"), bigPrevious.toString());
            write(
                    new File(logs, "system.prev1.log"),
                    "[2026-11-13 18:45:00.000] [system] [info] Logging system initialized. Running on X, pid 5151\n[2026-11-13 18:45:00.001] [system] [error] Previous session ended without a clean exit: started 2026-11-13 18:30:01, pid 4242; its last log line: HUNG HERE\nCRASHED HERE\n");
            write(new File(logs, "error.prev3.log"), "oldest error\n");
            write(
                    new File(logs, DiagnosticsReport.RUNNING_MARKER),
                    "started 2026-11-13 18:49:54, pid 19252\nheartbeat 1763041794\n");
            DiagnosticsReport withExits = new DiagnosticsReport("T");
            withExits.exit(
                    new DiagnosticsReport.Exit(
                            1763042700000L,
                            "org.game:game",
                            5151,
                            5,
                            11,
                            100,
                            900000,
                            1000000,
                            "crash",
                            null));
            withExits.exit(
                    new DiagnosticsReport.Exit(
                            1763041800000L,
                            "org.game:game",
                            4242,
                            6,
                            0,
                            100,
                            800000,
                            900000,
                            "Input dispatching timed out",
                            "----- pid 4242 -----\n\"main\" waiting\n"));
            withExits.exit(
                    new DiagnosticsReport.Exit(
                            1763041000000L, "org.game", 1000, 10, 0, 400, 1, 2, null, null));
            String all = withExits.finish(logs);
            int history = all.indexOf("== Exit history"),
                    oldest =
                            all.indexOf(
                                    "== Engine logs, 3 sessions before the latest ==\nstarted: NOT LOGGED: this session has no system log"),
                    hung =
                            all.indexOf(
                                    "== Engine logs, 2 sessions before the latest ==\nstarted: 2026-11-13 18:30:01.002\npid: 4242\nlast written: "),
                    hungCompile =
                            all.indexOf(
                                    "== Engine log compile.prev2.log (2 sessions before the latest) =="),
                    hungSystem =
                            all.indexOf(
                                    "== Engine log system.prev2.log (2 sessions before the latest) =="),
                    crashed =
                            all.indexOf(
                                    "== Engine logs, 1 session before the latest ==\nstarted: 2026-11-13 18:45:00.000\npid: 5151"),
                    latestSession =
                            all.indexOf(
                                    "== Engine logs, latest session ==\nstarted: 2026-11-13 18:49:54.185\npid: 19252"),
                    latestCompile = all.indexOf("== Engine log compile.log (latest session) =="),
                    latestSystem = all.indexOf("== Engine log system.log (latest session) ==");
            check(
                    history > 0
                            && history < oldest
                            && oldest < hung
                            && hung < hungCompile
                            && hungCompile < hungSystem
                            && hungSystem < crashed
                            && crashed < latestSession
                            && latestSession < latestCompile
                            && latestCompile < latestSystem,
                    "every session, labelled, oldest first, after the exit history");
            check(
                    all.contains("pid: 4242\nlast written: ")
                            && all.substring(hung, hungCompile).contains("exit: anr at ")
                            && all.substring(hung, hungCompile)
                                    .contains(": Input dispatching timed out\n"),
                    "a session's exit from the history, by pid");
            check(
                    all.substring(hung, hungCompile)
                            .contains(
                                    "ENDED WITHOUT A CLEAN EXIT: started 2026-11-13 18:30:01, pid 4242; its last log line: HUNG HERE\n"),
                    "the next start's record of an unclean end, at the session it is about");
            check(
                    all.substring(crashed, latestSession).contains("exit: crash_native at "),
                    "the crash's exit");
            check(
                    all.substring(latestSession, latestCompile)
                            .contains(
                                    "RUNNING MARKER: started 2026-11-13 18:49:54, pid 19252;heartbeat 1763041794 (the game is running"),
                    "the latest session's running marker");
            check(
                    all.contains(
                            "org.game:game pid 4242: anr (ABNORMAL), status 0, importance 100, pss 800000 kB, rss 900000 kB: Input dispatching timed out\nANR trace head:\n----- pid 4242 -----\n\"main\" waiting\n"),
                    "an ANR in the history, with its trace head");
            check(
                    all.contains(
                            "org.game:game pid 5151: crash_native (ABNORMAL), status 11, importance 100, pss 900000 kB, rss 1000000 kB: crash\n"
                                    + DiagnosticsReport.NATIVE_TRACE
                                    + "\n"),
                    "a native crash in the history says its system trace is not included");
            check(
                    all.contains("org.game pid 1000: user_requested, status 0")
                            && !all.contains("user_requested (ABNORMAL)"),
                    "a swipe is in the history, not abnormal");
            check(
                    all.contains("HUNG HERE\n")
                            && all.contains("PREVIOUS NEWEST\n")
                            && all.contains("CRASHED HERE\n")
                            && all.contains("NEWEST\n\n== Engine log system.log"),
                    "each session's newest lines are kept");
            check(
                    all.split("\\(last 256 KiB of ", -1).length == 3,
                    "the size limit applies to each file");
            check(
                    all.length() < 3 * DiagnosticsReport.MAX_FILE_BYTES,
                    "a big older log does not crowd out the latest one");
            new File(logs, DiagnosticsReport.RUNNING_MARKER).delete();
            new File(logs, "error.prev3.log").delete();
            new File(logs, "system.prev1.log").delete();

            // An engine before the pid was logged, and a system log without the start line.
            write(
                    new File(logs, "system.prev2.log"),
                    "[2026-11-13 18:30:01.002] [system] [info] Logging system initialized. Running on X\n");
            write(new File(logs, "system.log"), "no start line\n");
            DiagnosticsReport unavailable = new DiagnosticsReport("T");
            unavailable.exitProblem("Android 10 keeps no exit history (API 30+)");
            String old = unavailable.finish(logs);
            check(
                    old.contains(
                            "2 sessions before the latest ==\nstarted: 2026-11-13 18:30:01.002\npid: not logged"),
                    "start without a pid");
            check(
                    old.contains("latest session ==\nstarted: NOT LOGGED: system.log has no"),
                    "a missing start line is said");
            check(!old.contains("RUNNING MARKER"), "no marker, no marker line");
            check(
                    old.contains(
                            "== Exit history (system, our processes, newest first) ==\nUNAVAILABLE: Android 10"),
                    "no history is said");

            // A long session: the digest before the tail keeps the whole session's summaries, stall
            // reports and errors.
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] frame pacing: 30.0 s, 900 frames")
                            == 1,
                    "frame pacing summary");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] memory: rss_mb=900")
                            > 0,
                    "memory line");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] script work: 30.0 s")
                            > 0,
                    "script work");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] gpu passes: 30.0 s; gpu_frame_ms mean 7.42")
                            > 0,
                    "the GPU time per pass");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [warning] stall watchdog (report 1): the game thread")
                            == 0,
                    "stall report");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] ERROR: effects: x")
                            == 0,
                    "renderer error");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [error] script: missed attribute")
                            == 0,
                    "error");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] effects: thermal load hold (warm for 180 s without cooling below headroom 0.60): x")
                            == 0,
                    "a thermal load change is kept");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] effects: thermal load sensing: AThermal status (API 37)")
                            == 0,
                    "the thermal sensing line is kept");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] diagnostics: detailed on")
                            == 0,
                    "whether the session ran with detailed diagnostics is kept");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] memory at start (phase=s:anon/file MB:anon/file peak MB so far): loaded=0.4:52/31:52/31")
                            == 0,
                    "the memory at each phase of the start is kept");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] bench: msaa 1 | fx_msaa=1 | gpu_ms mean 9.10")
                            == 0,
                    "the renderer bench's lines are kept");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] effects: thermal status: none, headroom 0.71 now / 0.78 in 10 s")
                            > 0,
                    "the 30 s thermal reading is a periodic summary");
            check(
                    DiagnosticsReport.digestKind(
                                    "[2026-11-18 15:00:00.000] [system] [info] [storm-bgfx] effects parallax: x")
                            == -1,
                    "noise");
            File session = Files.createTempDirectory("storm-session").toFile();
            try {
                StringBuilder longLog =
                        new StringBuilder(
                                "[2026-11-18 14:00:00.000] [system] [info] Logging system initialized. Running on X, pid 28662\n");
                longLog.append(
                        "[2026-11-18 14:00:01.000] [system] [info] [storm-bgfx] frame pacing: 30.0 s, FIRST SUMMARY\n");
                longLog.append(
                        "[2026-11-18 14:00:01.500] [system] [info] [storm-bgfx] memory: rss_mb=812 FIRST MEMORY\n");
                longLog.append(
                        "[2026-11-18 14:00:02.000] [system] [error] stall watchdog (report 1): EARLY STALL\n");
                longLog.append(
                        "[2026-11-18 14:00:02.001] [system] [error] stall watchdog: game #00 pc 00000000001234ab  /data/app/lib/arm64/libengine.so\n");
                for (int i = 0; i < 1000; ++i)
                    longLog.append("[2026-11-18 14:01:")
                            .append(String.format("%02d", i % 60))
                            .append(".000] [system] [error] the same error\n");
                for (int i = 0; longLog.length() < 4 * DiagnosticsReport.MAX_FILE_BYTES; ++i)
                    longLog.append(
                                    "[2026-11-18 14:02:00.000] [system] [info] [storm-bgfx] effects parallax: noise ")
                            .append(i)
                            .append('\n');
                longLog.append(
                        "[2026-11-18 14:59:59.000] [system] [info] [storm-bgfx] frame pacing: 30.0 s, LAST BEFORE TAIL\n");
                for (int i = 0; i < 6000; ++i)
                    longLog.append("[2026-11-18 15:38:00.000] [system] [info] tail noise ")
                            .append(i)
                            .append('\n');
                longLog.append("TAIL NEWEST\n");
                write(new File(session, "system.log"), longLog.toString());
                String digested = new DiagnosticsReport("T").finish(session);
                int
                        digestAt =
                                digested.indexOf(
                                        "== Engine log system.log digest (latest session): the summaries, stall reports and errors before its tail =="),
                        tailAt = digested.indexOf("== Engine log system.log (latest session) ==");
                check(digestAt > 0 && digestAt < tailAt, "the digest comes before the tail");
                String part = digested.substring(digestAt, tailAt);
                check(
                        part.contains("FIRST SUMMARY\n")
                                && part.contains("FIRST MEMORY\n")
                                && part.contains("LAST BEFORE TAIL\n"),
                        "the session's summaries");
                check(
                        part.contains("EARLY STALL\n")
                                && part.contains("stall watchdog: game #00 pc 00000000001234ab"),
                        "the stall report and its backtrace");
                check(
                        part.contains(
                                        "the same error  [repeated 999 more times, the last at 2026-11-18 14:01:39.000]")
                                && part.split("the same error", -1).length == 2,
                        "a repeated error folded into its first line");
                check(
                        !part.contains("effects parallax") && !part.contains("tail noise"),
                        "noise and the tail are not in the digest");
                check(digested.contains("TAIL NEWEST\n"), "the tail follows");

                // Over the budget: each kind of summary thinned evenly, its last line kept, within
                // the budget.
                StringBuilder huge =
                        new StringBuilder(
                                "[2026-11-18 14:00:00.000] [system] [info] Logging system initialized. Running on X, pid 1\n");
                int summaries = 0;
                for (; huge.length() < 3 * DiagnosticsReport.MAX_DIGEST_BYTES; ++summaries)
                    huge.append(
                                    "[2026-11-18 14:00:00.000] [system] [info] [storm-bgfx] effects upscale: summary ")
                            .append(summaries)
                            .append(" of a long session\n");
                huge.append(
                        "[2026-11-18 14:00:00.000] [system] [info] [storm-bgfx] effects upscale: summary LAST\n");
                for (int i = 0; i < 6000; ++i)
                    huge.append("[2026-11-18 15:38:00.000] [system] [info] tail noise ")
                            .append(i)
                            .append('\n');
                write(new File(session, "system.log"), huge.toString());
                String thinned = new DiagnosticsReport("T").finish(session);
                part =
                        thinned.substring(
                                thinned.indexOf("digest (latest session)"),
                                thinned.indexOf("== Engine log system.log (latest session) =="));
                check(
                        part.contains("th line of each kind of summary kept")
                                && part.contains("summary 0 of")
                                && part.contains("summary LAST\n")
                                && !part.contains("summary 1 of"),
                        "summaries thinned evenly, first and last kept");
                check(
                        part.length() <= DiagnosticsReport.MAX_DIGEST_BYTES + 1024,
                        "the digest stays within its budget");
            } finally {
                for (File file : session.listFiles()) file.delete();
                session.delete();
            }

            DiagnosticsReport fresh = new DiagnosticsReport("T");
            fresh.readLog(
                    new BufferedReader(
                            new StringReader(
                                    line("I", "storm-bgfx", "bgfx renderer=Vulkan") + "\n")));
            fresh.logProblem("logcat exited with 1");
            String small = fresh.finish(new File(logs, "absent"));
            check(
                    small.contains("== Renderer (logged by the engine) ==\n11-11"),
                    "renderer facts copied");
            check(small.contains("MISSING: "), "a missing log directory is reported");
            check(
                    small.contains("LOGCAT FAILED: logcat exited with 1"),
                    "a logcat failure is reported");
        } finally {
            for (File file : logs.listFiles()) file.delete();
            logs.delete();
        }
    }

    private static void write(File file, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
