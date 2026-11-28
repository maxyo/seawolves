#include "character_interaction.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace storm::location
{

namespace
{
// Tokens are positive script ints, never 0 (0 is "no session" / "any lock" to the scripts).
uint32_t NextToken(uint32_t &counter)
{
    counter = (counter + 1) & 0x7fffffffu;
    if (counter == 0)
        counter = 1;
    return counter;
}
} // namespace

uint32_t InteractionState::Begin()
{
    if (session_ != 0)
        return 0;
    session_ = NextToken(sessionCounter_);
    transitionLocked_ = false;
    return session_;
}

bool InteractionState::Transition(uint32_t token, bool active)
{
    if (token == 0 || token != session_)
        return false;
    transitionLocked_ = active;
    return true;
}

bool InteractionState::TransitionLocked() const
{
    return session_ != 0 && transitionLocked_;
}

bool InteractionState::End(uint32_t token)
{
    if (token == 0 || token != session_)
        return false;
    session_ = 0;
    transitionLocked_ = false;
    return true;
}

bool InteractionState::SessionActive() const
{
    return session_ != 0;
}

uint32_t InteractionState::LockHeight(float height)
{
    if (!std::isfinite(height))
        return 0;
    heightLock_ = NextToken(heightCounter_);
    height_ = height;
    return heightLock_;
}

void InteractionState::UnlockHeight(uint32_t token)
{
    if (token == 0 || token == heightLock_)
        heightLock_ = 0;
}

std::optional<float> InteractionState::HeldHeight() const
{
    return heightLock_ != 0 ? std::optional(height_) : std::nullopt;
}

std::optional<glm::vec3> FindFreePositionNear(const glm::vec3 &center, float radius, const glm::vec3 &towards,
                                              const std::function<bool(glm::vec3 &)> &isFree)
{
    if (!std::isfinite(radius) || radius < 0.0f || radius > 3.0f)
        return std::nullopt;
    glm::vec3 spot = center;
    if (isFree(spot))
        return spot;
    constexpr float kRingStep = 0.25f;
    const float start = std::atan2(towards.x - center.x, towards.z - center.z);
    for (float ring = kRingStep; ring <= radius + 1e-4f; ring += kRingStep)
    {
        // About one sample per 0.25 of the ring's length, at least 8 (Caribbean Legend's 8*pi*r).
        const int samples = std::max(8, static_cast<int>(std::ceil(8.0f * std::numbers::pi_v<float> * ring)));
        for (int n = 0; n < samples; ++n)
        {
            const float angle = start + 2.0f * std::numbers::pi_v<float> * static_cast<float>(n) / samples;
            spot = center + glm::vec3(std::sin(angle) * ring, 0.0f, std::cos(angle) * ring);
            if (isFree(spot))
                return spot;
        }
    }
    return std::nullopt;
}

} // namespace storm::location
