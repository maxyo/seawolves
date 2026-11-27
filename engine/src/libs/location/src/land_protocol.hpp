// What a game's land scripts expect of the engine that the game's files cannot tell: one row per game profile
// (game_profile.hpp), read through landFacts(); code never compares profiles.
#pragma once

#include "game_profile.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace storm::location
{

enum class FightModel : uint8_t
{
    Legacy,          // To Each His Own: feints, stun chance, action energy asked of the scripts
    CaribbeanLegend, // block bypass, power attacks, NPC attack cancels, overhead bars from the scripts
};

struct LandFacts
{
    FightModel fight;
    bool questSignsOnlyAtPeace; // a quest sign over a character shows only while it is alive, in peace and staying
    bool positionOfDead;        // MSG_CHARACTER_GETPOS and GETAY answer for a dead character
};

inline constexpr std::array<LandFacts, 2> kLandFacts = {{
    {FightModel::Legacy, false, false},
    {FightModel::CaribbeanLegend, true, true},
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
