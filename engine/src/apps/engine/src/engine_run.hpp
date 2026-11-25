#pragma once
// What the engine's main loop and its Android stages share: the core, whether the loop runs and whether the window
// is active, and the one owner of "may sound play".
#include "core_private.h"
#include "v_sound_service.h"
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace storm::engine
{
inline CorePrivate *core_private = nullptr;
inline bool isRunning = false;
inline bool bActive = true;
inline bool bSoundInBackground = false;

// Single owner of "may sound play right now". Window focus, the Android
// activity lifecycle and Android audio focus all feed it instead of calling
// SetActiveWithFade independently, where they could re-enable each other's pause.
struct AudioGate
{
    bool windowFocused = true; // OSWindow focus
    bool foreground = true;    // AppEvents lifecycle (Android pause/resume)
    bool audioFocus = true;    // Android AudioManager focus, reported by the activity
    float duck = 1.0f;         // transient "may duck" focus loss
    bool applied = true;
    float appliedDuck = 1.0f;

    void apply()
    {
        if (!core_private || !core_private->initialized())
            return;
        const bool active = foreground && audioFocus && (windowFocused || bSoundInBackground);
        if (active == applied && duck == appliedDuck)
            return;
        auto *soundService = static_cast<VSoundService *>(core.GetService("SoundService"));
        if (!soundService)
            return;
        if (duck != appliedDuck)
        {
            soundService->SetDucking(duck);
            appliedDuck = duck;
        }
        if (active != applied)
        {
            soundService->SetActiveWithFade(active);
            applied = active;
        }
#ifdef __ANDROID__
        // A sound that stays paused (a lost audio focus gain) must be visible in the log.
        __android_log_print(ANDROID_LOG_INFO, "StormAudio",
                            "sound %s, duck %.2f (foreground %d, window focus %d, audio focus %d)",
                            active ? "active" : "paused", duck, foreground, windowFocused, audioFocus);
#endif
    }
};
inline AudioGate audioGate;
} // namespace storm::engine
