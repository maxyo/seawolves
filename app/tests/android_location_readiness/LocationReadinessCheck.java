package org.stormengine.port;

public final class LocationReadinessCheck {
    static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        MobileMailbox mailbox = new MobileMailbox();
        mailbox.offer(MobileMailbox.Message.locationLoadCancel(1));
        check(
                mailbox.poll().toString().equals("location load cancel 1"),
                "actual mailbox dispatches cancel");
        mailbox.offer(MobileMailbox.Message.locationLoadCancel(1));
        mailbox.purgeCommands();
        check(mailbox.poll() == null, "lifecycle drops queued stale load command");
        LocationReadiness state = new LocationReadiness();
        LocationReadiness.Snapshot first =
                state.accept(
                        1, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE);
        check(first.blocks(), "preparing owns screen");
        check(
                state.cancel(first).toString().equals("location load cancel 1"),
                "cancel bound to generation");
        LocationReadiness.Snapshot refreshed =
                state.accept(
                        1, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE);
        check(refreshed.blocks(), "refreshed preparing owns screen");
        check(
                state.cancel(first).toString().equals("location load cancel 1"),
                "held Cancel stays valid across a refresh");
        LocationReadiness.Snapshot error =
                state.accept(1, LocationReadiness.State.ERROR, true, LocationReadiness.Ack.NONE);
        check(error.blocks(), "error cannot reveal");
        check(state.cancel(error) != null, "a failed load keeps its menu escape");
        LocationReadiness.Snapshot next =
                state.accept(
                        2, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE);
        check(state.cancel(error) == null, "old generation button cannot cancel new load");
        check(
                state.accept(1, LocationReadiness.State.READY, false, LocationReadiness.Ack.NONE)
                        == null,
                "old completion cannot reveal the new load");
        check(!state.isCurrent(error) && state.isCurrent(next), "queued old UI callback ignored");
        LocationReadiness.Snapshot cancelled =
                state.accept(
                        2, LocationReadiness.State.CANCELLED, false, LocationReadiness.Ack.CANCEL);
        check(
                cancelled.cancelAccepted() && cancelled.blocks(),
                "accepted cancel records menu before hiding UI");
        check(state.cancel(cancelled) == null, "terminal cancel cannot redispatch");
        check(
                state.accept(2, LocationReadiness.State.PREPARING, true, LocationReadiness.Ack.NONE)
                        == null,
                "late preparing cannot reopen terminal generation");
        LocationReadiness.Snapshot high =
                state.accept(
                        Long.MIN_VALUE,
                        LocationReadiness.State.PREPARING,
                        true,
                        LocationReadiness.Ack.NONE);
        check(
                high != null
                        && state.cancel(high)
                                .toString()
                                .equals("location load cancel 9223372036854775808"),
                "unsigned generation exact");
        LocationReadiness.Snapshot ready =
                state.accept(
                        Long.MIN_VALUE,
                        LocationReadiness.State.READY,
                        false,
                        LocationReadiness.Ack.NONE);
        check(!ready.blocks() && !ready.cancelAccepted(), "ready releases only load overlay");
        check(
                LocationReadiness.state("cancelled") == LocationReadiness.State.CANCELLED
                        && LocationReadiness.ack("") == LocationReadiness.Ack.NONE
                        && LocationReadiness.ack("stale") == LocationReadiness.Ack.STALE,
                "the engine's words");
        for (String unknown : new String[] {"nonsense", ""})
            try {
                LocationReadiness.state(unknown);
                throw new AssertionError("unknown state accepted: " + unknown);
            } catch (IllegalArgumentException expected) {
            }
        System.out.println("PASS location readiness generation/cancel/terminal UI controls");
    }
}
