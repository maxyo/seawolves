package org.stormengine.port;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;

/**
 * Holds audio focus for the game while its activity is resumed and forwards
 * focus changes to the engine: pause on loss, attenuate on "may duck", resume
 * on gain. Pausing for the activity lifecycle itself is done by the engine
 * (SDL background/foreground events); this class only releases focus so other
 * apps resume their playback.
 */
final class AudioFocus implements AudioManager.OnAudioFocusChangeListener {
    interface Sink {
        void send(String action);
    }

    private final AudioManager manager;
    private final Sink sink;
    private final AudioFocusState state = new AudioFocusState();
    private AudioFocusRequest request;
    private boolean held;

    AudioFocus(Context context, Sink sink) {
        manager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        this.sink = sink;
    }

    /** UI thread: activity resumed, or the player returned to the window after a permanent loss. */
    void acquire() {
        if (manager == null) return;
        if (held && !state.shouldRequestAgain()) return;
        int result;
        if (Build.VERSION.SDK_INT >= 26) {
            if (request == null) {
                request =
                        new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                                .setAudioAttributes(
                                        new AudioAttributes.Builder()
                                                .setUsage(AudioAttributes.USAGE_GAME)
                                                .setContentType(
                                                        AudioAttributes.CONTENT_TYPE_SONIFICATION)
                                                .build())
                                // Let the system duck us automatically; the listener still handles
                                // explicit ducking.
                                .setWillPauseWhenDucked(false)
                                .setAcceptsDelayedFocusGain(true)
                                .setOnAudioFocusChangeListener(this)
                                .build();
            }
            result = manager.requestAudioFocus(request);
        } else {
            result =
                    manager.requestAudioFocus(
                            this, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        }
        held = result != AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        forward(state.onRequestResult(result));
    }

    /** UI thread: activity paused or destroyed. */
    void release() {
        if (manager == null) return;
        if (held) {
            if (Build.VERSION.SDK_INT >= 26) {
                if (request != null) manager.abandonAudioFocusRequest(request);
            } else manager.abandonAudioFocus(this);
        }
        held = false;
        // Focus is requested anew on resume: report its result even when it equals the last state.
        state.forget();
    }

    @Override
    public void onAudioFocusChange(int change) {
        forward(state.onFocusChange(change));
    }

    private void forward(String action) {
        if (action != null) sink.send(action);
    }
}
