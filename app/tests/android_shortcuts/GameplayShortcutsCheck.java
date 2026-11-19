package org.stormengine.port;

public final class GameplayShortcutsCheck {
    public static void main(String[] ignored) throws Exception {
        GameplayShortcuts queue = new GameplayShortcuts();
        assert !queue.offer("pause", 1);
        assert !queue.offer("LaunchGameMenu", 1);
        assert queue.offer("mainmenu", 100);
        assert queue.poll(5000, false) == null;
        assert queue.offer("mainmenu", 6000);
        assert !queue.offer("character", 6000);
        assert "shortcut:mainmenu".equals(queue.poll(10000, true));
        assert queue.poll(10001, true) == null;
        assert queue.offer("character", 10001);
        assert "shortcut:character".equals(queue.poll(10002, true));
        assert queue.poll(10002, true) == null;
        assert !queue.offer("ship", 10003); // only the screen openings of the HUD's taps
        assert queue.offer("characterscreen", 10003);
        assert "shortcut:characterscreen".equals(queue.poll(10004, true));
        assert queue.offer("shipscreen", 10005);
        assert "shortcut:shipscreen".equals(queue.poll(10006, true));
        // An event line of the HUD's log opens the journal (a quest) or the inventory (an item).
        assert queue.offer("journalscreen", 10007);
        assert "shortcut:journalscreen".equals(queue.poll(10008, true));
        assert queue.offer("itemsscreen", 10009);
        assert "shortcut:itemsscreen".equals(queue.poll(10010, true));
        queue.offer("mainmenu", 0);
        assert queue.poll(GameplayShortcuts.LIFETIME_MS, true) == null;
        queue.offer("character", 0);
        queue.clear();
        assert queue.poll(1, true) == null;
        // Real cross-thread handoff, bounded duplicate queue, no Android mocks.
        Thread ui =
                new Thread(
                        () -> {
                            for (int n = 0; n < 10000; n++) queue.offer("mainmenu", 0);
                        });
        ui.start();
        ui.join();
        assert "shortcut:mainmenu".equals(queue.poll(10, true));
        assert queue.poll(10, true) == null;
        System.out.println(
                "PASS: bounded focus-ready shortcut queue, expiry, ordering, single delivery");
    }
}
