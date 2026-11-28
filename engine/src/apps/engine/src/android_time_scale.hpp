#pragma once
// Four steps of the game's time acceleration for a touch HUD: x1, x2, x4, x8. The scripts own the ladder, its caps
// and refusals: a step only presses their TimeScaleSlower / TimeScaleFaster controls and reads the engine's time
// scale they set. Their counter can be out of step with the scale (leaving an interface sets x1 and keeps it), so a
// step first presses Slower down to the bottom of their ladder and then Faster up to the target.
#include <array>
#include <cmath>
#include <string_view>

namespace storm::android
{
inline constexpr std::array<float, 4> TimeScaleSteps{1.f, 2.f, 4.f, 8.f};

// "TimeScaleX1" … "TimeScaleX8" → the step, or -1.
inline int timeScaleStep(std::string_view control)
{
    constexpr std::array<std::string_view, 4> names{"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"};
    for (size_t i = 0; i < names.size(); ++i)
        if (control == names[i])
            return static_cast<int>(i);
    return -1;
}

// One HUD request for a step. `Game` is the scripts' side:
//   float scale()                      the engine's time scale
//   void activate(const char *control) a control activation the scripts' handler runs
template <class Game> void requestTimeScaleStep(Game &game, int step)
{
    if (step < 0 || step >= static_cast<int>(TimeScaleSteps.size()))
        return;
    const float before = game.scale();
    const float target = TimeScaleSteps[step];
    if (std::fabs(before - target) < 0.01f)
        return;
    // Each press is the scripts' answer; a press that leaves the scale as it was is their floor, cap or refusal.
    const auto press = [&game](const char *control) {
        const float was = game.scale();
        game.activate(control);
        return std::fabs(game.scale() - was) >= 0.01f;
    };
    while (press("TimeScaleSlower"))
    {
    }
    while (game.scale() < target - 0.01f && press("TimeScaleFaster"))
    {
    }
    // Their cap (x2.5 on the world map, x6 in a fight) gave the same scale as before: a step up goes round to x1
    // instead of standing still on a button that no longer changes anything.
    if (step > 0 && std::fabs(game.scale() - before) < 0.01f)
        requestTimeScaleStep(game, 0);
}
} // namespace storm::android

#ifdef STORM_TOUCH_UI
#include "core.h"
#include "platform/android_ui.hpp"

namespace storm::android
{
// Runs at a completed frame boundary, like every other control activation.
inline void applyTimeScaleStep(int step)
{
    struct Scripts
    {
        float scale() const
        {
            return core.GetTimeScale();
        }
        void activate(const char *control) const
        {
            // No toast for the new scale: the HUD's time button shows it.
            ++gameMessagesMuted;
            core.Event("Control Activation", "s", control);
            --gameMessagesMuted;
        }
    } scripts;
    requestTimeScaleStep(scripts, step);
}
} // namespace storm::android
#endif
