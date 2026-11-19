package org.stormengine.port;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

class RecordingUi {
    private final ArrayDeque<Runnable> posted = new ArrayDeque<>();

    void runOnUiThread(Runnable task) {
        posted.add(task);
    }

    void next() {
        posted.remove().run();
    }

    void drain() {
        while (!posted.isEmpty()) next();
    }
}

class MobileHud {
    final List<String> screens = new ArrayList<>();

    void showScreen(String name) {
        screens.add(name);
    }
}

// Interface models go from the engine to its UI host in-process (engine src/libs/ui_host):
// no snapshot crosses this activity. What is left here: the screen callbacks keep their FIFO
// order and a screen change drops the input made for the old one.
public final class ScreenCallbacksCheck {
    private static void screenChange() {
        GameActivity game = new GameActivity();
        game.seedOldInput();
        game.showMobileScreen("options");
        assert game.currentScreen().isEmpty();
        game.next();
        assert game.currentScreen().equals("options");
        assert game.views().screens.equals(Arrays.asList("options"));
        assert game.oldInputCleared();
        game.showMobileScreen("");
        game.drain();
        assert game.currentScreen().isEmpty() && game.commands().isEmpty();
    }

    private static void rapidTransitions() {
        GameActivity game = new GameActivity();
        // The engine's callbacks enqueue FIFO, but Android may drain them later.
        game.showMobileScreen("A");
        game.showMobileScreen("B");
        game.next();
        assert game.currentScreen().equals("A");
        game.next();
        assert game.currentScreen().equals("B");
        game.showMobileScreen("A");
        game.drain();
        assert game.currentScreen().equals("A");
        assert game.views().screens.equals(Arrays.asList("A", "B", "A"));
    }

    public static void main(String[] args) {
        screenChange();
        rapidTransitions();
        System.out.println(
                "PASS GameActivity screen callbacks: FIFO A/B/A transitions, old input dropped on a"
                        + " screen change");
    }
}
