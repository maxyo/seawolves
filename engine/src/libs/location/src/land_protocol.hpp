// What a game's land scripts expect of the engine that the game's files cannot tell: one row per game profile
// (game_profile.hpp), read through landFacts(); code never compares profiles.
#pragma once

#include "game_profile.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace storm::location
{

// The layout of the land events whose handlers read different arguments in each game.
enum class LandEventLayout : uint8_t
{
    Legacy,          // To Each His Own: Location_CharacterAttack "iislll", Location_CharacterFire "iifl"
    CaribbeanLegend, // "iislffl", "iiflfl"
};

struct LandFacts
{
    LandEventLayout events;
    // The most an NPC's attack or defence activity counts: To Each His Own's scripts give its best fighters up to
    // 1.1 and rely on it; Caribbean Legend's engine takes at most 1 of the same answer.
    float npcActivityMax;
};

inline constexpr std::array<LandFacts, 2> kLandFacts = {{
    {LandEventLayout::Legacy, 1000.0f},
    {LandEventLayout::CaribbeanLegend, 1.0f},
}};
static_assert(kLandFacts.size() == kGameProfiles.size());

inline const LandFacts &landFacts(GameProfile profile)
{
    return kLandFacts[static_cast<size_t>(profile)];
}

inline const LandFacts &landFacts()
{
    return landFacts(gameProfile());
}

// A command the engine does not know: an error, once per receiver and name.
void ReportUnknownCommand(std::string_view receiver, std::string_view name);
// A command accepted whose effect this engine does not have yet: a warning, once per command.
void ReportWithoutEffect(std::string_view command, std::string_view what);

} // namespace storm::location
