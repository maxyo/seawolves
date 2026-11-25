package org.stormengine.port;

import android.app.Activity;
import android.os.Build;
import android.window.OnBackInvokedDispatcher;

/**
 * Platform predictive-back registration without AndroidX. On API 33+ (and always for targetSdk 36 on Android 16)
 * the system no longer calls onBackPressed or dispatches KEYCODE_BACK when enableOnBackInvokedCallback is set, so
 * screens that consume Back register here. Older releases still route through onBackPressed, which activities
 * forward to the same handler.
 */
final class BackNavigation {
    static void register(Activity activity, Runnable handler) {
        if (Build.VERSION.SDK_INT < 33) return;
        activity.getOnBackInvokedDispatcher()
                .registerOnBackInvokedCallback(
                        OnBackInvokedDispatcher.PRIORITY_DEFAULT, handler::run);
    }

    private BackNavigation() {}
}
