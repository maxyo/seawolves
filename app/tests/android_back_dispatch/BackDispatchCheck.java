package org.stormengine.port;

import java.lang.reflect.Field;

/** Recording Android/SDL boundary; run.py supplies the actual production bodies. */
public final class BackDispatchCheck {
    static void set(GameActivity game, String name, Object value) throws Exception {
        Field field = GameActivity.class.getDeclaredField(name);
        field.setAccessible(true);
        field.set(game, value);
    }

    static GameActivity ready(String screen, MobileHud hud) throws Exception {
        GameActivity game = new GameActivity();
        set(game, "screen", screen);
        set(game, "hud", hud);
        set(game, "resumed", true);
        game.onWindowFocusChanged(true);
        return game;
    }

    static KeyEvent event(int action, long time, int repeat, boolean canceled) {
        return new KeyEvent(
                action,
                KeyEvent.KEYCODE_BACK,
                InputDevice.SOURCE_KEYBOARD,
                7,
                time,
                repeat,
                canceled,
                null);
    }

    static void down(GameActivity game, long time) {
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_DOWN, time, 0, false));
    }

    static void up(GameActivity game, long time) {
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_UP, time, 0, false));
    }

    public static void main(String[] args) throws Exception {
        MobileHud hud = new MobileHud();
        hud.modal = true;
        GameActivity game = ready("map", hud);
        down(game, 10);
        assert hud.dismissCalls == 0 && game.nextAction() == null : "DOWN cannot navigate";
        up(game, 10);
        assert hud.dismissCalls == 1 && game.nextAction() == null
                : "a held menu transition consumes BACK";
        up(game, 10);
        assert hud.dismissCalls == 1 : "duplicate UP cannot close underlying screen";
        down(game, 20);
        up(game, 20);
        assert "back".equals(game.nextAction()) && game.nextAction() == null;
        assert game.sdlDispatches == 0 : "neither BACK half may reach SDL Escape";

        // Exercise the actual backToUi body; older checks replaced it with an unconditional false.
        for (String hero : new String[] {"lscchar_all", "character"}) {
            game = ready(hero, new MobileHud());
            Field uiField = GameActivity.class.getDeclaredField("ui");
            uiField.setAccessible(true);
            EngineUi ui = (EngineUi) uiField.get(game);
            ui.active = true;
            ui.modal = true;
            down(game, 21);
            up(game, 21);
            up(game, 21);
            assert ui.backs == 1 && game.nextAction() == null
                    : "Hero popup consumes one Back without exiting hero";
            ui.modal = false; // a subsequent native frame publishes that the popup has closed
            down(game, 22);
            up(game, 22);
            up(game, 22);
            assert "back".equals(game.nextAction()) && game.nextAction() == null && ui.backs == 1
                    : "Hero without popup sends one stock back like its arrow, not an"
                            + " unacknowledged native request";
            ui.modal = true;
            game.onBackPressed();
            assert ui.backs == 2 && game.nextAction() == null
                    : "system callback also preserves popup-first routing";
            ui.modal = false;
            game.onBackPressed();
            // A screen transition invalidates queued actions (same purge used by showMobileScreen).
            java.lang.reflect.Method purge =
                    GameActivity.class.getDeclaredMethod("dropQueuedActions");
            purge.setAccessible(true);
            purge.invoke(game);
            set(game, "screen", "store");
            assert game.nextAction() == null : "Hero back cannot survive a screen transition";
            game.onBackPressed();
            assert ui.backs == 3 && game.nextAction() == null
                    : "other native interfaces retain their original route";
            set(game, "screen", "");
            ui.active = true; // old interface-active snapshot while host is already gameplay
            game.onBackPressed();
            assert "shortcut:mainmenu".equals(game.nextAction()) && ui.backs == 3
                    : "stale active flag cannot infer Hero";
        }

        // Real readiness controller and actual locationLoadAction: loading owns Back before
        // Hero/menu.
        game = ready("character", new MobileHud());
        Field loadField = GameActivity.class.getDeclaredField("locationReadiness");
        loadField.setAccessible(true);
        LocationReadiness load = (LocationReadiness) loadField.get(game);
        load.accept(7, "preparing", true, false, "");
        down(game, 23);
        up(game, 23);
        up(game, 23);
        assert "location-load:7\tcancel".equals(game.nextAction()) && game.nextAction() == null;
        load.accept(7, "cancelled", false, false, "cancel");
        game.onBackPressed();
        assert game.nextAction() == null
                : "cancel acknowledgement cannot fall through into Hero/gameplay";
        load.accept(8, "ready", false, false, "");
        game.onBackPressed();
        assert "back".equals(game.nextAction()) && game.nextAction() == null
                : "terminal ready restores normal Hero routing";

        game = ready("", new MobileHud());
        down(game, 30);
        up(game, 30);
        assert "shortcut:mainmenu".equals(game.nextAction()) && game.nextAction() == null;
        assert game.inputReleases == 1 : "real gameplay route releases held input";
        game.onBackPressed();
        assert "shortcut:mainmenu".equals(game.nextAction()) : "system callback retains route";

        hud = new MobileHud();
        game = ready("inventory", hud);
        down(game, 40);
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_UP, 40, 0, true));
        assert hud.dismissCalls == 0 && game.nextAction() == null : "cancelled UP is inert";
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_DOWN, 50, 0, true));
        up(game, 50);
        assert hud.dismissCalls == 0 : "cancelled DOWN cannot start a press";
        up(game, 60);
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_DOWN, 70, 1, false));
        up(game, 70);
        assert hud.dismissCalls == 0 : "orphan UP/repeated DOWN cannot start a press";
        down(game, 80);
        for (int repeat = 1; repeat <= 5; repeat++)
            assert game.dispatchKeyEvent(event(KeyEvent.ACTION_DOWN, 80, repeat, false));
        assert hud.dismissCalls == 0;
        up(game, 80);
        up(game, 80);
        assert hud.dismissCalls == 1
                && "back".equals(game.nextAction())
                && game.nextAction() == null;
        down(game, 90);
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_DOWN, 90, 1, true));
        up(game, 90);
        assert hud.dismissCalls == 1 : "canceled repeat cancels the press";
        down(game, 100);
        up(game, 101);
        down(game, 110);
        KeyEvent wrongDevice = event(KeyEvent.ACTION_UP, 110, 0, false);
        wrongDevice.deviceId++;
        assert game.dispatchKeyEvent(wrongDevice);
        assert hud.dismissCalls == 1 : "release must match time and device";
        down(game, 120);
        assert game.dispatchKeyEvent(event(KeyEvent.ACTION_MULTIPLE, 120, 0, false));
        up(game, 120);
        assert hud.dismissCalls == 1 : "unsupported BACK actions cannot leak to SDL";

        down(game, 130);
        game.onWindowFocusChanged(false);
        game.onWindowFocusChanged(true);
        up(game, 130);
        assert hud.dismissCalls == 1 : "dialog/window transition cancels held BACK";
        down(game, 140);
        game.onPause();
        up(game, 140);
        assert hud.dismissCalls == 1 : "background transition cancels held BACK";
        assert game.sdlDispatches == 0;

        game = ready("map", new MobileHud());
        int[] mouseSources = {
            InputDevice.SOURCE_MOUSE, InputDevice.SOURCE_MOUSE | InputDevice.SOURCE_KEYBOARD
        };
        for (int source : mouseSources)
            for (int action : new int[] {KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP}) {
                KeyEvent mouse = event(action, 150, 0, false);
                mouse.source = source;
                assert !game.dispatchKeyEvent(mouse) && game.lastSdlEvent == mouse;
            }
        KeyEvent unknownMouse = event(KeyEvent.ACTION_DOWN, 160, 0, false);
        unknownMouse.source = InputDevice.SOURCE_UNKNOWN;
        unknownMouse.device = new InputDevice(InputDevice.SOURCE_MOUSE);
        assert !game.dispatchKeyEvent(unknownMouse) && game.lastSdlEvent == unknownMouse;
        assert game.sdlDispatches == 5 && game.nextAction() == null : "mouse BACK keeps SDL route";
        KeyEvent unrelated = event(KeyEvent.ACTION_DOWN, 170, 0, false);
        unrelated.keyCode = 24;
        assert !game.dispatchKeyEvent(unrelated) && game.lastSdlEvent == unrelated;
        KeyEvent unknownKeyboard = event(KeyEvent.ACTION_DOWN, 180, 0, false);
        unknownKeyboard.source = InputDevice.SOURCE_UNKNOWN;
        assert game.dispatchKeyEvent(unknownKeyboard);
        up(game, 180);
        assert "back".equals(game.nextAction()) : "source-less system BACK still navigates";
        game.mBrokenLibraries = true;
        KeyEvent broken = event(KeyEvent.ACTION_DOWN, 190, 0, false);
        assert !game.dispatchKeyEvent(broken) && game.lastSdlEvent == broken;
        System.out.println(
                "PASS: production BACK dispatch: modal/screen/gameplay routes, single delivery,"
                    + " cancellation, repeats, paired device/time, focus/pause reset, mouse/source"
                    + " fallback, unrelated keys, SDL isolation");
    }
}

