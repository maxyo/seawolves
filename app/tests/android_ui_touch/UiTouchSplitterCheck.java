package org.stormengine.port;

import android.view.MotionEvent;

import java.util.ArrayList;

/** The production splitter with a recording UI and recording views; only MotionEvent is a stand-in. */
public final class UiTouchSplitterCheck {
    static final ArrayList<String> ui = new ArrayList<>(), views = new ArrayList<>();
    static boolean viewsTake = true;
    static long clock = 100;
    static boolean timed; // the UI's log shows each sample's time
    // The UI wants the left half of the screen.
    static final UiTouchSplitter splitter =
            new UiTouchSplitter(
                    new UiTouchSplitter.Ui() {
                        public boolean wantsTouch(float x, float y) {
                            return x < 500;
                        }

                        public void touch(int phase, int pointer, float x, float y, long time) {
                            ui.add(
                                    new String[] {"down", "move", "up", "cancel"}[phase]
                                            + " "
                                            + pointer
                                            + " "
                                            + (int) x
                                            + ","
                                            + (int) y
                                            + (timed ? " @" + time : ""));
                        }
                    },
                    event -> {
                        StringBuilder line =
                                new StringBuilder(
                                        new String[] {
                                                    "DOWN", "UP", "MOVE", "CANCEL", "?", "PDOWN",
                                                    "PUP"
                                                }
                                                [event.getActionMasked()]);
                        if (event.getActionMasked() >= 5)
                            line.append('#').append(event.getPointerId(event.getActionIndex()));
                        for (int i = 0; i < event.getPointerCount(); i++)
                            line.append(' ')
                                    .append(event.getPointerId(i))
                                    .append('@')
                                    .append((int) event.getX(i))
                                    .append(',')
                                    .append((int) event.getY(i));
                        views.add(line.toString());
                        return viewsTake;
                    });

    /** pointers: id, x, y triples; `changed` = index of the pointer the action is about. */
    static boolean send(int action, int changed, int... pointers) {
        int count = pointers.length / 3;
        int[] ids = new int[count];
        float[] xs = new float[count], ys = new float[count];
        for (int i = 0; i < count; i++) {
            ids[i] = pointers[i * 3];
            xs[i] = pointers[i * 3 + 1];
            ys[i] = pointers[i * 3 + 2];
        }
        return splitter.dispatch(
                new MotionEvent(
                        100,
                        ++clock,
                        action | changed << MotionEvent.ACTION_POINTER_INDEX_SHIFT,
                        ids,
                        xs,
                        ys));
    }

    static void expect(ArrayList<String> log, String... lines) {
        if (!log.equals(java.util.Arrays.asList(lines)))
            throw new AssertionError(
                    "expected " + java.util.Arrays.toString(lines) + " but got " + log);
        log.clear();
    }

