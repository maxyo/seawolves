package org.stormengine.port;

/** Host checks for the audio-focus to engine-action mapping. */
public final class AudioFocusStateTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void same(
            AudioFocusState.Focus actual, AudioFocusState.Focus expected, String message) {
        require(actual == expected, message + ": " + actual);
    }

    public static void main(String[] arguments) {
        AudioFocusState s = new AudioFocusState();
        require(s.shouldRequestAgain(), "Fresh state must request focus");
        same(
                s.onRequestResult(AudioFocusState.REQUEST_GRANTED),
                AudioFocusState.Focus.GAIN,
                "Grant");
        require(!s.shouldRequestAgain(), "Held focus requested again");
        same(s.onRequestResult(AudioFocusState.REQUEST_GRANTED), null, "Duplicate gain resent");
        // Navigation prompt / notification: duck, then restore.
        same(
                s.onFocusChange(AudioFocusState.LOSS_TRANSIENT_CAN_DUCK),
                AudioFocusState.Focus.DUCK,
                "Duck");
        same(
                s.onFocusChange(AudioFocusState.LOSS_TRANSIENT_CAN_DUCK),
                null,
                "Duplicate duck resent");
        require(!s.shouldRequestAgain(), "Ducked focus is still held");
        same(s.onFocusChange(AudioFocusState.GAIN), AudioFocusState.Focus.GAIN, "Gain after duck");
        // Phone call: pause, resume automatically.
        same(
                s.onFocusChange(AudioFocusState.LOSS_TRANSIENT),
                AudioFocusState.Focus.LOSS,
                "Transient loss");
        same(s.onFocusChange(AudioFocusState.GAIN), AudioFocusState.Focus.GAIN, "Gain after call");
        // Music app takes over: pause; Android will not notify again.
        same(s.onFocusChange(AudioFocusState.LOSS), AudioFocusState.Focus.LOSS, "Permanent loss");
        require(
                s.shouldRequestAgain(),
                "Permanent loss must be re-requested when the player returns");
        same(s.onFocusChange(AudioFocusState.LOSS_TRANSIENT), null, "Loss while lost resent");
        same(
                s.onRequestResult(AudioFocusState.REQUEST_GRANTED),
                AudioFocusState.Focus.GAIN,
                "Regain after permanent loss");
        require(!s.shouldRequestAgain(), "Regained focus requested again");
        // Request refused (call in progress) or delayed: stay silent until gain arrives.
        same(
                s.onRequestResult(AudioFocusState.REQUEST_FAILED),
                AudioFocusState.Focus.LOSS,
                "Refused request");
        require(s.shouldRequestAgain(), "Refused request must be retried");
        same(
                s.onRequestResult(AudioFocusState.REQUEST_DELAYED),
                null,
                "Delayed request while silent");
        same(
                s.onFocusChange(AudioFocusState.GAIN_TRANSIENT),
                AudioFocusState.Focus.GAIN,
                "Delayed gain");
        // Unknown values keep the state; pause forgets it so resume reports again.
        same(s.onFocusChange(12345), null, "Unknown change");
        s.forget();
        same(
                s.onRequestResult(AudioFocusState.REQUEST_GRANTED),
                AudioFocusState.Focus.GAIN,
                "State not resent after pause");
        System.out.println("PASS: audio focus mapping");
    }
}
