#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#ifdef __ANDROID__
#include "android_runtime.hpp"
#endif

// «Вибрация» (settings screen «Экран и касания», app setting haptics): short effects for what the player feels
// in a fight. The engine names the moment, the application plays it (GameActivity.onHaptic(int): the view's haptic
// feedback, which follows the system's touch-feedback setting). A moment repeated sooner than its gap is dropped
// (a broadside's balls hitting one after another are one hit).
namespace storm::android
{
// The values are GameActivity's HAPTIC_* constants.
enum class Haptic : uint8_t
{
    Hit = 0,       // the hero or his ship is hit
    Block = 1,     // the hero blocks or parries a blow
    Broadside = 2, // the player's ship fires a side
    Boarding = 3,  // a boarding fight begins
    Count,
};

// «Вибрация» on: the app setting haptics of the "appSettings" channel (platform/display_options.hpp), set by the UI
// host whenever the app sends its settings.
inline std::atomic<bool> hapticsOn{true};

// The shortest time between two effects of one kind, in ms.
inline constexpr std::array<uint32_t, static_cast<std::size_t>(Haptic::Count)> kHapticGapMs = {250, 150, 600, 3000};

class HapticThrottle
{
  public:
    // True when an effect of `kind` may play at `now_ms` (and it is then counted as played).
    bool Allow(Haptic kind, uint64_t now_ms)
    {
        const auto index = static_cast<std::size_t>(kind);
        if (index >= last_.size())
            return false;
        if (played_[index] && now_ms - last_[index] < kHapticGapMs[index])
            return false;
        played_[index] = true;
        last_[index] = now_ms;
        return true;
    }

  private:
    std::array<uint64_t, static_cast<std::size_t>(Haptic::Count)> last_{};
    std::array<bool, static_cast<std::size_t>(Haptic::Count)> played_{};
};

// Game thread. Off when the player switched «Вибрация» off.
inline void haptic(Haptic kind)
{
#ifdef __ANDROID__
    static HapticThrottle throttle;
    if (!hapticsOn.load(std::memory_order_relaxed) || !throttle.Allow(kind, SDL_GetTicks64()))
        return;
    CallActivity(upcall::GameActivity::onHaptic, [&](JNIEnv *env, jobject activity, jmethodID method) {
        env->CallVoidMethod(activity, method, static_cast<jint>(kind));
        return true;
    });
#else
    (void)kind;
#endif
}
} // namespace storm::android
