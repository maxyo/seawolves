#pragma once

// AI_MESSAGE_CANNONS_RANGE: the overlay of the player's boards' fire zones on the sea. A charged board's zone shades
// from its near colour at the ship to its edge colour at its range; a reloading board's does the same in the other
// pair of colours, and its alpha rises over one time and falls over the other, again and again.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace storm::sea_ai
{
struct CannonsRange
{
    uint32_t chargedNear = 0, chargedEdge = 0;     // ARGB
    uint32_t reloadingNear = 0, reloadingEdge = 0; // ARGB
    float riseSeconds = 0.0f, fallSeconds = 0.0f;
    bool enabled = false;

    struct Shades
    {
        uint32_t nearColor, edgeColor;
    };

    // The colours of a board `seconds` after the overlay began.
    [[nodiscard]] Shades BoardShades(bool charged, float seconds) const
    {
        if (charged)
            return {chargedNear, chargedEdge};
        return {Blink(reloadingNear, seconds), Blink(reloadingEdge, seconds)};
    }

  private:
    [[nodiscard]] uint32_t Blink(uint32_t argb, float seconds) const
    {
        const float period = riseSeconds + fallSeconds;
        float k = 1.0f;
        if (period > 0.0f)
        {
            const float t = std::fmod(seconds, period);
            k = t < riseSeconds ? t / riseSeconds : 1.0f - (t - riseSeconds) / fallSeconds;
        }
        const auto alpha = static_cast<uint32_t>(std::lround(std::clamp(k, 0.0f, 1.0f) * float(argb >> 24)));
        return (alpha << 24) | (argb & 0x00FFFFFFu);
    }
};
} // namespace storm::sea_ai
