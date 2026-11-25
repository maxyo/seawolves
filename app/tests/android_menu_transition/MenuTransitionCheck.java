package org.stormengine.port;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * MenuTransition on a virtual scheduler: the 150 ms gap of a closing menu, the touches held during it, a new menu
 * superseding it, detach, and the loading shield.
 */
public final class MenuTransitionCheck {
    /** A recording HUD with a virtual clock; touches are ints (their action), copies are new boxes. */
    static final class Hud implements MenuTransition.Host<Integer> {
        final MenuTransition<Integer> gate = new MenuTransition<>(this);
        final List<String> shown = new ArrayList<>();
        final List<Integer> dispatched = new ArrayList<>();
        int gaps, recycled;
        long now, deadline = -1;

        public void startGap() {
            gaps++;
            deadline = now + MenuTransition.GAP_MS;
        }

        public void stopGap() {
            deadline = -1;
        }

        public void apply(String screen) {
            shown.add(screen.isEmpty() ? "gameplay" : screen);
        }

        public boolean dispatch(Integer touch) {
            dispatched.add(touch);
            return true;
        }

        public Integer copy(Integer touch) {
            return new Integer(touch);
        }

        public void recycle(Integer touch) {
            recycled++;
        }

        void advance(long millis) {
            now += millis;
            if (deadline >= 0 && now >= deadline) {
                deadline = -1;
                gate.commit();
            }
        }
    }

    static final int DOWN = 0, UP = 1, MOVE = 2;

    static Hud ready(String screen) {
        Hud hud = new Hud();
        hud.gate.show(screen);
        return hud;
    }

    public static void main(String[] args) {
        Hud hud = ready("game_menu");
        hud.gate.show("");
        assert hud.gate.pending() && hud.gate.screen().equals("game_menu") && hud.gaps == 1
                : "an empty screen after a menu starts the gap; the menu keeps the screen";
        assert hud.gate.holdsScreen() : "the HUD keeps what it shows";
        for (int action : new int[] {MOVE, UP, DOWN, UP}) assert hud.gate.touch(action);
        assert hud.dispatched.isEmpty() : "the closing menu gets no touch";
        hud.advance(100);
        hud.gate.show("");
        assert hud.gaps == 1 : "a repeated empty does not extend the deadline";
        hud.advance(49);
        assert hud.gate.pending() && hud.shown.equals(Arrays.asList("game_menu"));
        hud.advance(1);
        assert !hud.gate.pending()
                && hud.gate.screen().isEmpty()
                && hud.shown.get(hud.shown.size() - 1).equals("gameplay");
        assert hud.dispatched.equals(Arrays.asList(MOVE, UP, DOWN, UP)) && hud.recycled == 4
                : "touches of the gap reach what follows it, in order, and their copies are recycled";

        // A new menu supersedes the gap at once, without gameplay between them.
        hud = ready("game_menu");
        hud.gate.show("");
        hud.advance(64);
        hud.gate.show("option_screen");
        assert !hud.gate.pending()
                && hud.deadline < 0
                && hud.shown.equals(Arrays.asList("game_menu", "option_screen"));
        hud.gate.commit(); // a stale callback
        hud.advance(300);
        assert hud.gate.screen().equals("option_screen") && hud.shown.size() == 2;
        assert hud.gate.touch(DOWN) && hud.dispatched.equals(Arrays.asList(DOWN))
                : "the new menu's touches are dispatched";
        hud.gate.show("");
        hud.advance(20);
        hud.gate.show("option_screen");
        assert !hud.gate.pending() && hud.shown.size() == 3
                : "returning to the same screen is a new opening";
        hud = ready("");
        hud.gate.show("");
        assert !hud.gate.pending() && hud.gaps == 0 : "gameplay to gameplay is no gap";

        // Detach: what was held is recycled, not dispatched; the screen is committed.
        hud = ready("game_menu");
        hud.gate.show("");
        hud.gate.touch(DOWN);
        hud.gate.detached();
        assert !hud.gate.pending()
                && hud.gate.screen().isEmpty()
                && hud.deadline < 0
                && hud.dispatched.isEmpty()
                && hud.recycled == 1;

        // The input goes off in the gap: what was held is recycled, not given to what follows; the
        // gap goes on.
        hud = ready("game_menu");
        hud.gate.show("");
        hud.gate.touch(DOWN);
        hud.gate.touch(UP);
        hud.gate.dropHeld();
        hud.advance(MenuTransition.GAP_MS);
        assert hud.gate.screen().isEmpty() && hud.dispatched.isEmpty() && hud.recycled == 2
                : "a tap held when the input went off reaches nothing after it";

        // A blocking load takes precedence over the gap and takes every touch.
        hud = ready("game_menu");
        hud.gate.show("");
        hud.gate.touch(DOWN);
        LocationReadiness load = new LocationReadiness();
        assert hud.gate.readiness(
                load.accept(
                        3, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE));
        assert hud.gate.blocks()
                        && !hud.gate.pending()
                        && !hud.gate.holdsScreen()
                        && hud.deadline < 0
                        && hud.recycled == 1
                        && hud.dispatched.isEmpty()
                : "the load ends the gap and drops its held touches";
        for (int action : new int[] {DOWN, MOVE, UP}) assert hud.gate.touch(action);
        assert hud.dispatched.isEmpty() : "the shield takes every touch";
        assert !hud.gate.readiness(
                        load.accept(
                                3,
                                LocationReadiness.State.PREPARING,
                                true,
                                LocationReadiness.Ack.NONE))
                : "the same readiness again changes nothing";
        assert hud.gate.readiness(
                                load.accept(
                                        3,
                                        LocationReadiness.State.ERROR,
                                        true,
                                        LocationReadiness.Ack.NONE))
                        && hud.gate.blocks()
                : "an error keeps the shield";
        assert hud.gate.readiness(
                                load.accept(
                                        3,
                                        LocationReadiness.State.CANCELLED,
                                        false,
                                        LocationReadiness.Ack.CANCEL))
                        && hud.gate.blocks()
                : "an accepted cancel shields until the stock quit";
        hud.gate.readiness(
                load.accept(
                        4, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE));
        assert hud.gate.readiness(
                        load.accept(
                                4,
                                LocationReadiness.State.READY,
                                false,
                                LocationReadiness.Ack.NONE))
                && !hud.gate.blocks();
        assert hud.gate.touch(DOWN) && hud.dispatched.equals(Arrays.asList(DOWN))
                : "ordinary dispatch resumes after ready";
        System.out.println(
                "PASS: HUD screen gate: 150 ms gap, held touches delivered in order, supersession, detach, loading shield");
    }
}
