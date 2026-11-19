package org.stormengine.port;

import java.util.ArrayList;

/** Production gate/rebuild/dispatch/lifecycle bodies come from run.py. */
public final class MenuTransitionCheck {
    static MobileHud ready(String screen) {
        FrameLayout.tasks.clear();
        android.os.SystemClock.now = 100;
        MobileHud hud = new MobileHud();
        hud.showScreen(screen);
        return hud;
    }

    static void touch(MobileHud hud, int action) {
        assert hud.dispatchTouchEvent(new MotionEvent(action));
    }

    public static void main(String[] args) {
        MobileHud hud = ready("game_menu");
        int renders = hud.rendered.size(), releases = hud.releases();
        touch(hud, MotionEvent.ACTION_DOWN);
        hud.showScreen("");
        assert hud.menuTransitionPending()
                && hud.screen().equals("game_menu")
                && hud.background == 1;
        assert hud.rendered.size() == renders;
        assert hud.menuTransitionPending() : "Back must be consumed while the menu is held";
        assert hud.releases() > releases
                && hud.forwarded.get(hud.forwarded.size() - 1) == MotionEvent.ACTION_CANCEL;
        assert !hud.childOwnsTouch && MotionEvent.recycled == 1;
        int forwarded = hud.forwarded.size();
        touch(hud, MotionEvent.ACTION_MOVE);
        touch(hud, MotionEvent.ACTION_UP);
        touch(hud, MotionEvent.ACTION_DOWN);
        touch(hud, MotionEvent.ACTION_UP);
        assert hud.forwarded.size() == forwarded && hud.childClicks == 0
                : "stale children must receive no input";
        int recycled = MotionEvent.recycled;
        hud.refresh();
        assert hud.rendered.size() == renders
                : "unrelated publications cannot remove the held menu";
        FrameLayout.advance(100);
        hud.showScreen("");
        assert FrameLayout.tasks.size() == 1 : "repeat empty must not extend the original deadline";
        FrameLayout.advance(49);
        assert hud.menuTransitionPending() && hud.rendered.size() == renders;
        FrameLayout.advance(1);
        assert !hud.menuTransitionPending()
                && hud.screen().isEmpty()
                && hud.background == Color.TRANSPARENT;
        assert hud.rendered.get(hud.rendered.size() - 1).equals("gameplay")
                && FrameLayout.tasks.isEmpty();
        // The four touches made during the gap were held, not dropped: what came after it got them,
        // in order.
        assert hud.forwarded.size() == forwarded + 4
                : "touches made during the gap must reach what follows it";
        assert hud.forwarded
                .subList(forwarded, forwarded + 4)
                .equals(
                        java.util.Arrays.asList(
                                MotionEvent.ACTION_MOVE,
                                MotionEvent.ACTION_UP,
                                MotionEvent.ACTION_DOWN,
                                MotionEvent.ACTION_UP));
        assert hud.childClicks == 1 && MotionEvent.recycled == recycled + 4
                : "the tap made in the gap lands once, and the held copies are recycled";

        // A new menu supersedes the transient empty immediately, without gameplay between them.
        hud = ready("game_menu");
        hud.showScreen("");
        Runnable stale = hud.pendingCallback();
        FrameLayout.advance(64);
        hud.showScreen("option_screen");
        assert !hud.menuTransitionPending()
                && hud.screen().equals("option_screen")
                && FrameLayout.tasks.isEmpty();
        assert hud.rendered.size() == 2 && hud.rendered.get(1).equals("option_screen");
        stale.run();
        FrameLayout.advance(300);
        assert hud.screen().equals("option_screen") && hud.rendered.size() == 2;
        final int asked = hud.unbuffered;
        touch(hud, MotionEvent.ACTION_DOWN);
        touch(hud, MotionEvent.ACTION_UP);
        assert hud.childClicks == 1;
        // Every gesture asks for its samples unbatched, once, at its down (the engine places
        // pointers per frame).
        assert hud.unbuffered == asked + 1;

        // A second transition owns a fresh timer; returning to the same screen is also a new
        // opening.
        hud.showScreen("");
        FrameLayout.advance(20);
        hud.showScreen("option_screen");
        assert !hud.menuTransitionPending() && FrameLayout.tasks.isEmpty();
        hud.showScreen("");
        FrameLayout.advance(150);
        assert hud.screen().isEmpty();
        renders = hud.rendered.size();
        hud.showScreen("");
        assert !hud.menuTransitionPending() && FrameLayout.tasks.isEmpty();
        assert hud.rendered.size() == renders + 1
                : "stable gameplay updates follow the normal path";

        // Detach drops callback ownership and commits current model state for a later attach.
        hud = ready("game_menu");
        hud.showScreen("");
        hud.onDetachedFromWindow();
        assert hud.detached
                && !hud.menuTransitionPending()
                && hud.screen().isEmpty()
                && FrameLayout.tasks.isEmpty();
        renders = hud.rendered.size();
        FrameLayout.advance(500);
        assert hud.rendered.size() == renders;
        hud.showScreen("ship");
        assert hud.screen().equals("ship") && !hud.menuTransitionPending();
        hud = ready("");
        assert !hud.menuTransitionPending() && FrameLayout.tasks.isEmpty();
        // A required loading barrier takes precedence over a pending empty menu and its callback.
        hud = ready("game_menu");
        hud.showScreen("");
        LocationReadiness load = new LocationReadiness();
        renders = hud.rendered.size();
        hud.setReadiness(load.accept(3, "preparing", true, false, ""));
        assert hud.rendered.size() == renders && hud.background == Color.TRANSPARENT
                : "native stock artwork stays unobscured";
        forwarded = hud.forwarded.size();
        touch(hud, MotionEvent.ACTION_DOWN);
        touch(hud, MotionEvent.ACTION_UP);
        assert hud.forwarded.size() == forwarded
                : "transparent loading barrier consumes input before child or gameplay dispatch";
        FrameLayout.advance(150);
        assert hud.screen().isEmpty()
                        && hud.background == Color.TRANSPARENT
                        && hud.rendered.size() == renders
                : "expired menu callback cannot mount gameplay behind readiness";
        hud.setReadiness(load.accept(3, "ready", false, false, ""));
        assert hud.rendered.get(hud.rendered.size() - 1).equals("gameplay")
                && hud.background == Color.TRANSPARENT;
        System.out.println(
                "PASS actual HUD transition gate: 150ms deadline, immediate supersession, retained"
                    + " opaque menu, touch cancellation/blocking, rebuild guard, detach cleanup");
    }
}

