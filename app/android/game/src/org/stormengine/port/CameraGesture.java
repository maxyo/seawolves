package org.stormengine.port;

import android.view.MotionEvent;

/**
 * The fingers of the camera-look surface (MobileHud): one finger turns the camera, two pinch
 * its zoom. Only pointers the portable UI did not claim arrive here (UiTouchSplitter), so a
 * thumb on the move stick or on a HUD control is never part of a pinch.
 *
 * - The first finger looks: each sample of a move (the historical ones too, with their times)
 *   is a camera drag from where it was; its lift is told apart from a cancel, so the world map's
 *   turn can coast on after a release and stop dead on a cancel or a pinch.
 * - A second finger makes it a pinch: the look ends where it is and each move sends the factor
 *   by which the distance between the two fingers changed. A third finger is ignored.
 * - A pinch ends when either of its fingers lifts; the finger left down does nothing until it
 *   lifts too, so ending a pinch never turns or zooms the camera.
 *
 * Android's ScaleGestureDetector is not used: it scales over every pointer of the event and
 * starts only after a span slop, while the look and the pinch must hand over in one state
 * (which finger looks, when a pinch begins and ends) and be checked on the build host.
 */
final class CameraGesture {
    /** What the gesture does to the camera. */
    interface Camera {
        /** A look finger landed at x, y (pixels) at `time` (its MotionEvent time). */
        void lookStart(float x, float y, long time);

        /**
         * The look finger moved by dx, dy pixels to x, y at `time`: once per sample, the
         * historical ones of a batched move first (the world map's turn coasts on at the
         * finger's speed, measured from them).
         */
        void look(float dx, float dy, float x, float y, long time);

        /** The look ended: `lifted` at `time` (the turn may coast on), or cancelled or turned into a pinch (it stops). */
        void lookEnd(boolean lifted, long time);

        /** The fingers' distance changed by this factor (> 1: spread, the camera comes nearer). */
        void zoom(float scale);
    }

    /** Below this distance between two fingers (px) their ratio is noise, not a pinch. */
    static final float MIN_SPAN = 8f;

    private final Camera camera;
    private int look = -1,
            other = -1; // pointer ids: the look finger, and the second finger of a pinch
    private boolean spent; // a pinch ended with a finger still down: nothing until all lift
    private float lastX, lastY, lastSpan;

    CameraGesture(Camera camera) {
        this.camera = camera;
    }

    boolean pinching() {
        return look >= 0 && other >= 0;
    }

    /** One event of the look surface; always consumed (blank touches are no native mouse clicks). */
    boolean onTouch(MotionEvent event) {
        final int action = event.getActionMasked(), index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                cancel(); // a new gesture: whatever was held is over
            // fall through
            case MotionEvent.ACTION_POINTER_DOWN:
                if (spent) break;
                if (look < 0) {
                    look = event.getPointerId(index);
                    lastX = event.getX(index);
                    lastY = event.getY(index);
                    camera.lookStart(lastX, lastY, event.getEventTime());
                } else if (other < 0) {
                    other = event.getPointerId(index);
                    camera.lookEnd(false, event.getEventTime());
                    lastSpan = span(event);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (pinching()) {
                    final float span = span(event);
                    if (span < 0) {
                        release(true);
                        break;
                    } // a finger left without an up
                    if (lastSpan >= MIN_SPAN && span >= MIN_SPAN && span != lastSpan)
                        camera.zoom(span / lastSpan);
                    lastSpan = span;
                } else if (look >= 0) {
                    final int at = event.findPointerIndex(look);
                    if (at < 0) {
                        camera.lookEnd(false, event.getEventTime());
                        release(true);
                        break;
                    }
                    final int history = event.getHistorySize();
                    for (int h = 0; h <= history; h++) {
                        final float x = h < history ? event.getHistoricalX(at, h) : event.getX(at);
                        final float y = h < history ? event.getHistoricalY(at, h) : event.getY(at);
                        // The splitter repeats the look finger in a move when a UI finger changes.
                        if (x != lastX || y != lastY)
                            camera.look(
                                    x - lastX,
                                    y - lastY,
                                    x,
                                    y,
                                    h < history
                                            ? event.getHistoricalEventTime(h)
                                            : event.getEventTime());
                        lastX = x;
                        lastY = y;
                    }
                }
                break;
            case MotionEvent.ACTION_POINTER_UP:
                {
                    final int pointer = event.getPointerId(index);
                    if (pointer == look || pointer == other) {
                        if (pointer == look && other < 0)
                            camera.lookEnd(true, event.getEventTime()); // the look finger lifted
                        release(pinching() || event.getPointerCount() > 2);
                    }
                    break;
                }
            case MotionEvent.ACTION_UP:
                if (look >= 0 && other < 0 && event.getPointerId(index) == look) {
                    camera.lookEnd(true, event.getEventTime());
                    look = -1;
                }
                cancel();
                break;
            case MotionEvent.ACTION_CANCEL:
                cancel();
                break;
            default:
                break;
        }
        return true;
    }

    /** Ends what is held and forgets the fingers (cancelled input, a new gesture, the surface gone). */
    void cancel() {
        if (look >= 0 && other < 0) camera.lookEnd(false, 0);
        look = other = -1;
        spent = false;
    }

    /** One of the tracked fingers is gone; `stay` keeps the rest of this gesture inert. */
    private void release(boolean stay) {
        look = other = -1;
        spent = stay;
    }

    /** The distance between the pinch's fingers in this event, or -1 when one is missing. */
    private float span(MotionEvent event) {
        final int a = event.findPointerIndex(look), b = event.findPointerIndex(other);
        if (a < 0 || b < 0) return -1;
        final float dx = event.getX(a) - event.getX(b), dy = event.getY(a) - event.getY(b);
        return (float) Math.sqrt(dx * dx + dy * dy);
    }
}
