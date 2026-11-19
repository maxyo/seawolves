package org.stormengine.port;

/** Actual MobileHud method bodies are inserted by run.py. Android drawing/dispatch is a
 * stand-in: this verifies the app barrier, not the native stock artwork or its presentation. */
final class MobileHudLoadingCheck extends LoadingViewStub {
    LocationReadiness.Snapshot readiness;
    boolean emptyScreenPending, uiHud = true, loading;
    String screen = "saves";
    Object emptyScreenCommit = new Object();
    final GameStub game = new GameStub();
    final TouchStub touches = new TouchStub();
    final java.util.ArrayList<MotionEvent> heldTouches = new java.util.ArrayList<>();
    int gestureCancels, bufferedDispatches, interfaceCalls, gameplayCalls;

    void removeCallbacks(Object ignored) {}

    void deliverHeldTouches(boolean dispatch) {
        for (MotionEvent event : heldTouches) {
            if (dispatch) ++bufferedDispatches;
            event.recycle();
        }
        heldTouches.clear();
    }

    void cancelGestures() {
        ++gestureCancels;
    }

    // An open interface shows in the portable UI: rebuild presents it with no Android view.
    {
        game.presented =
                () -> {
                    if (!screen.isEmpty() && (readiness == null || !readiness.blocks()))
                        ++interfaceCalls;
                };
    }

    void gameplay() {
        ++gameplayCalls;
        ++children;
    }

    // PRODUCTION_METHODS
    static void check(boolean value, String label) {
        if (!value) throw new AssertionError(label);
    }

    public static void main(String[] args) {
        MobileHudLoadingCheck hud = new MobileHudLoadingCheck();
        LocationReadiness controller = new LocationReadiness();
        hud.children = 3;
        hud.emptyScreenPending = true;
        MotionEvent held = new MotionEvent(MotionEvent.ACTION_DOWN);
        hud.heldTouches.add(held);
        LocationReadiness.Snapshot preparing = controller.accept(1, "preparing", true, false, "");
        hud.showReadiness(preparing);
        check(hud.children == 0 && hud.background == Color.TRANSPARENT, "stock artwork unobscured");
        check(
                hud.touches.cancels == 1 && hud.gestureCancels == 1 && hud.game.releases >= 1,
                "old owners cancelled");
        check(
                held.recycled
                        && hud.heldTouches.isEmpty()
                        && hud.bufferedDispatches == 0
                        && !hud.emptyScreenPending,
                "old held samples discarded");
        check(
                hud.interfaceCalls == 0 && hud.gameplayCalls == 0 && !hud.uiHud,
                "no Android menu or gameplay behind load");
        for (int action :
                new int[] {
                    MotionEvent.ACTION_DOWN,
                    MotionEvent.ACTION_MOVE,
                    MotionEvent.ACTION_UP,
                    MotionEvent.ACTION_CANCEL
                })
            check(
                    hud.dispatchTouchEvent(new MotionEvent(action)),
                    "transparent shield consumes " + action);
        check(
                hud.parentDispatches == 0 && hud.touches.dispatches == 0,
                "no touch reaches SDL or portable UI");
        int removals = hud.removals;
        hud.showReadiness(controller.accept(1, "preparing", true, false, ""));
        check(hud.removals == removals && hud.children == 0, "a refresh adds no panel/spinner");
        hud.showReadiness(controller.accept(1, "error", true, true, ""));
        check(
                hud.children == 0
                        && hud.background == Color.TRANSPARENT
                        && hud.dispatchTouchEvent(new MotionEvent(0)),
                "error keeps stock artwork and input shield");
        check(
                "location-load:1\tcancel"
                        .equals(controller.command(controller.current(), "cancel")),
                "Back cancellation remains generation bound");
        hud.showReadiness(controller.accept(1, "cancelled", false, false, "cancel"));
        check(
                hud.children == 0 && hud.dispatchTouchEvent(new MotionEvent(0)),
                "accepted cancel shields until stock quit");
        hud.showReadiness(controller.accept(2, "preparing", true, false, ""));
        hud.showReadiness(controller.accept(2, "ready", false, false, ""));
        check(
                hud.interfaceCalls == 1 && hud.children == 0 && hud.background == Color.TRANSPARENT,
                "ready restores ordinary screen");
        hud.dispatchTouchEvent(new MotionEvent(0));
        check(hud.touches.dispatches == 1, "ordinary dispatch resumes after ready");
        System.out.println(
                "PASS actual MobileHud transparent loading barrier / teardown / lifecycle");
    }
}

class LoadingViewStub {
    int children, background = 99, removals, parentDispatches;

    public boolean dispatchTouchEvent(MotionEvent event) {
        ++parentDispatches;
        return false;
    }

    void requestUnbufferedDispatch(MotionEvent event) {}

    void removeAllViews() {
        children = 0;
        ++removals;
    }

    void setBackgroundColor(int color) {
        background = color;
    }
}

class GameStub {
    int releases, published;
    Runnable presented = () -> {};

    void releaseVirtualControls() {
        ++releases;
    }

    void publishPresentation() {
        ++published;
        presented.run();
    }
}

class TouchStub {
    int dispatches, cancels;

    boolean dispatch(MotionEvent e) {
        ++dispatches;
        return true;
    }

    void cancelOwners(MotionEvent e) {
        ++cancels;
    }
}

class Color {
    static final int TRANSPARENT = 0;
}

class MotionEvent {
    static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3;
    final int action;
    boolean recycled;

    MotionEvent(int action) {
        this.action = action;
    }

    int getActionMasked() {
        return action;
    }

    static MotionEvent obtain(MotionEvent event) {
        return new MotionEvent(event.action);
    }

    static MotionEvent obtain(long down, long now, int action, int x, int y, int flags) {
        return new MotionEvent(action);
    }

    void recycle() {
        recycled = true;
    }
}

// The extracted production code retains its fully-qualified SystemClock call.
class android {
    static class os {
        static class SystemClock {
            static long uptimeMillis() {
                return 1;
            }
        }
    }
}
