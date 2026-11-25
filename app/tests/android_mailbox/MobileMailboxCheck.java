package org.stormengine.port;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class MobileMailboxCheck {
    private static List<String> drain(MobileMailbox box) {
        final List<String> out = new ArrayList<>();
        for (MobileMailbox.Message m; (m = box.poll()) != null; ) out.add(m.toString());
        return out;
    }

    private static final MobileMailbox.Message RELOAD = MobileMailbox.Message.GRAPHICS_RELOAD,
            HUD_LOST = MobileMailbox.Message.HUD_OWNER_LOST,
            BACK = MobileMailbox.Message.BACK,
            LOOK = MobileMailbox.Message.lookCancel();

    private static MobileMailbox.Message zoom(float scale) {
        return MobileMailbox.Message.cameraZoom(scale);
    }

    private static MobileMailbox.Message cancel(long generation) {
        return MobileMailbox.Message.locationLoadCancel(generation);
    }

    private static MobileMailbox.Message focus(AudioFocusState.Focus focus) {
        return MobileMailbox.Message.audioFocus(focus);
    }

    private static void expect(List<String> got, String... want) {
        if (!got.equals(Arrays.asList(want)))
            throw new AssertionError("got " + got + ", want " + Arrays.asList(want));
    }

    public static void main(String[] args) {
        // The reported bug: a short focus loss, the gain arrives while an interface opens, the
        // purge for the new screen runs. The gain survives; the screen's commands go.
        MobileMailbox box = new MobileMailbox();
        AudioFocusState focus = new AudioFocusState();
        box.offer(focus(focus.onRequestResult(AudioFocusState.REQUEST_GRANTED)));
        box.offer(zoom(1.2f));
        box.offer(focus(focus.onFocusChange(AudioFocusState.LOSS_TRANSIENT)));
        box.offer(LOOK);
        box.offer(RELOAD);
        box.offer(BACK);
        box.offer(focus(focus.onFocusChange(AudioFocusState.GAIN)));
        box.offer(cancel(3L));
        box.offer(RELOAD); // a true duplicate: one reload
        box.offer(HUD_LOST);
        box.purgeCommands();
        expect(drain(box), "audio focus gain", "graphics reload", "HUD owner lost");

        // Latest value per key, in the order the latest values came.
        box.offer(focus(AudioFocusState.Focus.DUCK));
        box.offer(RELOAD);
        box.offer(focus(AudioFocusState.Focus.LOSS));
        expect(drain(box), "graphics reload", "audio focus loss");

        // The native loading pump consumes only the matching generation's cancel.
        box.offer(zoom(1.2f));
        box.offer(cancel(41L));
        box.offer(focus(AudioFocusState.Focus.GAIN));
        box.offer(cancel(42L));
        box.offer(BACK);
        if (box.pollLocationLoadCancel(43)) throw new AssertionError("wrong generation consumed");
        if (!box.pollLocationLoadCancel(42)) throw new AssertionError("cancel missing");
        if (box.pollLocationLoadCancel(42)) throw new AssertionError("cancel consumed twice");
        expect(
                drain(box),
                "camera zoom 1.2",
                "location load cancel 41",
                "audio focus gain",
                "back");
        box.offer(cancel(-1L));
        if (!box.pollLocationLoadCancel(-1L))
            throw new AssertionError("unsigned generation mismatch");
        expect(drain(box));

        box.offer(zoom(1.2f));
        box.offer(BACK);
        box.offer(cancel(3L));
        box.offer(focus(AudioFocusState.Focus.GAIN));
        box.offer(RELOAD);
        if (!box.loadingBoundary(50, true)) throw new AssertionError("loading entry missing");
        box.offer(zoom(1.2f));
        box.offer(LOOK);
        box.offer(BACK);
        box.offer(cancel(49L));
        box.offer(cancel(50L));
        if (box.loadingBoundary(50, true)) throw new AssertionError("same generation repurged");
        if (!box.pollLocationLoadCancel(50)) throw new AssertionError("active cancel lost");
        expect(drain(box), "audio focus gain", "graphics reload");
        box.offer(cancel(50L));
        box.offer(focus(AudioFocusState.Focus.DUCK));
        box.loadingBoundary(50, false);
        expect(drain(box), "audio focus duck");
        box.offer(BACK);
        expect(drain(box), "back");

        // Commands are bounded and refused (the caller logs); state always fits, purges reset the
        // count.
        for (int i = 0; i < MobileMailbox.COMMAND_CAPACITY; ++i)
            if (!box.offer(zoom(i))) throw new AssertionError("refused " + i);
        if (box.offer(BACK)) throw new AssertionError("a command beyond the capacity was accepted");
        if (!box.offer(focus(AudioFocusState.Focus.GAIN)) || !box.offer(RELOAD))
            throw new AssertionError("state refused on a full mailbox");
        box.purgeCommands();
        expect(drain(box), "audio focus gain", "graphics reload");
        for (int i = 0; i < MobileMailbox.COMMAND_CAPACITY; ++i) box.offer(BACK);
        for (int i = 0; i < 10; ++i) box.poll();
        for (int i = 0; i < 10; ++i)
            if (!box.offer(BACK)) throw new AssertionError("poll did not free a slot");
        if (box.offer(BACK)) throw new AssertionError("poll miscounted");
        System.out.println(
                "PASS mailbox: purges keep the latest audio focus, graphics reload and HUD declarations, drop commands and true duplicates");
    }
}
