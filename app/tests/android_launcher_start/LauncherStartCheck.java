package org.stormengine.port;

/** The launcher's first moments: a tap before its UI exists. */
public final class LauncherStartCheck {
    public static void main(String[] args) {
        // Touches made while the UI runtime starts stay queued for it, in order, and reach it once
        // it exists.
        PointerQueue waiting = new PointerQueue(), taken = new PointerQueue();
        waiting.add(0, 7, 640f, 408f, 1000L); // DOWN
        waiting.add(1, 7, 642f, 409f, 1010L); // MOVE
        waiting.add(2, 7, 641f, 409f, 1050L); // UP
        assert !waiting.takeInto(taken, false) && taken.size == 0 && waiting.size == 3
                : "no runtime yet: the tap is kept";
        waiting.add(0, 8, 10f, 10f, 2000L);
        assert waiting.takeInto(taken, true) && waiting.size == 0 && taken.size == 4
                : "the runtime is there: every sample goes";
        assert taken.phase[0] == 0
                        && taken.phase[2] == 2
                        && taken.time[1] == 1010L
                        && taken.pointer[3] == 8
                : "oldest first, with their times";
        for (int i = 0; i < 40; i++) waiting.add(1, 1, i, i, i);
        assert waiting.size == 40
                        && waiting.takeInto(taken, true)
                        && taken.size == 40
                        && taken.x[39] == 39f
                : "the queue grows";
        System.out.println("launcher start: a tap before the UI exists is kept for it");
    }
}