class RecordingSDL {
    boolean mBrokenLibraries;
    final Object mLayout = new Object();
    int sdlDispatches;
    KeyEvent lastSdlEvent;

    public boolean dispatchKeyEvent(KeyEvent event) {
        sdlDispatches++;
        lastSdlEvent = event;
        return false;
    }

    public void onBackPressed() {
        throw new AssertionError("default Android BACK must not exit game");
    }

    public void onWindowFocusChanged(boolean focus) {}

    protected void onPause() {}
}

class KeyEvent {
    static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MULTIPLE = 2, KEYCODE_BACK = 4;
    int action, keyCode, source, deviceId, repeat;
    long downTime;
    boolean canceled;
    InputDevice device;

    KeyEvent(
            int action,
            int keyCode,
            int source,
            int deviceId,
            long downTime,
            int repeat,
            boolean canceled,
            InputDevice device) {
        this.action = action;
        this.keyCode = keyCode;
        this.source = source;
        this.deviceId = deviceId;
        this.downTime = downTime;
        this.repeat = repeat;
        this.canceled = canceled;
        this.device = device;
    }

    int getAction() {
        return action;
    }

    int getKeyCode() {
        return keyCode;
    }

    int getSource() {
        return source;
    }

    int getDeviceId() {
        return deviceId;
    }

    int getRepeatCount() {
        return repeat;
    }

    long getDownTime() {
        return downTime;
    }

    boolean isCanceled() {
        return canceled;
    }

    InputDevice getDevice() {
        return device;
    }
}

class InputDevice {
    static final int SOURCE_UNKNOWN = 0, SOURCE_KEYBOARD = 0x101, SOURCE_MOUSE = 0x2002;
    final int source;

    InputDevice(int source) {
        this.source = source;
    }

    int getSources() {
        return source;
    }
}

class MobileHud {
    boolean modal;
    int dismissCalls;

    boolean menuTransitionPending() {
        dismissCalls++;
        boolean result = modal;
        modal = false;
        return result;
    }
}

class android {
    static class util {
        static class Log {
            static void i(String tag, String message) {}

            static void e(String tag, String message) {
                throw new AssertionError(message);
            }
        }
    }

    static class os {
        static class SystemClock {
            static long uptimeMillis() {
                return 1000;
            }
        }
    }
}

class EngineUi {
    boolean active, modal;
    int backs;

    boolean interfaceActive() {
        return active;
    }

    boolean wantsBack() {
        return modal;
    }

    void back() {
        backs++;
    }
}
