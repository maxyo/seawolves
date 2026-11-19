package org.stormengine.port;

import android.app.Activity;
import android.os.Build;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

/**
 * Platform predictive-back registration without AndroidX. On API 33+ (and always
 * for targetSdk 36 on Android 16) the system no longer calls onBackPressed or
 * dispatches KEYCODE_BACK when enableOnBackInvokedCallback is set, so screens that
 * consume Back register here. Older releases still route through onBackPressed,
 * which activities forward to the same handler. A disabled handler is unregistered
 * so the system can play its back-to-home/cross-activity animation.
 */
final class BackNavigation {
    private final Activity activity;
    private final Runnable handler;
    private Object callback;
    private boolean enabled;

    BackNavigation(Activity activity, Runnable handler) {
        this.activity = activity;
        this.handler = handler;
    }

    void setEnabled(boolean value) {
        if (enabled == value) return;
        enabled = value;
        if (Build.VERSION.SDK_INT < 33) return;
        OnBackInvokedDispatcher dispatcher = activity.getOnBackInvokedDispatcher();
        if (value) {
            OnBackInvokedCallback registered = handler::run;
            callback = registered;
            dispatcher.registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, registered);
        } else if (callback != null) {
            dispatcher.unregisterOnBackInvokedCallback((OnBackInvokedCallback) callback);
            callback = null;
        }
    }

    boolean enabled() {
        return enabled;
    }
}
