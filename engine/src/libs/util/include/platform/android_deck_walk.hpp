#pragma once
#include "storm_ui/hud_command.h"
#include "android_combat_input.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string_view>

namespace storm::android
{
// Walking on deck in first person (DECK_CAMERA), from the touch HUD's walk stick: one vector,
// forward and sideways, as far as the stick is pushed. The game has no analog deck walk and no
// deck strafe at all: it binds DeckCamera_Forward / DeckCamera_Backward to keys, and its
// DeckCamera_Left / DeckCamera_Right turn the view (the scripts leave them unbound; the strafe of
// the original deck camera is commented out). So the stick is not fed as keys: the deck camera
// reads this vector beside its keys. Game-thread owned.
//
// Request "<gameplayRevision>\t<x>\t<y>": x to the right, y forward, -100…100 each and at most 100
// long together (the stick's travel after the HUD's dead zone and response curve). A walk is taken
// in the current gameplay revision and lasts, through any number of frames, until "0\t0" (pointer
// up or cancel in the UI, accepted in any revision), a cancelled input (cancelAndroidCombatInput)
// or the guard failing (no deck walk bound, the player cannot act): events, never a clock.
struct DeckStep
{
    float x = 0.0f, y = 0.0f; // -1…1, length <= 1
};
class DeckWalk
{
    uint64_t revision_ = 0, cancel_ = 0;
    int x_ = 0, y_ = 0;
    void synchronize()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch != cancel_)
        {
            cancel_ = epoch;
            x_ = y_ = 0;
        }
    }

  public:
    // Installed by the engine: the gameplay binds the deck walk and the player can act.
    std::function<bool()> guard;
    void update(uint64_t gameplayRevision)
    {
        revision_ = gameplayRevision;
    }
    bool offer(const storm::ui::hud::DeckWalk &walk, uint64_t enqueueEpoch)
    {
        synchronize();
        if (enqueueEpoch != cancel_)
            return false; // sent before a cancellation
        if (std::abs(walk.x) > 100 || std::abs(walk.y) > 100 ||
            walk.x * walk.x + walk.y * walk.y > 101 * 101) // 100 long, give or take the rounding of each part
            return false;
        if (!walk.x && !walk.y)
        {
            x_ = y_ = 0;
            return true;
        } // the end of a walk always counts
        if (walk.revision != revision_ || !revision_ || !guard || !guard())
            return false;
        x_ = walk.x;
        y_ = walk.y;
        return true;
    }
    // Read by the deck camera each frame: the walk, or nothing.
    std::optional<DeckStep> read()
    {
        synchronize();
        if (!x_ && !y_)
            return std::nullopt;
        if (!guard || !guard())
        {
            x_ = y_ = 0;
            return std::nullopt;
        }
        const float length = std::max(100.0f, std::hypot(static_cast<float>(x_), static_cast<float>(y_)));
        return DeckStep{static_cast<float>(x_) / length, static_cast<float>(y_) / length};
    }
};
inline DeckWalk deckWalk;
} // namespace storm::android
