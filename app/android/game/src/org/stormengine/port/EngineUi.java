package org.stormengine.port;

/**
 * The game's portable UI lives in the engine (engine src/libs/ui_host, libengine.so): one UI
 * host on the engine thread, drawn inside the engine frame after the game picture. The engine
 * gives it its states and interface models itself; this side only routes the pointers the UI
 * takes, writes the channels Android owns ("presentation", "appSettings") and passes the
 * system insets and editor answers. Calls are queued and applied at the top of the next
 * engine frame; the queries answer at once from the UI's last frame. Any thread.
 *
 * What the UI asks of Android arrives on {@link GameActivity}: onUiHudInput, onUiAction,
 * onUiTextRequest, uiFailed (called on the engine thread).
 */
final class EngineUi implements UiTouchSplitter.Ui, UiTextEditor.Answer {
    /** At pointer-down: does a pointer going down at (x, y), in surface pixels, belong to the UI? */
    @Override
    public boolean wantsTouch(float x, float y) {
        return nativeWantsTouch(x, y);
    }

    /** One sample of a pointer the UI took: phase {@link UiTouchSplitter.Ui#DOWN} … {@link UiTouchSplitter.Ui#CANCEL}, its event time. */
    @Override
    public void touch(int phase, int pointer, float x, float y, long time) {
        nativeTouch(phase, pointer, x, y, time);
    }

    /** The platform took the gestures away: every pointer of the UI is cancelled, every held HUD control released. */
    void cancelInput() {
        nativeCancelInput();
    }

    /** The back key belongs to the UI now (an open HUD sheet or its own modal); give it with {@link #back}. */
    boolean wantsBack() {
        return nativeWantsBack();
    }

    void back() {
        nativeBack();
    }

    /** A game interface is shown by the UI. */
    boolean interfaceActive() {
        return nativeInterfaceActive();
    }

    void setState(String channel, byte[] json) {
        nativeSetState(channel, json);
    }

    @Override
    public void submitText(String token, String text, boolean accepted) {
        nativeSubmitText(token, text, accepted);
    }

    /** The system bars, in pixels (the display cutout is ignored: the UI runs under it). */
    void setSafeArea(int left, int top, int right, int bottom) {
        nativeSetSafeArea(left, top, right, bottom);
    }

    /** A toast in the UI's one toast place, the top edge (tone info | warn | danger | ok). */
    void toast(String text, String tone) {
        nativeToast(text, tone);
    }

    private static native boolean nativeWantsTouch(float x, float y);

    private static native void nativeTouch(int phase, int pointer, float x, float y, long time);

    private static native void nativeCancelInput();

    private static native boolean nativeWantsBack();

    private static native void nativeBack();

    private static native boolean nativeInterfaceActive();

    private static native void nativeSetState(String channel, byte[] json);

    private static native void nativeSubmitText(String token, String text, boolean accepted);

    private static native void nativeToast(String text, String tone);

    private static native void nativeSetSafeArea(float left, float top, float right, float bottom);
}
