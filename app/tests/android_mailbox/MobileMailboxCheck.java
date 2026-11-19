package org.stormengine.port;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class MobileMailboxCheck {
    private static List<String> drain(MobileMailbox box) {
        final List<String> out = new ArrayList<>();
        for (String m; (m = box.poll()) != null; ) out.add(m);
        return out;
    }

    private static void expect(List<String> got, String... want) {
        if (!got.equals(Arrays.asList(want)))
            throw new AssertionError("got " + got + ", want " + Arrays.asList(want));
    }

    public static void main(String[] queuedLiterals) {
        // Every message the application queues has a kind (the literal starts are passed by
        // run.py).
        for (String literal : queuedLiterals) MobileMailbox.kindOf(literal);
        try {
            MobileMailbox.kindOf("nobody:knows");
            throw new AssertionError("unclassified message accepted");
        } catch (IllegalArgumentException expected) {
        }

        // The reported bug: a short focus loss, the gain arrives while an interface opens, the
        // purge for the new screen runs. The gain survives; the screen's commands go.
        MobileMailbox box = new MobileMailbox();
        AudioFocusState focus = new AudioFocusState();
        box.offer(focus.onRequestResult(AudioFocusState.REQUEST_GRANTED));
        box.offer("camera:zoom\t1.2");
        box.offer(focus.onFocusChange(AudioFocusState.LOSS_TRANSIENT));
        box.offer("camera:look\t1");
        box.offer("graphics:reload");
        box.offer("back");
        box.offer(focus.onFocusChange(AudioFocusState.GAIN));
        box.offer("location-load:3\tretry");
        box.offer("graphics:reload"); // a true duplicate: one reload
        box.offer("hudowner:lost");
        box.purgeCommands();
        expect(drain(box), "audiofocus:gain", "graphics:reload", "hudowner:lost");

        // Latest value per key, in the order the latest values came.
        box.offer("audiofocus:duck");
        box.offer("graphics:reload");
        box.offer("audiofocus:loss");
        expect(drain(box), "graphics:reload", "audiofocus:loss");

        // The native loading pump consumes only the matching generation's cancel.
        box.offer("camera:zoom\t1.2");
        box.offer("location-load:41\tcancel");
        box.offer("audiofocus:gain");
        box.offer("location-load:42\tretry");
        box.offer("location-load:42\tcancel");
        box.offer("back");
        if (box.pollLocationLoadCancel(43)) throw new AssertionError("wrong generation consumed");
        if (!box.pollLocationLoadCancel(42)) throw new AssertionError("cancel missing");
        if (box.pollLocationLoadCancel(42)) throw new AssertionError("cancel consumed twice");
        expect(
                drain(box),
                "camera:zoom\t1.2",
                "location-load:41\tcancel",
                "audiofocus:gain",
                "location-load:42\tretry",
                "back");
        box.offer("location-load:18446744073709551615\tcancel");
        if (!box.pollLocationLoadCancel(-1L))
            throw new AssertionError("unsigned generation mismatch");
        expect(drain(box));

        box.offer("camera:zoom\t1.2");
        box.offer("back");
        box.offer("location-load:3\tretry");
        box.offer("audiofocus:gain");
        box.offer("graphics:reload");
        if (!box.loadingBoundary(50, true)) throw new AssertionError("loading entry missing");
        box.offer("camera:zoom\t1.2");
        box.offer("camera:look\t1");
        box.offer("back");
        box.offer("location-load:49\tcancel");
        box.offer("location-load:50\tcancel");
        if (box.loadingBoundary(50, true)) throw new AssertionError("same generation repurged");
        if (!box.pollLocationLoadCancel(50)) throw new AssertionError("active cancel lost");
        expect(drain(box), "audiofocus:gain", "graphics:reload");
        box.offer("location-load:50\tretry");
        box.offer("audiofocus:duck");
        box.loadingBoundary(50, false);
        expect(drain(box), "audiofocus:duck");
        box.offer("back");
        expect(drain(box), "back");

        // Commands are bounded and refused (the caller logs); state always fits, purges reset the
        // count.
        for (int i = 0; i < MobileMailbox.COMMAND_CAPACITY; ++i)
            if (!box.offer("camera:zoom\t" + i)) throw new AssertionError("refused " + i);
        if (box.offer("back"))
            throw new AssertionError("a command beyond the capacity was accepted");
        if (!box.offer("audiofocus:gain") || !box.offer("graphics:reload"))
            throw new AssertionError("state refused on a full mailbox");
        box.purgeCommands();
        expect(drain(box), "audiofocus:gain", "graphics:reload");
        for (int i = 0; i < MobileMailbox.COMMAND_CAPACITY; ++i) box.offer("back");
        for (int i = 0; i < 10; ++i) box.poll();
        for (int i = 0; i < 10; ++i)
            if (!box.offer("back")) throw new AssertionError("poll did not free a slot");
        if (box.offer("back")) throw new AssertionError("poll miscounted");
        System.out.println(
                "PASS mailbox: purges keep the latest audio focus, graphics:reload and HUD"
                        + " declarations, drop commands and true duplicates; "
                        + queuedLiterals.length
                        + " queued kinds classified");
    }
}
