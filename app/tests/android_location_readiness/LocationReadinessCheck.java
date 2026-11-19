package org.stormengine.port;

public final class LocationReadinessCheck {
    static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        MobileMailbox mailbox = new MobileMailbox();
        mailbox.offer("location-load:1\tretry");
        check(mailbox.poll().equals("location-load:1\tretry"), "actual mailbox dispatches retry");
        mailbox.offer("location-load:1\tcancel");
        mailbox.purgeCommands();
        check(mailbox.poll() == null, "lifecycle drops queued stale load command");
        LocationReadiness state = new LocationReadiness();
        LocationReadiness.Snapshot first = state.accept(1, "preparing", true, false, "");
        check(first.blocks(), "preparing owns screen");
        check(state.command(first, "retry") == null, "not retryable before error");
        check(
                state.command(first, "cancel").equals("location-load:1\tcancel"),
                "cancel bound to generation");
        LocationReadiness.Snapshot refreshed = state.accept(1, "preparing", true, false, "");
        check(refreshed.blocks(), "refreshed preparing owns screen");
        check(
                state.command(first, "cancel").equals("location-load:1\tcancel"),
                "held Cancel stays valid across a refresh");
        check(state.command(refreshed, "retry") == null, "a refresh does not enable Retry");
        LocationReadiness.Snapshot error = state.accept(1, "error", true, true, "");
        check(error.blocks(), "error cannot reveal");
        check(
                state.command(error, "retry").equals("location-load:1\tretry"),
                "producer error can retry");
        state.accept(1, "error", true, true, "");
        check(
                state.command(error, "retry") != null,
                "diagnostic refresh does not invalidate same generation button");
        LocationReadiness.Snapshot retry = state.accept(2, "preparing", true, false, "retry");
        check(
                state.command(error, "cancel") == null,
                "old generation button cannot cancel new load");
        check(
                state.accept(1, "ready", false, false, "") == null,
                "old completion cannot reveal retry");
        check(!state.isCurrent(error) && state.isCurrent(retry), "queued old UI callback ignored");
        LocationReadiness.Snapshot fatal = state.accept(2, "error", true, false, "reload-required");
        check(state.command(fatal, "retry") == null, "mandatory failure cannot falsely retry");
        check(state.command(fatal, "cancel") != null, "mandatory failure retains menu escape");
        LocationReadiness.Snapshot cancelled = state.accept(2, "cancelled", false, false, "cancel");
        check(
                cancelled.cancelAccepted() && cancelled.blocks(),
                "accepted cancel records menu before hiding UI");
        check(state.command(cancelled, "cancel") == null, "terminal cancel cannot redispatch");
        check(
                state.accept(2, "preparing", true, false, "") == null,
                "late preparing cannot reopen terminal generation");
        LocationReadiness.Snapshot high =
                state.accept(Long.MIN_VALUE, "preparing", true, false, "");
        check(
                high != null
                        && state.command(high, "cancel")
                                .equals("location-load:9223372036854775808\tcancel"),
                "unsigned generation exact");
        LocationReadiness.Snapshot ready = state.accept(Long.MIN_VALUE, "ready", false, false, "");
        check(!ready.blocks() && !ready.cancelAccepted(), "ready releases only load overlay");
        check(
                state.accept(Long.MIN_VALUE, "nonsense", true, false, "") == null,
                "unknown state rejected");
        System.out.println("PASS location readiness generation/actions/terminal UI controls");
    }
}
