package org.stormengine.port;

import java.util.ArrayList;

/**
 * The HUD's screen gate (MobileHud): the open interface, the short gap when the game closes one (native scripts can
 * close a menu a frame before opening the next: the menu keeps the screen until GAP_MS pass or the next one opens,
 * and the touches made meanwhile wait for what follows), and the loading shield (a blocking location load takes
 * every touch: the engine draws its stock loading artwork). T is the platform's touch event.
 */
final class MenuTransition<T> {
    static final long GAP_MS = 150;

    /** What the HUD does for the gate. */
    interface Host<T> {
        /** A gap starts: the closing menu's touches are cancelled, and commit() is to run after GAP_MS. */
        void startGap();

        /** The pending commit() is not to run. */
        void stopGap();

        /** `screen` is shown now ("": gameplay). */
        void apply(String screen);

        boolean dispatch(T touch);

        T copy(T touch);

        void recycle(T touch);
    }

    private final Host<T> host;
    private String screen = "";
    private boolean pending;
    private LocationReadiness.Snapshot readiness;

    /** Touches made during the gap, in order with their own times; the closing menu gets none of them. */
    private final ArrayList<T> held = new ArrayList<>();

    MenuTransition(Host<T> host) {
        this.host = host;
    }

    String screen() {
        return screen;
    }

    /** A closing menu still holds the screen (Back is consumed meanwhile). */
    boolean pending() {
        return pending;
    }

    /** A location load blocks the game: nothing of the HUD shows, every touch is taken. */
    boolean blocks() {
        return readiness != null && readiness.blocks();
    }

    /** The HUD keeps what it shows: a closing menu holds the screen (a blocking load does not wait for it). */
    boolean holdsScreen() {
        return pending && !blocks();
    }

    void show(String name) {
        if (name.isEmpty() && !screen.isEmpty()) {
            if (pending) return; // a repeated empty keeps the first deadline
            pending = true;
            host.startGap();
            return;
        }
        host.stopGap();
        pending = false;
        screen = name;
        host.apply(name);
        deliverHeld(true);
    }

    /** The gap's end (GAP_MS after it started): gameplay shows. */
    void commit() {
        if (!pending) return;
        pending = false;
        screen = "";
        host.apply("");
        deliverHeld(true);
    }

    /** A touch on the HUD: taken by the shield, held during a gap, else dispatched. */
    boolean touch(T event) {
        if (blocks()) return true;
        if (pending) {
            held.add(host.copy(event));
            return true;
        }
        return host.dispatch(event);
    }

    /** The input went off: what was held is recycled, not dispatched; the gap goes on. */
    void dropHeld() {
        deliverHeld(false);
    }

    /** The HUD leaves its window: nothing is left to touch; what was held is recycled, not dispatched. */
    void detached() {
        host.stopGap();
        deliverHeld(false);
        commit();
    }

    /**
     * A location readiness from the engine; false when only its producer's phase changed (nothing to do). A blocking
     * one ends a gap and drops the held touches; the HUD then cancels what owned touches.
     */
    boolean readiness(LocationReadiness.Snapshot next) {
        LocationReadiness.Snapshot previous = readiness;
        readiness = next;
        if (previous != null
                && previous.generation == next.generation
                && previous.state == next.state
                && previous.active == next.active) return false;
        if (next.blocks()) {
            host.stopGap();
            pending = false;
            deliverHeld(false);
        }
        return true;
    }

    private void deliverHeld(boolean dispatch) {
        if (held.isEmpty()) return;
        ArrayList<T> touches = new ArrayList<>(held);
        held.clear();
        for (T event : touches)
            try {
                if (dispatch) host.dispatch(event);
            } finally {
                host.recycle(event);
            }
    }
}
