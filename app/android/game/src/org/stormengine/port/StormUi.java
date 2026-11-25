package org.stormengine.port;

import android.content.res.AssetManager;

/**
 * Native entry points of the portable UI runtime (libstorm_ui.so) for the screens without the engine; the game's UI
 * is hosted by the engine ({@link EngineUi}). Call every method on the GL thread that owns the UI surface.
 *
 * <p>One host per process (RmlUi's state is process-global): {@link #nativeCreate} returns 0 while another host
 * lives, so screens that can be alive together run in different processes (launcher, :downloads, :gallery).
 */
final class StormUi {
    static final int DOWN = 0, MOVE = 1, UP = 2, CANCEL = 3;

    // Every APK packages the UI runtime (build_apk.py): failing to load it is an error, raised with
    // its cause.
    static {
        System.loadLibrary("storm_ui");
    }

    private StormUi() {}

    /** `showErrors`: UI errors are also shown as a toast (DebugBuild); otherwise they are only logged. */
    static native long nativeCreate(
            AssetManager assets,
            String directory,
            int width,
            int height,
            float density,
            String gameRoot,
            boolean showErrors);

    static native void nativeDestroy(long handle);

    static native boolean nativeLoadDocument(long handle, String path);

    static native void nativeResize(long handle, int width, int height, float density);

    /** One pointer sample; `time` is its MotionEvent time (SystemClock.uptimeMillis). */
    static native void nativeTouch(
            long handle, int phase, int pointer, float x, float y, long time);

    /** Settles the UI without drawing: negative when the shown frame is out of date (request a render), else seconds until the next prepare. */
    static native double nativePrepare(long handle);

    /** Draws a frame (prepare included); returns seconds until the next prepare is needed (0 while animating). */
    static native double nativeFrame(long handle);

    static native void nativeToast(long handle, String text, String tone);

    /** Queued actions as {screen, node, verb, value, engine command, …}, or null. */
    static native String[] nativeTakeActions(long handle);

    /** A named state channel of the screen ("launcher", "downloads", …): JSON bytes. */
    static native boolean nativeSetState(long handle, String channel, byte[] json);

    static native void nativeBack(long handle);

    /** Pending editor request {token, text, title, numeric, x, y, width, height} in pixels, or null. */
    static native String[] nativeTakeTextRequest(long handle);

    static native void nativeSubmitText(long handle, String token, String text, boolean accepted);
}