class android {
    static class os {
        static class SystemClock {
            static long now;

            static long uptimeMillis() {
                return now;
            }
        }
    }
}

class MotionEvent {
    static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3;
    static int recycled;
    final int action;

    MotionEvent(int action) {
        this.action = action;
    }

    int getActionMasked() {
        return action;
    }

    static MotionEvent obtain(long a, long b, int action, float x, float y, int meta) {
        return new MotionEvent(action);
    }

    static MotionEvent obtain(MotionEvent other) {
        return new MotionEvent(other.action);
    }

    void recycle() {
        recycled++;
    }
}

class Color {
    static final int TRANSPARENT = 0;
}

class FrameLayout {
    static class LayoutParams {
        LayoutParams(int width, int height) {}
    }

    static class Task {
        final FrameLayout owner;
        final Runnable callback;
        final long deadline;

        Task(FrameLayout owner, Runnable callback, long deadline) {
            this.owner = owner;
            this.callback = callback;
            this.deadline = deadline;
        }
    }

    static final ArrayList<Task> tasks = new ArrayList<>();
    final ArrayList<Integer> forwarded = new ArrayList<>();
    boolean childOwnsTouch, detached;
    int childClicks, background, unbuffered;

    public void requestUnbufferedDispatch(MotionEvent event) {
        unbuffered++;
    }

    public boolean dispatchTouchEvent(MotionEvent event) {
        forwarded.add(event.action);
        if (event.action == MotionEvent.ACTION_DOWN) childOwnsTouch = true;
        if (event.action == MotionEvent.ACTION_CANCEL) childOwnsTouch = false;
        if (event.action == MotionEvent.ACTION_UP) {
            if (childOwnsTouch) childClicks++;
            childOwnsTouch = false;
        }
        return true;
    }

    protected void onDetachedFromWindow() {
        detached = true;
    }

    void postDelayed(Runnable callback, long delay) {
        tasks.add(new Task(this, callback, android.os.SystemClock.now + delay));
    }

    void removeCallbacks(Runnable callback) {
        tasks.removeIf(task -> task.owner == this && task.callback == callback);
    }

    static void advance(long duration) {
        android.os.SystemClock.now += duration;
        while (true) {
            Task due = null;
            for (Task task : tasks)
                if (task.deadline <= android.os.SystemClock.now) {
                    due = task;
                    break;
                }
            if (due == null) return;
            tasks.remove(due);
            due.callback.run();
        }
    }

    void removeAllViews() {
        childOwnsTouch = false;
    }

    void removeView(Object view) {}

    void setBackgroundColor(int color) {
        background = color;
    }

    Object getContext() {
        return this;
    }
}

class GameActivity {
    int releases, presentations;
    Runnable presented = () -> {};

    void releaseVirtualControls() {
        releases++;
    }

    void closeInterfaceInUi() {}

    void publishPresentation() {
        presentations++;
        presented.run();
    }
}

class MenuText {
    MenuText(Object context, String language) {}
}

class JSONObject {
    boolean optBoolean(String key) {
        return false;
    }
}
