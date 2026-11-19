package org.stormengine.port;

import android.view.MotionEvent;

import java.util.ArrayList;

/** The production gesture behind the production splitter, with a recording camera and UI. */
public final class CameraGestureCheck {
    static final ArrayList<String> camera = new ArrayList<>(), ui = new ArrayList<>();
    static float zoom = 1; // the product of every pinch factor sent
    static long clock = 100;
    static boolean timed; // the camera's log shows positions and times
    static final CameraGesture gesture =
            new CameraGesture(
                    new CameraGesture.Camera() {
                        public void lookStart(float x, float y, long time) {
                            camera.add("start" + (timed ? " " + (int) x + " @" + time : ""));
                        }

                        public void look(float dx, float dy, float x, float y, long time) {
                            camera.add(
                                    "look "
                                            + (int) dx
                                            + ","
                                            + (int) dy
                                            + (timed ? " to " + (int) x + " @" + time : ""));
                        }

                        public void lookEnd(boolean lifted, long time) {
                            camera.add(lifted ? "lifted" + (timed ? " @" + time : "") : "end");
                        }

                        public void zoom(float scale) {
                            zoom *= scale;
                            camera.add("zoom");
                        }
                    });
    // The UI holds the floating move stick's zone (the left of the screen); the rest is the look
    // surface.
    static final UiTouchSplitter splitter =
            new UiTouchSplitter(
                    new UiTouchSplitter.Ui() {
                        public boolean wantsTouch(float x, float y) {
                            return x < 400;
                        }

                        public void touch(int phase, int pointer, float x, float y, long time) {
                            ui.add(
                                    new String[] {"down", "move", "up", "cancel"}[phase]
                                            + " "
                                            + pointer);
                        }
                    },
                    gesture::onTouch);

    /** pointers: id, x, y triples; `changed` = index of the pointer the action is about. */
    static void send(int action, int changed, int... pointers) {
        int count = pointers.length / 3;
        int[] ids = new int[count];
        float[] xs = new float[count], ys = new float[count];
        for (int i = 0; i < count; i++) {
            ids[i] = pointers[i * 3];
            xs[i] = pointers[i * 3 + 1];
            ys[i] = pointers[i * 3 + 2];
        }
        if (!splitter.dispatch(
                new MotionEvent(
                        100,
                        ++clock,
                        action | changed << MotionEvent.ACTION_POINTER_INDEX_SHIFT,
                        ids,
                        xs,
                        ys))) throw new AssertionError("an event was not consumed");
    }

    static void expect(String... lines) {
        if (!camera.equals(java.util.Arrays.asList(lines)))
            throw new AssertionError(
                    "expected " + java.util.Arrays.toString(lines) + " but got " + camera);
        camera.clear();
    }

    static void expectZoom(double expected) {
        if (Math.abs(zoom - expected) > 1e-4)
            throw new AssertionError("zoom " + zoom + ", expected " + expected);
        zoom = 1;
    }

