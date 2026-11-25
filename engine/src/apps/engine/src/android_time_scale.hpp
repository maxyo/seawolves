#pragma once
// Four steps of the game's time acceleration for a touch HUD: x1, x2, x4, x8. The scripts keep the logic: the
// counter is set one step of their own ladder below the target and one TimeScaleFaster / TimeScaleSlower activation
// runs their handler, with its guards, clamps and the TimeSpeed perk. The engine's time scale decides which step is
// current, not TimeScaleCounter: leaving an interface and dialogues set the scale without the counter.
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace storm::android
{
// The scripts' counters of x1, x2, x4, x8 and the counter one of their own steps below each.
inline constexpr std::array<int32_t, 4> TimeScaleCounters{0, 4, 12, 28};
inline constexpr std::array<int32_t, 4> TimeScaleFrom{1, 3, 10, 24};

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
//   bool counter(int32_t &value)      TimeScaleCounter (false: no such variable)
//   void setCounter(int32_t value)
//   float scale()                     the engine's time scale
//   void activate(const char *control) a control activation the scripts' handler runs
template <class Game> void requestTimeScaleStep(Game &game, int step)
{
    int32_t current = 0;
    if (step < 0 || step >= static_cast<int>(TimeScaleCounters.size()) || !game.counter(current))
        return;
    const float before = game.scale();
    const float target = 1.f + static_cast<float>(TimeScaleCounters[step]) * 0.25f;
    if (current == TimeScaleCounters[step] && std::fabs(before - target) < 0.01f)
        return;
    game.setCounter(TimeScaleFrom[step]);
    game.activate(step == 0 ? "TimeScaleSlower" : "TimeScaleFaster");
    if (std::fabs(game.scale() - before) >= 0.01f)
        return;
    // Nothing changed: the scripts refused (underwater, a quest lock) or their limit (x2.5 on the world map) is
    // reached. The counter goes back, and a step up goes round to x1 instead of standing still on a button that no
    // longer changes anything.
    game.setCounter(current);
    if (step > 0)
        requestTimeScaleStep(game, 0);
}
} // namespace storm::android

#ifdef __ANDROID__
#include "core.h"
#include "platform/android_ui.hpp"
#include "v_data.h"

namespace storm::android
{
// Runs at a completed frame boundary, like every other control activation.
inline void applyTimeScaleStep(int step)
{
    struct Scripts
    {
        VDATA *variable = static_cast<VDATA *>(core.GetScriptVariable("TimeScaleCounter"));
        bool counter(int32_t &value) const
        {
            return variable && variable->Get(value);
        }
        void setCounter(int32_t value) const
        {
            variable->Set(value);
        }
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
