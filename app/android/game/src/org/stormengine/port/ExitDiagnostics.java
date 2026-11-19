package org.stormengine.port;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.io.InputStream;
import java.text.DateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Reports the previous abnormal process exit once (API 30+). Native crashes, ANRs,
 * Java crashes and low-memory kills are logged with their system description so a
 * tester can attach logcat; in debug builds the launcher shows a short notice (its text is the UI pack's). The diagnostics
 * export lists the whole history ({@link #describe}) with an ANR's trace head and a native crash's
 * backtrace (Android 12+, {@link Tombstone}).
 */
final class ExitDiagnostics {
    private ExitDiagnostics() {}

    /** The newest unseen abnormal exit as {reason, when} (reason: native_crash, crash, anr, low_memory, resources; when: its
     *  time in milliseconds, which the launcher writes in the language of its texts), or null. */
    static String[] takeNotice(Context context) {
        if (Build.VERSION.SDK_INT < 30) return null;
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 8);
            SharedPreferences seen =
                    context.getSharedPreferences("exit-diagnostics", Context.MODE_PRIVATE);
            long last = seen.getLong("timestamp", 0), newest = last;
            ApplicationExitInfo report = null;
            for (ApplicationExitInfo exit : exits) {
                if (exit.getTimestamp() <= last) continue;
                newest = Math.max(newest, exit.getTimestamp());
                String key = reasonKey(exit.getReason());
                if (key == null) continue;
                log(exit);
                if (report == null || exit.getTimestamp() > report.getTimestamp()) report = exit;
            }
            if (newest != last) seen.edit().putLong("timestamp", newest).apply();
            if (report == null) return null;
            return new String[] {
                reasonKey(report.getReason()), Long.toString(report.getTimestamp())
            };
        } catch (RuntimeException unavailable) {
            Log.w("StormExit", "Exit history is unavailable", unavailable);
            return null;
        }
    }

    /** A notice's time in the launcher's language (a game language name: "russian"), not the phone's: «18.10.2026, 13:36». */
    static String when(String millis, String language) {
        Locale locale = Locale.forLanguageTag(GameLanguage.code(language));
        return DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale)
                .format(new Date(Long.parseLong(millis)));
    }

    private static String reasonKey(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
                return "native_crash";
            case ApplicationExitInfo.REASON_CRASH:
                return "crash";
            case ApplicationExitInfo.REASON_ANR:
                return "anr";
            case ApplicationExitInfo.REASON_LOW_MEMORY:
                return "low_memory";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE:
                return "resources";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
                return "crash";
            default:
                return null; // User stop, process kill in background, update, etc.
        }
    }

    private static void log(ApplicationExitInfo exit) {
        Log.w(
                "StormExit",
                "Previous abnormal exit: process="
                        + exit.getProcessName()
                        + " pid="
                        + exit.getPid()
                        + " reason="
                        + exit.getReason()
                        + " status="
                        + exit.getStatus()
                        + " importance="
                        + exit.getImportance()
                        + " pss="
                        + exit.getPss()
                        + "kB rss="
                        + exit.getRss()
                        + "kB time="
                        + exit.getTimestamp()
                        + " description="
                        + exit.getDescription()
                        + (exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE
                                ? " (backtrace: «Поделиться отчётом», exit history)"
                                : ""));
        if (exit.getReason() == ApplicationExitInfo.REASON_ANR) {
            String trace = trace(exit);
            if (trace != null) Log.w("StormExit", "ANR trace head:\n" + trace);
        }
    }

    /** The largest tombstone read: a crash's report is tens to hundreds of kB (its memory maps and log tail). */
    private static final int MAX_TOMBSTONE = 8 << 20;

    /**
     * What the system kept of an exit: the head of an ANR's trace (text), the backtrace of a native crash (its
     * tombstone, protobuf since Android 12, read by {@link Tombstone}), else null.
     */
    private static String trace(ApplicationExitInfo exit) {
        boolean anr = exit.getReason() == ApplicationExitInfo.REASON_ANR,
                nativeCrash = exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE;
        if (!anr && !nativeCrash) return null;
        if (nativeCrash && Build.VERSION.SDK_INT < 31)
            return "NONE: Android "
                    + Build.VERSION.RELEASE
                    + " keeps no native crash report for apps (Android 12+)\n";
        try (InputStream trace = exit.getTraceInputStream()) {
            if (trace == null)
                return nativeCrash ? "NONE: the system kept no tombstone of this crash\n" : null;
            if (anr) {
                byte[] head = new byte[8192];
                int n = trace.read(head);
                return n > 0
                        ? new String(head, 0, n, java.nio.charset.StandardCharsets.UTF_8)
                        : null;
            }
            java.io.ByteArrayOutputStream proto = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            for (int n; (n = trace.read(buffer)) > 0; ) {
                if (proto.size() + n > MAX_TOMBSTONE)
                    return "UNREADABLE: the tombstone is larger than " + MAX_TOMBSTONE + " bytes\n";
                proto.write(buffer, 0, n);
            }
            return Tombstone.describe(proto.toByteArray());
        } catch (java.io.IOException | IllegalArgumentException unreadable) {
            Log.w("StormExit", "Cannot read the trace of exit " + exit.getPid(), unreadable);
            return "UNREADABLE: " + unreadable + "\n";
        }
    }

    /**
     * How the game process before this one ended, for the engine's GPU crash guards (engine fx/gpu_crash_guard.hpp
     * PreviousExit): {@code <root>/previous-exit.txt} = the reason's name ({@link DiagnosticsReport#reasonName}), its
     * time and the system's description, one line. A guard's marker left by a process the system killed for memory
     * or stopped is then no crash. Without a history (Android before 11, none yet) the file is removed: unknown.
     */
    static void writePreviousGameExit(Context context, java.io.File root) {
        java.io.File file = new java.io.File(root, "previous-exit.txt");
        String line = null;
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                String game = context.getPackageName() + ":game";
                int self = android.os.Process.myPid();
                for (ApplicationExitInfo exit :
                        context.getSystemService(ActivityManager.class)
                                .getHistoricalProcessExitReasons(null, 0, 16)) {
                    if (!game.equals(exit.getProcessName()) || exit.getPid() == self) continue;
                    String description =
                            exit.getDescription() == null
                                    ? ""
                                    : exit.getDescription().replace('\n', ' ');
                    String reason = DiagnosticsReport.reasonName(exit.getReason());
                    if (reason.indexOf(' ') >= 0)
                        reason = "unknown"; // a reason newer than our table: the engine keeps it a
                    // crash
                    line = reason + " " + exit.getTimestamp() + " " + description;
                    break; // newest first
                }
            } catch (RuntimeException unavailable) {
                Log.w(
                        "StormExit",
                        "Exit history is unavailable: the engine takes the previous exit as"
                                + " unknown",
                        unavailable);
            }
        }
        if (line == null) {
            if (file.exists() && !file.delete()) Log.e("StormExit", "Cannot remove " + file);
            return;
        }
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
            out.write((line + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (java.io.IOException error) {
            Log.e(
                    "StormExit",
                    "Cannot write " + file + ": the engine takes the previous exit as unknown",
                    error);
            file.delete();
        }
    }

    /** The system's exit history of our processes (every reason, newest first) for the diagnostics export. */
    static void describe(Context context, DiagnosticsReport report) {
        if (Build.VERSION.SDK_INT < 30) {
            report.exitProblem(
                    "Android " + Build.VERSION.RELEASE + " keeps no exit history (API 30+)");
            return;
        }
        try {
            List<ApplicationExitInfo> exits =
                    context.getSystemService(ActivityManager.class)
                            .getHistoricalProcessExitReasons(null, 0, 0);
            for (ApplicationExitInfo exit : exits)
                report.exit(
                        new DiagnosticsReport.Exit(
                                exit.getTimestamp(),
                                exit.getProcessName(),
                                exit.getPid(),
                                exit.getReason(),
                                exit.getStatus(),
                                exit.getImportance(),
                                exit.getPss(),
                                exit.getRss(),
                                exit.getDescription(),
                                trace(exit)));
        } catch (RuntimeException unavailable) {
            Log.w("StormExit", "Exit history is unavailable", unavailable);
            report.exitProblem(unavailable.toString());
        }
    }
}
