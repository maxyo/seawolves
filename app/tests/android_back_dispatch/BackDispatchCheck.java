package org.stormengine.port;

import static org.stormengine.port.BackDispatch.Route.*;

/** BackDispatch: which Back key halves navigate, and where one Back goes. */
public final class BackDispatchCheck {
    static final int DEVICE = 7;

    static boolean down(BackDispatch back, long time) {
        return back.key(true, false, time, DEVICE, 0, false);
    }

    static boolean up(BackDispatch back, long time) {
        return back.key(false, true, time, DEVICE, 0, false);
    }

    static BackDispatch.Route route(String screen, boolean wantsBack, boolean active) {
        return BackDispatch.route(false, () -> false, wantsBack, active, screen);
    }

    public static void main(String[] args) {
        BackDispatch back = new BackDispatch();
        assert !down(back, 10) : "DOWN never navigates";
        assert up(back, 10) : "the matching UP navigates once";
        assert !up(back, 10) : "a duplicate UP does not navigate again";

        assert !down(back, 40) && !back.key(false, true, 40, DEVICE, 0, true)
                : "a cancelled UP is inert";
        assert !back.key(true, false, 50, DEVICE, 0, true) && !up(back, 50)
                : "a cancelled DOWN starts no press";
        assert !up(back, 60) : "an orphan UP is inert";
        assert !back.key(true, false, 70, DEVICE, 1, false) && !up(back, 70)
                : "a repeated DOWN alone starts no press";
        down(back, 80);
        for (int repeat = 1; repeat <= 5; repeat++)
            assert !back.key(true, false, 80, DEVICE, repeat, false);
        assert up(back, 80) && !up(back, 80) : "a held key navigates once on release";
        down(back, 90);
        back.key(true, false, 90, DEVICE, 1, true);
        assert !up(back, 90) : "a cancelled repeat cancels the press";
        down(back, 100);
        assert !up(back, 101) : "the release must match the down time";
        down(back, 110);
        assert !back.key(false, true, 110, DEVICE + 1, 0, false)
                : "the release must match the device";
        down(back, 120);
        assert !back.key(false, false, 120, DEVICE, 0, false) && !up(back, 120)
                : "another action (MULTIPLE) drops the press";
        down(back, 130);
        back.cancel();
        assert !up(back, 130) : "a focus change or pause cancels the held key";

        // Where one Back goes.
        assert BackDispatch.route(
                                true,
                                () -> {
                                    throw new AssertionError(
                                            "the menu is not asked during a blocking load");
                                },
                                true,
                                true,
                                "character")
                        == CANCEL_LOAD
                : "a blocking load owns Back first";
        assert BackDispatch.route(false, () -> true, true, true, "map") == HELD_BY_MENU
                : "the closing menu holds Back";
        for (String hero : new String[] {"lscchar_all", "character"}) {
            assert route(hero, true, true) == UI : "a popup on Hero takes Back";
            assert route(hero, false, true) == SCREEN
                    : "Hero without a popup sends the stock back, not a UI request";
        }
        assert route("store", false, true) == UI : "another open interface takes Back in the UI";
        assert route("store", false, false) == SCREEN
                : "an interface the UI does not draw gets the stock back";
        assert route("", false, true) == MAIN_MENU
                : "a stale interface-active flag does not make gameplay a screen";
        assert route("", false, false) == MAIN_MENU : "gameplay opens the main menu";
        assert route("", true, false) == UI : "a modal takes Back in gameplay too";
        System.out.println(
                "PASS: BACK dispatch: single delivery on a matching release, cancellation, repeats, device/time, focus/pause reset; routes: load, held menu, UI modal, Hero stock back, screens, gameplay");
    }
}
