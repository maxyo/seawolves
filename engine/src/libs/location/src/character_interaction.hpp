// Caribbean Legend's interaction sessions on a character (MSG_CHARACTER_EX_MSG commands BeginInteractionSession,
// InteractionTransition, IsInteractionTransitionLocked, EndInteractionSession), its height lock (LockHeight,
// UnlockHeight) and the search of a free spot near a point (FindFreePositionNear). The dynamic objects' seat scripts
// put a character into a session while it sits, and hold its height on the seat. The position blends the same
// scripts use come with the movement layer; until then they are refused by name (character_interaction.cpp).
#pragma once

#include "storm_glm.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace storm::location
{

class InteractionState
{
  public:
    // A new session's token (never 0), or 0 while a session is active.
    uint32_t Begin();
    // Locks or unlocks the session's transition; false unless `token` is the active session's.
    bool Transition(uint32_t token, bool active);
    [[nodiscard]] bool TransitionLocked() const;
    // Ends the session; false unless `token` is the active session's.
    bool End(uint32_t token);
    [[nodiscard]] bool SessionActive() const;

    // Holds the character at `height` until unlocked: a new lock's token (never 0); 0 for a height not finite.
    uint32_t LockHeight(float height);
    // Lock 0 or the current lock's token unlocks; another token leaves the lock.
    void UnlockHeight(uint32_t token);
    [[nodiscard]] std::optional<float> HeldHeight() const;

  private:
    uint32_t sessionCounter_ = 0;
    uint32_t session_ = 0; // the active session's token, 0 without one
    bool transitionLocked_ = false;
    uint32_t heightCounter_ = 0;
    uint32_t heightLock_ = 0;
    float height_ = 0.0f;
};

// The first free spot around `center`: the centre itself, then rings 0.25 apart up to `radius`, each ring starting in
// the direction of `towards` (the character) and going round. `isFree` tests one spot and may move it onto the floor.
// Nothing when no spot is free or `radius` is not in [0, 3].
std::optional<glm::vec3> FindFreePositionNear(const glm::vec3 &center, float radius, const glm::vec3 &towards,
                                              const std::function<bool(glm::vec3 &)> &isFree);

} // namespace storm::location
