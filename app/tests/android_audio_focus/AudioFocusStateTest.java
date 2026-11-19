package org.stormengine.port;

/** Host checks for the audio-focus to engine-action mapping. */
public final class AudioFocusStateTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void same(String actual, String expected, String message) {
        require(
                expected == null ? actual == null : expected.equals(actual),
                message + ": " + actual);
    }

    public static void main(String[] arguments) {
        AudioFocusState s = new AudioFocusState();
        require(s.shouldRequestAgain(), "Fresh state must request focus");
        same(s.onRequestResult(AudioFocusState.REQUEST_GRANTED), "audiofocus:gain", "Grant");
        require(!s.shouldRequestAgain(), "Held focus requested again");
        same(s.onRequestResult(AudioFocusState.REQUEST_GRANTED), null, "Duplicate gain resent");
        // Navigation prompt / notification: duck, then restore.
        same(s.onFocusChange(AudioFocusState.LOSS_TRANSIENT_CAN_DUCK), "audiofocus:duck", "Duck");
        same(
                s.onFocusChange(AudioFocusState.LOSS_TRANSIENT_CAN_DUCK),
                null,
                "Duplicate duck resent");
        require(!s.shouldRequestAgain(), "Ducked focus is still held");
        same(s.onFocusChange(AudioFocusState.GAIN), "audiofocus:gain", "Gain after duck");
        // Phone call: pause, resume automatically.
        same(s.onFocusChange(AudioFocusState.LOSS_TRANSIENT), "audiofocus:loss", "Transient loss");
        same(s.onFocusChange(AudioFocusState.GAIN), "audiofocus:gain", "Gain after call");
        // Music app takes over: pause; Android will not notify again.
        same(s.onFocusChange(AudioFocusState.LOSS), "audiofocus:loss", "Permanent loss");
        require(
                s.shouldRequestAgain(),
                "Permanent loss must be re-requested when the player returns");
        same(s.onFocusChange(AudioFocusState.LOSS_TRANSIENT), null, "Loss while lost resent");
        same(
                s.onRequestResult(AudioFocusState.REQUEST_GRANTED),
                "audiofocus:gain",
                "Regain after permanent loss");
        require(!s.shouldRequestAgain(), "Regained focus requested again");
        // Request refused (call in progress) or delayed: stay silent until gain arrives.
        same(
                s.onRequestResult(AudioFocusState.REQUEST_FAILED),
                "audiofocus:loss",
                "Refused request");
        require(s.shouldRequestAgain(), "Refused request must be retried");
        same(
                s.onRequestResult(AudioFocusState.REQUEST_DELAYED),
                null,
                "Delayed request while silent");
        same(s.onFocusChange(AudioFocusState.GAIN_TRANSIENT), "audiofocus:gain", "Delayed gain");
        // Unknown values keep the state; pause forgets it so resume reports again.
        same(s.onFocusChange(12345), null, "Unknown change");
        s.forget();
        same(
                s.onRequestResult(AudioFocusState.REQUEST_GRANTED),
                "audiofocus:gain",
                "State not resent after pause");
        System.out.println("PASS: audio focus mapping");
    }
}
