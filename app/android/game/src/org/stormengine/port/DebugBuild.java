package org.stormengine.port;

import android.content.Context;
import android.content.pm.ApplicationInfo;

/**
 * Debug or release build: ApplicationInfo.FLAG_DEBUGGABLE, which tools/android/build_apk.py sets for
 * --build-type debug (local developer builds) and never for release (the GitHub and Play release APKs).
 * The app's one source for debug-only screens and notices; the engine reads the same flag
 * (platform/android_ui.hpp debugBuild). Non-fatal errors are shown on screen only in debug builds; release
 * builds keep them in logcat, the engine logs and the diagnostics export.
 */
final class DebugBuild {
    private DebugBuild() {}

    static boolean on(Context context) {
        return (context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }
}
