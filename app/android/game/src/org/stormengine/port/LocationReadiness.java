package org.stormengine.port;

/** Generation-bound load UI state. No native diagnostic counters reach the visible text. */
final class LocationReadiness {
    static final class Snapshot {
        final long generation;
        final String state, ack;
        final boolean active, retryable;

        Snapshot(long generation, String state, boolean active, boolean retryable, String ack) {
            this.generation = generation;
            this.state = state;
            this.active = active;
            this.retryable = retryable;
            this.ack = ack;
        }

        boolean blocks() {
            return active || state.equals("cancelled");
        }

        boolean cancelAccepted() {
            return state.equals("cancelled") && ack.equals("cancel");
        }
    }

    private Snapshot current;

    synchronized Snapshot accept(
            long generation, String state, boolean active, boolean retryable, String ack) {
        if (!state.equals("inactive")
                && !state.equals("preparing")
                && !state.equals("error")
                && !state.equals("ready")
                && !state.equals("cancelled")) return null;
        if (current != null && Long.compareUnsigned(generation, current.generation) < 0)
            return null;
        // A delayed preparing notification must not reopen a terminal generation.
        if (current != null && generation == current.generation && !current.active && active)
            return null;
        current = new Snapshot(generation, state, active, retryable, ack);
        return current;
    }

    synchronized Snapshot current() {
        return current;
    }

    synchronized boolean isCurrent(Snapshot value) {
        return current == value;
    }

    synchronized String command(Snapshot shown, String verb) {
        if (current == null || shown.generation != current.generation || !current.active)
            return null;
        if (!verb.equals("cancel")
                && !(verb.equals("retry") && current.state.equals("error") && current.retryable))
            return null;
        return "location-load:" + Long.toUnsignedString(shown.generation) + "\t" + verb;
    }
}