    public static void main(String[] args) {
        final int DOWN = MotionEvent.ACTION_DOWN,
                UP = MotionEvent.ACTION_UP,
                MOVE = MotionEvent.ACTION_MOVE,
                CANCEL = MotionEvent.ACTION_CANCEL,
                PDOWN = MotionEvent.ACTION_POINTER_DOWN,
                PUP = MotionEvent.ACTION_POINTER_UP;

        // One finger on the UI: the views see nothing, the gesture is consumed.
        assert send(DOWN, 0, 1, 100, 300);
        assert send(MOVE, 0, 1, 120, 280);
        assert send(UP, 0, 1, 120, 280);
        expect(ui, "down 1 100,300", "move 1 120,280", "up 1 120,280");
        expect(views);

        // One finger on the game: the event goes on untouched; the views' answer is ours.
        assert send(DOWN, 0, 1, 800, 300);
        assert send(MOVE, 0, 1, 700, 300);
        assert send(UP, 0, 1, 700, 300);
        expect(ui);
        expect(views, "DOWN 1@800,300", "MOVE 1@700,300", "UP 1@700,300");
        viewsTake = false;
        assert !send(DOWN, 0, 1, 800, 300) : "nobody wants the pointer: not consumed";
        viewsTake = true;
        views.clear();

        // Stick held (pointer 1, UI), then a look with a second finger (pointer 2, game).
        assert send(DOWN, 0, 1, 100, 300);
        assert send(PDOWN, 1, 1, 100, 300, 2, 800, 200);
        expect(views, "DOWN 2@800,200"); // the views see a gesture of their own
        assert send(MOVE, 0, 1, 110, 290, 2, 700, 210);
        expect(ui, "down 1 100,300", "move 1 110,290");
        expect(views, "MOVE 2@700,210");
        // A third finger presses a UI button while both are held.
        assert send(PDOWN, 2, 1, 110, 290, 2, 700, 210, 3, 300, 100);
        expect(ui, "down 3 300,100");
        expect(views, "MOVE 2@700,210");
        assert send(PUP, 2, 1, 110, 290, 2, 700, 210, 3, 300, 100);
        expect(ui, "up 3 300,100");
        expect(views, "MOVE 2@700,210");
        // The look ends first; the stick goes on.
        assert send(PUP, 1, 1, 110, 290, 2, 650, 210);
        expect(views, "UP 2@650,210");
        expect(ui);
        assert send(MOVE, 0, 1, 130, 270);
        assert send(UP, 0, 1, 130, 270);
        expect(ui, "move 1 130,270", "up 1 130,270");
        expect(views);

        // Look first (game), then the stick (UI), reordered indices, the stick released first.
        assert send(DOWN, 0, 5, 900, 100);
        assert send(PDOWN, 0, 4, 50, 350, 5, 900, 100);
        assert send(MOVE, 0, 4, 60, 340, 5, 880, 120);
        assert send(PUP, 0, 4, 60, 340, 5, 880, 120);
        assert send(MOVE, 0, 5, 860, 140);
        assert send(UP, 0, 5, 860, 140);
        expect(ui, "down 4 50,350", "move 4 60,340", "up 4 60,340");
        expect(
                views,
                "DOWN 5@900,100",
                "MOVE 5@900,100",
                "MOVE 5@880,120",
                "MOVE 5@880,120",
                "MOVE 5@860,140",
                "UP 5@860,140");

        // Two fingers on the game while the UI holds one: the views get pointer-down / pointer-up
        // of their own.
        assert send(DOWN, 0, 1, 100, 300);
        assert send(PDOWN, 1, 1, 100, 300, 2, 800, 200);
        assert send(PDOWN, 2, 1, 100, 300, 2, 800, 200, 3, 900, 250);
        assert send(PUP, 1, 1, 100, 300, 2, 800, 200, 3, 900, 250);
        assert send(PUP, 1, 1, 100, 300, 3, 900, 250);
        expect(
                views,
                "DOWN 2@800,200",
                "PDOWN#3 2@800,200 3@900,250",
                "PUP#2 2@800,200 3@900,250",
                "UP 3@900,250");
        // The system cancels: both sides hear it.
        assert send(PDOWN, 1, 1, 100, 300, 6, 800, 200);
        ui.clear();
        views.clear();
        assert send(CANCEL, 0, 1, 100, 300, 6, 800, 200);
        expect(ui, "cancel 1 100,300");
        expect(views, "CANCEL 6@800,200");

        // A new gesture that starts while the UI still thinks it holds a pointer cancels the stale
        // one.
        assert send(DOWN, 0, 1, 100, 300);
        ui.clear();
        assert send(DOWN, 0, 1, 800, 300);
        expect(ui, "cancel 1 100,300");
        expect(views, "DOWN 1@800,300");
        // Lost UP of a UI pointer, followed by a fresh gesture with a different ID.
        assert send(DOWN, 0, 7, 100, 300);
        assert send(MOVE, 0, 7, 145, 275);
        ui.clear();
        views.clear();
        assert send(DOWN, 0, 9, 850, 320);
        expect(ui, "cancel 7 145,275");
        expect(views, "DOWN 9@850,320");
        assert send(UP, 0, 9, 855, 325);
        expect(ui);
        expect(views, "UP 9@855,325");

        // Pure view stream forwards CANCEL unchanged; CameraGesture cancels all IDs.
        assert send(DOWN, 0, 7, 850, 300);
        assert send(MOVE, 0, 7, 875, 275);
        ui.clear();
        views.clear();
        assert send(CANCEL, 0, 9, 900, 320);
        expect(views, "CANCEL 9@900,320");
        expect(ui);

        // A malformed mixed MOVE omits the view pointer: terminate its old stream once.
        assert send(DOWN, 0, 1, 100, 300);
        assert send(PDOWN, 1, 1, 100, 300, 7, 850, 275);
        ui.clear();
        views.clear();
        assert send(MOVE, 0, 1, 110, 290);
        expect(views, "CANCEL 7@850,275");
        expect(ui, "move 1 110,290");
        assert send(CANCEL, 0, 1, 110, 290);
        expect(views);
        expect(ui, "cancel 1 110,290");

        // System CANCEL may also omit the view pointer; its retained sample still terminates it.
        assert send(DOWN, 0, 1, 100, 300);
        assert send(PDOWN, 1, 1, 100, 300, 7, 860, 280);
        ui.clear();
        views.clear();
        assert send(CANCEL, 0, 1, 100, 300);
        expect(views, "CANCEL 7@860,280");
        expect(ui, "cancel 1 100,300");

        // A view-owned pointer also needs its terminal event before a new UI gesture.
        assert send(DOWN, 0, 7, 850, 300);
        assert send(MOVE, 0, 7, 875, 275);
        ui.clear();
        views.clear();
        assert send(DOWN, 0, 9, 100, 320);
        expect(views, "CANCEL 7@875,275");
        expect(ui, "down 9 100,320");
        assert send(UP, 0, 9, 100, 320);
        expect(views);
        expect(ui, "up 9 100,320");

        // Loading owns the whole screen immediately: both old owners receive exactly one
        // terminal event at their retained coordinates, even without a physical CANCEL.
        assert send(DOWN, 0, 3, 120, 250);
        assert send(PDOWN, 1, 3, 120, 250, 7, 830, 280);
        ui.clear();
        views.clear();
        MotionEvent barrier =
                new MotionEvent(
                        100, ++clock, CANCEL, new int[] {0}, new float[] {0}, new float[] {0});
        splitter.cancelOwners(barrier);
        splitter.cancelOwners(barrier);
        expect(ui, "cancel 3 120,250");
        expect(views, "CANCEL 7@830,280");
        assert send(DOWN, 0, 9, 130, 200);
        expect(ui, "down 9 130,200");
        expect(views);
        assert send(UP, 0, 9, 130, 200);
        ui.clear();
        views.clear();

        // A batched move: every sample reaches the UI, oldest first, each with its own time;
        // the views' pointer gets the event as it came.
        timed = true;
        ui.clear();
        views.clear();
        assert send(DOWN, 0, 1, 100, 300);
        final long down = clock;
        assert splitter.dispatch(
                new MotionEvent(
                                100,
                                down + 16,
                                MOVE,
                                new int[] {1},
                                new float[] {100},
                                new float[] {240})
                        .withHistory(
                                new long[] {down + 4, down + 10},
                                new float[][] {{100}, {100}},
                                new float[][] {{290}, {270}}));
        clock = down + 16;
        assert send(UP, 0, 1, 100, 240);
        expect(
                ui,
                "down 1 100,300 @" + down,
                "move 1 100,290 @" + (down + 4),
                "move 1 100,270 @" + (down + 10),
                "move 1 100,240 @" + (down + 16),
                "up 1 100,240 @" + clock);
        expect(views);
        timed = false;
        System.out.println("UI touch splitter checks passed");
    }
}
