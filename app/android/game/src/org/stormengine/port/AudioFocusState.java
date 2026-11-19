package org.stormengine.port;

/**
 * Maps Android audio-focus changes to the engine's {@code audiofocus:} actions.
 * Pure Java (no android.* at runtime) so the host test can exercise it.
 *
 * gain  - play at full volume
 * duck  - another app plays a short sound; keep playing, attenuated
 * loss  - another app owns the output (call, music, assistant); pause
 */
final class AudioFocusState {
    // android.media.AudioManager constants, spelled out to keep this class host-testable.
    static final int GAIN = 1,
            GAIN_TRANSIENT = 2,
            GAIN_TRANSIENT_MAY_DUCK = 3,
            GAIN_TRANSIENT_EXCLUSIVE = 4;
    static final int LOSS = -1, LOSS_TRANSIENT = -2, LOSS_TRANSIENT_CAN_DUCK = -3;
    static final int REQUEST_FAILED = 0, REQUEST_GRANTED = 1, REQUEST_DELAYED = 2;

    private String reported = "";
    private boolean permanentlyLost;

    /** @return action to send to the engine, or null when the engine already has this state. */
    String onFocusChange(int change) {
        switch (change) {
            case GAIN:
            case GAIN_TRANSIENT:
            case GAIN_TRANSIENT_MAY_DUCK:
            case GAIN_TRANSIENT_EXCLUSIVE:
                permanentlyLost = false;
                return report("gain");
            case LOSS_TRANSIENT_CAN_DUCK:
                return report("duck");
            case LOSS:
                permanentlyLost = true;
                return report("loss");
            case LOSS_TRANSIENT:
                return report("loss");
            default:
                return null; // unknown future value: keep the current state
        }
    }

    /** Result of AudioManager.requestAudioFocus. A delayed grant is silent until the listener reports gain. */
    String onRequestResult(int result) {
        if (result == REQUEST_GRANTED) {
            permanentlyLost = false;
            return report("gain");
        }
        return report("loss");
    }

    /**
     * After a permanent loss Android never calls the listener again; focus has to be
     * requested anew when the player comes back to the game.
     */
    boolean shouldRequestAgain() {
        return permanentlyLost || !"gain".equals(reported) && !"duck".equals(reported);
    }

    /** Focus was released (pause): the next request reports its result even when unchanged. */
    void forget() {
        reported = "";
    }

    private String report(String state) {
        if (state.equals(reported)) return null;
        reported = state;
        return "audiofocus:" + state;
    }
}
