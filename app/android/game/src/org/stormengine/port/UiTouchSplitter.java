package org.stormengine.port;

import android.view.MotionEvent;

import java.util.HashMap;
import java.util.Map;

/**
 * Splits one stream of touches between the portable UI and the views of this host, per
 * pointer and at pointer-down: a pointer that goes down on something of the UI (an
 * interactive HUD element, an open sheet, a game interface) belongs to the UI for its whole
 * gesture; every other pointer goes on to the Android views (camera look, Java HUD, forms).
 * The stick can be held with one finger while another looks around or presses a button.
 *
 * Android itself cannot do this split: the UI is one full-screen view, and a view that
 * already holds a pointer is given every further pointer inside its bounds.
 */
final class UiTouchSplitter {
    /** The UI's side of the split: {@link EngineUi} in the game. */
    interface Ui {
        /** Touch phases of {@link #touch}, as the UI host's TouchPhase. */
        int DOWN = 0, MOVE = 1, UP = 2, CANCEL = 3;

        boolean wantsTouch(float x, float y);

        /** One sample of a pointer; `time` is its MotionEvent time (SystemClock.uptimeMillis). */
        void touch(int phase, int pointer, float x, float y, long time);
    }

    /** The views under the UI: receives the stream of the pointers the UI did not want. */
    interface Views {
        boolean dispatch(MotionEvent event);
    }

    private final Ui ui;
    private final Views views;
    private final HashMap<Integer, float[]> claimed = new HashMap<>();

    /** Pointers down on the views' side, and when its current stream began. */
    private static final class PassedPointer {
        final MotionEvent.PointerProperties properties = new MotionEvent.PointerProperties();
        final MotionEvent.PointerCoords coords = new MotionEvent.PointerCoords();
    }

    private final HashMap<Integer, PassedPointer> passed = new HashMap<>();
    private long passedDownTime;

    UiTouchSplitter(Ui ui, Views views) {
        this.ui = ui;
        this.views = views;
    }

    boolean dispatch(MotionEvent event) {
        final int action = event.getActionMasked(), index = event.getActionIndex();
        final int pointer = event.getPointerId(index);
        final boolean down =
                action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN;
        final boolean up =
                action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP;
        if (action == MotionEvent.ACTION_DOWN) {
            cancelClaimed(event);
            cancelPassed(event);
        } // a new gesture: nothing is held any more

        // Remember the last position while each pointer still belongs to this gesture.
        // A new DOWN above must cancel with the old position, even if Android reuses its ID.
        for (int i = 0; i < event.getPointerCount(); i++) {
            float[] position = claimed.get(event.getPointerId(i));
            if (position != null) {
                position[0] = event.getX(i);
                position[1] = event.getY(i);
            }
        }

        // The UI's part of the event.
        final boolean mixed = !claimed.isEmpty(); // the event carries pointers of the UI
        boolean ours = false; // this event's own pointer is the UI's
        if (down && ui != null && ui.wantsTouch(event.getX(index), event.getY(index))) {
            claimed.put(pointer, new float[] {event.getX(index), event.getY(index)});
            ui.touch(Ui.DOWN, pointer, event.getX(index), event.getY(index), event.getEventTime());
            ours = true;
        } else if (action == MotionEvent.ACTION_MOVE) {
            // Every sample, oldest first: a move batches the ones since the last frame (history),
            // and the UI measures the finger's speed from all of them with their own times.
            final int history = event.getHistorySize();
            for (int h = 0; h <= history; h++)
                for (int i = 0; i < event.getPointerCount(); i++) {
                    if (!claimed.containsKey(event.getPointerId(i))) continue;
                    if (h < history)
                        ui.touch(
                                Ui.MOVE,
                                event.getPointerId(i),
                                event.getHistoricalX(i, h),
                                event.getHistoricalY(i, h),
                                event.getHistoricalEventTime(h));
                    else
                        ui.touch(
                                Ui.MOVE,
                                event.getPointerId(i),
                                event.getX(i),
                                event.getY(i),
                                event.getEventTime());
                }
        } else if (up && claimed.remove(pointer) != null) {
            ui.touch(Ui.UP, pointer, event.getX(index), event.getY(index), event.getEventTime());
            ours = true;
        } else if (action == MotionEvent.ACTION_CANCEL) {
            cancelClaimed(event);
        }

        // The views' part.
        if (!ours && !mixed) {
            // Every pointer of this event is theirs: they get it as it came.
            if (down) {
                if (passed.isEmpty()) passedDownTime = event.getEventTime();
                passed.put(pointer, new PassedPointer());
            } else if (up) passed.remove(pointer);
            else if (action == MotionEvent.ACTION_CANCEL) passed.clear();
            rememberPassed(event);
            final boolean accepted = views.dispatch(event);
            if (!accepted && action == MotionEvent.ACTION_DOWN) passed.clear();
            return accepted;
        }
        final boolean theirs = !ours && (down || up); // this event's own pointer is the views'
        if (theirs && down) {
            if (passed.isEmpty()) passedDownTime = event.getEventTime();
            passed.put(pointer, new PassedPointer());
        }
        if (!passed.isEmpty()) {
            rememberPassed(event);
            int part = MotionEvent.ACTION_MOVE;
            if (action == MotionEvent.ACTION_CANCEL) part = MotionEvent.ACTION_CANCEL;
            else if (theirs) part = down ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP;
            forward(event, part, pointer);
            if (theirs && up) passed.remove(pointer);
            if (action == MotionEvent.ACTION_CANCEL) passed.clear();
        }
        return true;
    }

