package org.stormengine.port;

import java.util.Arrays;
import java.util.TreeSet;

/** The real Java shortcut queue takes exactly the shortcuts the engine's HUD hands over (the arguments). */
public final class ShortcutContractCheck {
    public static void main(String[] engine) {
        TreeSet<String> declared = new TreeSet<>(Arrays.asList(engine)),
                accepted = new TreeSet<>(Arrays.asList(GameplayShortcuts.ACCEPTED));
        if (!declared.equals(accepted))
            throw new AssertionError(
                    "GameplayShortcuts.ACCEPTED "
                            + accepted
                            + " differs from the engine's shortcuts "
                            + declared);
        for (String name : engine)
            if (!new GameplayShortcuts().offer(name, 0))
                throw new AssertionError("GameplayShortcuts refuses the engine's shortcut " + name);
        if (new GameplayShortcuts().offer("not-a-shortcut", 0))
            throw new AssertionError("GameplayShortcuts takes an unknown shortcut");
        System.out.println(
                "PASS: GameplayShortcuts takes exactly the engine's shortcuts " + declared);
    }
}
