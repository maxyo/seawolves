package org.stormengine.port;

import java.util.Locale;

/** Generation-bound load UI state. No native diagnostic counters reach the visible text. */
final class LocationReadiness {
    /** The engine's scene load state (render::GetSceneLoadStatus), or the cancel it accepted. */
    enum State {
        INACTIVE,
        PREPARING,
        ERROR,
        READY,
        CANCELLED
    }

    /** What the engine answered to the last load action: nothing, the cancel, a stale action. */
    enum Ack {
        NONE,
        CANCEL,
        STALE
    }

    static final class Snapshot {
        final long generation;
        final State state;
        final Ack ack;
        final boolean active;

        Snapshot(long generation, State state, boolean active, Ack ack) {
            this.generation = generation;
            this.state = state;
            this.active = active;
            this.ack = ack;
        }

        boolean blocks() {
            return active || state == State.CANCELLED;
        }

        boolean cancelAccepted() {
            return state == State.CANCELLED && ack == Ack.CANCEL;
        }
    }

    /** The engine's words for a state and an answer (onLocationReadiness); anything else throws. */
    static State state(String engine) {
        return State.valueOf(engine.toUpperCase(Locale.ROOT));
    }

    static Ack ack(String engine) {
        return engine.isEmpty() ? Ack.NONE : Ack.valueOf(engine.toUpperCase(Locale.ROOT));
    }

    private Snapshot current;

    synchronized Snapshot accept(long generation, State state, boolean active, Ack ack) {
        if (current != null && Long.compareUnsigned(generation, current.generation) < 0)
            return null;
        // A delayed preparing notification must not reopen a terminal generation.
        if (current != null && generation == current.generation && !current.active && active)
            return null;
        current = new Snapshot(generation, state, active, ack);
        return current;
    }

    synchronized Snapshot current() {
        return current;
    }

    synchronized boolean isCurrent(Snapshot value) {
        return current == value;
    }

    /** The message that cancels the load `shown`; null when that load is no longer the current one. */
    synchronized MobileMailbox.Message cancel(Snapshot shown) {
        if (current == null || shown.generation != current.generation || !current.active)
            return null;
        return MobileMailbox.Message.locationLoadCancel(shown.generation);
    }
}
