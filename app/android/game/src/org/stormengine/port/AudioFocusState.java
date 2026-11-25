package org.stormengine.port;

/** Maps Android audio-focus changes to the focus the engine is told (MobileMailbox.Type.AUDIO_FOCUS). */
final class AudioFocusState {
    enum Focus {
        /** Play at full volume. */
        GAIN,
        /** Another app plays a short sound: keep playing, attenuated. */
        DUCK,
        /** Another app owns the output (call, music, assistant): pause. */
        LOSS
    }

    // android.media.AudioManager constants, spelled out to keep this class host-testable.
    static final int GAIN = 1,
            GAIN_TRANSIENT = 2,
            GAIN_TRANSIENT_MAY_DUCK = 3,
            GAIN_TRANSIENT_EXCLUSIVE = 4;
    static final int LOSS = -1, LOSS_TRANSIENT = -2, LOSS_TRANSIENT_CAN_DUCK = -3;
    static final int REQUEST_FAILED = 0, REQUEST_GRANTED = 1, REQUEST_DELAYED = 2;

    private Focus reported;
    private boolean permanentlyLost;

    /** @return the focus to tell the engine, or null when the engine already has it. */
    Focus onFocusChange(int change) {
        switch (change) {
            case GAIN:
            case GAIN_TRANSIENT:
            case GAIN_TRANSIENT_MAY_DUCK:
            case GAIN_TRANSIENT_EXCLUSIVE:
                permanentlyLost = false;
                return report(Focus.GAIN);
            case LOSS_TRANSIENT_CAN_DUCK:
                return report(Focus.DUCK);
            case LOSS:
                permanentlyLost = true;
                return report(Focus.LOSS);
            case LOSS_TRANSIENT:
                return report(Focus.LOSS);
            default:
                return null; // unknown future value: keep the current state
        }
    }

    /** Result of AudioManager.requestAudioFocus. A delayed grant is silent until the listener reports gain. */
    Focus onRequestResult(int result) {
        if (result == REQUEST_GRANTED) {
            permanentlyLost = false;
            return report(Focus.GAIN);
        }
        return report(Focus.LOSS);
    }

    /**
     * After a permanent loss Android never calls the listener again; focus has to be
     * requested anew when the player comes back to the game.
     */
    boolean shouldRequestAgain() {
        return permanentlyLost || reported != Focus.GAIN && reported != Focus.DUCK;
    }

    /** Focus was released (pause): the next request reports its result even when unchanged. */
    void forget() {
        reported = null;
    }

    private Focus report(Focus focus) {
        if (focus == reported) return null;
        reported = focus;
        return focus;
    }
}
