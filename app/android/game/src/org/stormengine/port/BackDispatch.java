package org.stormengine.port;

import java.util.function.BooleanSupplier;

/**
 * Back on the game's screen: which half of a Back key press navigates (GameActivity.dispatchKeyEvent owns both halves
 * before SDL sees them as Escape), and where one Back goes (GameActivity.onBackPressed).
 */
final class BackDispatch {
    /** Where one Back goes, the first that takes it. */
    enum Route {
        /** A blocking location load: its cancel. */
        CANCEL_LOAD,
        /** The closing menu still holds the screen (MobileHud.menuTransitionPending): nothing. */
        HELD_BY_MENU,
        /** The portable UI: a modal, or the open interface it draws. */
        UI,
        /** The stock "back" command of the open interface. */
        SCREEN,
        /** Gameplay: the main menu shortcut. */
        MAIN_MENU
    }

    private long downTime = -1;
    private int deviceId;

    /** The press in progress is dropped (focus lost, pause). */
    void cancel() {
        downTime = -1;
    }

    /**
     * A Back key event (a keyboard's or the system's, not a mouse's); true when it completes a press: an uncancelled
     * up of the same down time and device as the press's first, unrepeated down. Any other action drops the press.
     */
    boolean key(
            boolean down,
            boolean up,
            long eventDownTime,
            int device,
            int repeat,
            boolean canceled) {
        if (down) {
            if (canceled) downTime = -1;
            else if (repeat == 0) {
                downTime = eventDownTime;
                deviceId = device;
            }
            return false;
        }
        boolean pressed = up && downTime == eventDownTime && deviceId == device && !canceled;
        downTime = -1;
        return pressed;
    }

    /**
     * Where one Back goes. `screen` is the open interface ("" in gameplay); the engine's Hero interfaces
     * ("lscchar_all", "character") take the same stock back as their header arrow, never a request to the UI as well
     * (a delayed one could close twice). menuTransitionPending is asked only when no load blocks.
     */
    static Route route(
            boolean loadBlocks,
            BooleanSupplier menuTransitionPending,
            boolean uiWantsBack,
            boolean uiInterfaceActive,
            String screen) {
        if (loadBlocks) return Route.CANCEL_LOAD;
        if (menuTransitionPending.getAsBoolean()) return Route.HELD_BY_MENU;
        if (uiWantsBack) return Route.UI;
        boolean hero = screen.equals("lscchar_all") || screen.equals("character");
        if (!hero && !screen.isEmpty() && uiInterfaceActive) return Route.UI;
        return screen.isEmpty() ? Route.MAIN_MENU : Route.SCREEN;
    }
}
