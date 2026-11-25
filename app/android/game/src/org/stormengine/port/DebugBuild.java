package org.stormengine.port;

import android.content.Context;
import android.content.pm.ApplicationInfo;

/**
 * Debug or release build (ApplicationInfo.FLAG_DEBUGGABLE, set only for --build-type debug): debug builds show
 * debug-only screens and non-fatal errors on screen; release builds keep errors in the logs.
 */
final class DebugBuild {
    private DebugBuild() {}

    static boolean on(Context context) {
        return (context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }
}