    public static void main(String[] args) {
        final int DOWN = MotionEvent.ACTION_DOWN,
                UP = MotionEvent.ACTION_UP,
                MOVE = MotionEvent.ACTION_MOVE,
                CANCEL = MotionEvent.ACTION_CANCEL,
                PDOWN = MotionEvent.ACTION_POINTER_DOWN,
                PUP = MotionEvent.ACTION_POINTER_UP;

        // One finger looks.
        send(DOWN, 0, 1, 800, 300);
        send(MOVE, 0, 1, 780, 310);
        send(UP, 0, 1, 780, 310);
        expect("start", "look -20,10", "lifted");

        // A second finger makes it a pinch: the look ends where it was, the factors follow the
        // distance between the fingers (100 -> 200 -> 150 px: x2 then x0.75).
        send(DOWN, 0, 1, 600, 300);
        send(MOVE, 0, 1, 610, 300);
        send(PDOWN, 1, 1, 610, 300, 2, 710, 300);
        send(MOVE, 0, 1, 560, 300, 2, 760, 300);
        expectZoom(2.0);
        send(MOVE, 0, 1, 585, 300, 2, 735, 300);
        expectZoom(0.75);
        expect("start", "look 10,0", "end", "zoom", "zoom");

        // No jump when a finger lifts: the one left down neither turns nor zooms until it lifts.
        send(PUP, 1, 1, 585, 300, 2, 735, 300);
        send(MOVE, 0, 1, 500, 200);
        send(MOVE, 0, 1, 420, 120);
        send(UP, 0, 1, 420, 120);
        expect();
        expectZoom(1.0);
        // The same with the other finger lifting first, and then a new gesture looks again.
        send(DOWN, 0, 3, 600, 300);
        send(PDOWN, 1, 3, 600, 300, 4, 900, 300);
        send(MOVE, 0, 3, 650, 300, 4, 900, 300);
        expectZoom(250.0 / 300.0);
        send(PUP, 0, 3, 650, 300, 4, 900, 300);
        send(MOVE, 0, 4, 950, 340);
        send(UP, 0, 4, 950, 340);
        expect("start", "end", "zoom");
        send(DOWN, 0, 5, 700, 300);
        send(MOVE, 0, 5, 705, 300);
        send(UP, 0, 5, 705, 300);
        expect("start", "look 5,0", "lifted");

        // A third finger is no part of the pinch; its lift changes nothing.
        send(DOWN, 0, 1, 500, 100);
        send(PDOWN, 1, 1, 500, 100, 2, 600, 100);
        send(PDOWN, 2, 1, 500, 100, 2, 600, 100, 3, 900, 500);
        send(MOVE, 0, 1, 500, 100, 2, 700, 100, 3, 950, 300);
        expectZoom(2.0);
        send(PUP, 2, 1, 500, 100, 2, 700, 100, 3, 950, 300);
        send(MOVE, 0, 1, 500, 100, 2, 800, 100);
        expectZoom(1.5);
        send(CANCEL, 0, 1, 500, 100, 2, 800, 100);
        expect("start", "end", "zoom", "zoom");

        // Fingers on one spot: no factor out of noise.
        send(DOWN, 0, 1, 600, 300);
        send(PDOWN, 1, 1, 600, 300, 2, 603, 300);
        send(MOVE, 0, 1, 600, 300, 2, 606, 300);
        send(UP, 0, 1, 600, 300);
        expect("start", "end");

        // The thumb on the move stick (the UI's) and a second finger on the look surface: a
        // look, not a pinch, however the two move apart.
        send(DOWN, 0, 1, 150, 300);
        send(PDOWN, 1, 1, 150, 300, 2, 700, 300);
        send(MOVE, 0, 1, 100, 330, 2, 740, 280);
        send(MOVE, 0, 1, 60, 360, 2, 800, 260);
        send(PUP, 1, 1, 60, 360, 2, 800, 260);
        send(MOVE, 0, 1, 80, 340);
        send(UP, 0, 1, 80, 340);
        expect("start", "look 40,-20", "look 60,-20", "lifted");
        expectZoom(1.0);
        if (!ui.equals(java.util.Arrays.asList("down 1", "move 1", "move 1", "move 1", "up 1")))
            throw new AssertionError("the stick lost its thumb: " + ui);
        ui.clear();
        // Look first, then the stick: still no pinch, the look goes on.
        send(DOWN, 0, 2, 700, 300);
        send(PDOWN, 1, 2, 700, 300, 1, 150, 300);
        send(MOVE, 0, 2, 720, 300, 1, 100, 300);
        send(PUP, 1, 2, 720, 300, 1, 100, 300);
        send(UP, 0, 2, 720, 300);
        expect("start", "look 20,0", "lifted");
        expectZoom(1.0);

        // A batched move: every sample turns the camera, oldest first, each with its own time
        // (the world map's turn measures the finger's speed from them); the lift has its time.
        timed = true;
        send(DOWN, 0, 1, 600, 300);
        final long down = clock;
        if (!splitter.dispatch(
                new MotionEvent(
                                100,
                                down + 16,
                                MOVE,
                                new int[] {1},
                                new float[] {660},
                                new float[] {300})
                        .withHistory(
                                new long[] {down + 5, down + 11},
                                new float[][] {{620}, {640}},
                                new float[][] {{300}, {300}})))
            throw new AssertionError("a batched move was not consumed");
        clock = down + 20;
        send(UP, 0, 1, 660, 300);
        expect(
                "start 600 @" + down,
                "look 20,0 to 620 @" + (down + 5),
                "look 20,0 to 640 @" + (down + 11),
                "look 20,0 to 660 @" + (down + 16),
                "lifted @" + (down + 21));
        // A cancel stops the look without a lift.
        send(DOWN, 0, 1, 600, 300);
        send(CANCEL, 0, 1, 600, 300);
        expect("start 600 @" + (down + 22), "end");
        timed = false;

        // Pure view CANCEL must stop the actual camera even when its original pointer is absent.
        send(DOWN, 0, 7, 700, 300);
        send(CANCEL, 0, 9, 800, 320);
        expect("start", "end");
        send(MOVE, 0, 9, 820, 320);
        expect(); // no orphan camera movement after cancellation

        System.out.println(
                "PASS: one finger looks (every sample with its time; a lift apart from a cancel),"
                    + " two pinch by the ratio of their distance, a lifted finger leaves the camera"
                    + " still, a stick thumb plus a finger elsewhere is a look");
    }
}
