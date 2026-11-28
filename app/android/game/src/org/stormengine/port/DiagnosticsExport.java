package org.stormengine.port;

import android.content.Context;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Build;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * «Поделиться отчётом» (about screen): for a device without adb, writes our log lines, the engine's
 * log files, device, GPU and build facts to one text file in the cache and offers it to the share
 * sheet ({@link ShareSheet}). Runs off the UI threads (logcat takes a moment; the GL probe needs a thread of its own).
 */
final class DiagnosticsExport {
    private DiagnosticsExport() {}

    static void share(Context context, JSONObject buildInfo, StormUiView ui) {
        ShareSheet.offer(
                context, ui, "Diagnostics export", "text/plain", () -> write(context, buildInfo));
    }

    /** Writes the report into the cache directory (also the scenario driver's diagnostics, android/game/testloop). */
    static File write(Context context, JSONObject info) throws IOException {
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        DiagnosticsReport report = new DiagnosticsReport("Seawolves diagnostics " + stamp);
        report.section("Build")
                .fact(
                        "version",
                        info.optString("versionName") + " (" + info.optInt("versionCode") + ")")
                .fact("applicationId", info.optString("applicationId"))
                .fact("commit", info.optString("commit"))
                .fact(
                        "detailed diagnostics",
                        GraphicsSettings.detailedDiagnostics(context) ? "on" : "off");
        game(context, report.section("Game"));
        report.section("Device")
                .fact("model", Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + ")")
                .fact("android", Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")")
                .fact("abi", Arrays.toString(Build.SUPPORTED_ABIS))
                .fact("fingerprint", Build.FINGERPRINT);
        gl(report.section("GPU (EGL probe of this process)"));
        GraphicsSettings.describe(context, report.section("Graphics"));
        ExitDiagnostics.describe(context, report);
        try {
            Process logcat =
                    new ProcessBuilder(
                                    "logcat", "-d", "-v", "threadtime", "-b", "main,system,crash")
                            .redirectErrorStream(true)
                            .start();
            try (BufferedReader reader =
                    new BufferedReader(
                            new InputStreamReader(
                                    logcat.getInputStream(), StandardCharsets.UTF_8))) {
                report.readLog(reader);
            }
            int status = logcat.waitFor();
            if (status != 0) report.logProblem("logcat exited with " + status);
        } catch (IOException error) {
            report.logProblem(error.toString());
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            report.logProblem("interrupted");
        }
        // The engine's logs: fs::GetLogsPath() = storagePaths().user / "Logs", user = <internal
        // storage>/storm (engine fs.h, android_runtime.hpp).
        String text = report.finish(new File(context.getFilesDir(), "storm/Logs"));
        File file =
                File.createTempFile(
                        "seawolves-diagnostics-" + stamp + "-",
                        ".txt",
                        ShareSheet.directory(context, ShareSheet.DIAGNOSTICS));
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        return file;
    }

    /**
     * The installed game and the script patch set the engine applies to it, identified exactly as the launcher's game
     * files screen does (ArchiveInstaller.describeInstalled: the engine's rule, run by the archive reader).
     */
    private static void game(Context context, DiagnosticsReport report) {
        String path;
        try {
            path = GameStorage.readyPath(context);
        } catch (IOException none) {
            report.fact("installed", "none (" + none.getMessage() + ")");
            return;
        }
        try {
            ArchiveInstaller.GameRoot root =
                    ArchiveInstaller.describeInstalled(
                            new File(path),
                            new File(context.getCacheDir(), "diagnostics-game-scan"),
                            context.getAssets());
            ArchiveInstaller.Identification identity = root.identification;
            report.fact(
                            "installed",
                            root.name()
                                    + (root.version.isEmpty() ? "" : " " + root.version)
                                    + ", "
                                    + root.files
                                    + " files, "
                                    + root.bytes
                                    + " bytes")
                    .fact(
                            "script patch set",
                            DiagnosticsReport.patchSet(
                                    identity.identified,
                                    identity.setId,
                                    identity.setVersion,
                                    identity.testedVersions));
        } catch (Exception error) {
            report.fact("UNAVAILABLE", "cannot describe the installed game: " + error);
        }
    }

    /** GL_VENDOR/RENDERER/VERSION from a 1x1 pbuffer context: answers even when the game never started. */
    private static void gl(DiagnosticsReport report) {
        EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        int[] version = new int[2], count = new int[1];
        EGLConfig[] configs = new EGLConfig[1];
        if (display == EGL14.EGL_NO_DISPLAY
                || !EGL14.eglInitialize(display, version, 0, version, 1)) {
            report.fact(
                    "UNAVAILABLE", "eglInitialize 0x" + Integer.toHexString(EGL14.eglGetError()));
            return;
        }
        EGLContext context = EGL14.EGL_NO_CONTEXT;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        try {
            final int esBit = 0x40; // EGL_OPENGL_ES3_BIT_KHR: the game asks for ES 3
            int[] attributes = {
                EGL14.EGL_RENDERABLE_TYPE,
                esBit,
                EGL14.EGL_SURFACE_TYPE,
                EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_NONE
            };
            if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0)
                    || count[0] == 0) {
                report.fact(
                        "UNAVAILABLE",
                        "no OpenGL ES 3 pbuffer config, 0x"
                                + Integer.toHexString(EGL14.eglGetError()));
                return;
            }
            context =
                    EGL14.eglCreateContext(
                            display,
                            configs[0],
                            EGL14.EGL_NO_CONTEXT,
                            new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE},
                            0);
            surface =
                    EGL14.eglCreatePbufferSurface(
                            display,
                            configs[0],
                            new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE},
                            0);
            if (context == EGL14.EGL_NO_CONTEXT
                    || surface == EGL14.EGL_NO_SURFACE
                    || !EGL14.eglMakeCurrent(display, surface, surface, context)) {
                report.fact(
                        "UNAVAILABLE",
                        "ES 3 context, 0x" + Integer.toHexString(EGL14.eglGetError()));
                return;
            }
            report.fact(
                            "EGL",
                            version[0]
                                    + "."
                                    + version[1]
                                    + " "
                                    + EGL14.eglQueryString(display, EGL14.EGL_VENDOR))
                    .fact("GL_VENDOR", GLES20.glGetString(GLES20.GL_VENDOR))
                    .fact("GL_RENDERER", GLES20.glGetString(GLES20.GL_RENDERER))
                    .fact("GL_VERSION", GLES20.glGetString(GLES20.GL_VERSION))
                    .fact(
                            "GL_SHADING_LANGUAGE_VERSION",
                            GLES20.glGetString(GLES20.GL_SHADING_LANGUAGE_VERSION));
        } finally {
            EGL14.eglMakeCurrent(
                    display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
            EGL14.eglReleaseThread();
        }
    }
}