    /** A loading barrier takes ownership without waiting for another physical event. */
    void cancelOwners(MotionEvent event) {
        cancelClaimed(event);
        cancelPassed(event);
    }

    private void cancelClaimed(MotionEvent event) {
        // The terminating event may omit an old pointer entirely (lost UP/new gesture).
        for (Map.Entry<Integer, float[]> pointer : claimed.entrySet()) {
            float[] position = pointer.getValue();
            ui.touch(Ui.CANCEL, pointer.getKey(), position[0], position[1], event.getEventTime());
        }
        claimed.clear();
    }

    private void rememberPassed(MotionEvent event) {
        for (int i = 0; i < event.getPointerCount(); i++) {
            PassedPointer pointer = passed.get(event.getPointerId(i));
            if (pointer == null) continue;
            event.getPointerProperties(i, pointer.properties);
            event.getPointerCoords(i, pointer.coords);
        }
    }

    private void cancelPassed(MotionEvent event) {
        if (passed.isEmpty()) return;
        final int count = passed.size();
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[count];
        int at = 0;
        for (PassedPointer pointer : passed.values()) {
            properties[at] = pointer.properties;
            coords[at++] = pointer.coords;
        }
        final MotionEvent cancel =
                MotionEvent.obtain(
                        passedDownTime,
                        event.getEventTime(),
                        MotionEvent.ACTION_CANCEL,
                        count,
                        properties,
                        coords,
                        event.getMetaState(),
                        event.getButtonState(),
                        event.getXPrecision(),
                        event.getYPrecision(),
                        event.getDeviceId(),
                        event.getEdgeFlags(),
                        event.getSource(),
                        event.getFlags());
        passed.clear();
        try {
            views.dispatch(cancel);
        } finally {
            cancel.recycle();
        }
    }

    /**
     * Gives the views their part of an event that also carries pointers of the UI: the same
     * moment with only their pointers in it. `kind` is ACTION_MOVE, ACTION_CANCEL, or
     * ACTION_DOWN / ACTION_UP of `pointer` (told as a pointer-down / pointer-up when the views
     * hold other pointers too).
     */
    private void forward(MotionEvent event, int kind, int pointer) {
        final int count = passed.size();
        final MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
        final MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[count];
        int at = 0, changed = 0;
        for (int i = 0; i < event.getPointerCount() && at < count; i++) {
            if (!passed.containsKey(event.getPointerId(i))) continue;
            properties[at] = new MotionEvent.PointerProperties();
            coords[at] = new MotionEvent.PointerCoords();
            event.getPointerProperties(i, properties[at]);
            event.getPointerCoords(i, coords[at]);
            if (event.getPointerId(i) == pointer) changed = at;
            at++;
        }
        if (at != count) {
            cancelPassed(event);
            return;
        } // a pointer left without an up: the views' stream is over
        int action = kind;
        if (count > 1 && kind == MotionEvent.ACTION_DOWN)
            action =
                    MotionEvent.ACTION_POINTER_DOWN
                            | changed << MotionEvent.ACTION_POINTER_INDEX_SHIFT;
        if (count > 1 && kind == MotionEvent.ACTION_UP)
            action =
                    MotionEvent.ACTION_POINTER_UP
                            | changed << MotionEvent.ACTION_POINTER_INDEX_SHIFT;
        final MotionEvent part =
                MotionEvent.obtain(
                        passedDownTime,
                        event.getEventTime(),
                        action,
                        count,
                        properties,
                        coords,
                        event.getMetaState(),
                        event.getButtonState(),
                        event.getXPrecision(),
                        event.getYPrecision(),
                        event.getDeviceId(),
                        event.getEdgeFlags(),
                        event.getSource(),
                        event.getFlags());
        try {
            views.dispatch(part);
        } finally {
            part.recycle();
        }
    }
}
