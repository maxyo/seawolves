// The land hero's bars and status, channel "vitals" of the portable UI (publishVitals), as the land interface reads
// them (battle_interface land_status.hpp).
#pragma once
#include "android_gameplay.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android
{
// A character of the land HUD: its index, name, FaceId and, for the picture drawn without a
// FaceId, the land interface icon's texture and uv text as the scripts set them.
struct LandPortrait
{
    int id = -1;
    std::string name, face, texture, uv;
    bool operator==(const LandPortrait &) const = default;
};
struct LandOfficer
{
    LandPortrait who;
    float health = 0.f, energy = 0.f;
    bool operator==(const LandOfficer &) const = default;
};
struct LandOpponent
{
    std::string name;
    float health = 0.f;
    bool operator==(const LandOpponent &) const = default;
};
// The boarding deck fight (not the cabin, where the captain is the opponent): our men and theirs, now and at the
// fight's start (real men, as the game counts them after the fight). The boarding deck is boarding_location >= 0 of
// TEHO LAi_boarding.c; a side's men are its living fighters on the deck (group "player" without the hero and
// officers, group "brdenemy") and its reserve, as landBoardingMen counts them.
struct LandBoarding
{
    int ours = 0, theirs = 0, oursStart = 0, theirsStart = 0;
    bool operator==(const LandBoarding &) const = default;
};
// The boarding script's counters (TEHO Loc_ai/LAi_boarding.c) as men: a fighter on the deck is a body
// standing for crew_per_chr real men, and each side counts its reserve (boarding_*_crew, not yet on
// the deck) and its living fighters on the deck; the start is boarding_*_crew_start bodies. A reserve
// below 0 is an empty one (the script takes its musketeers from the enemy reserve without a check, so
// it runs to -2 and less). Nothing when there is no start or a body stands for no men (the script
// divides the crew by the bodies).
inline std::optional<LandBoarding> landBoardingMen(int oursReserve, int oursAlive, float oursPerBody, int oursStart,
                                                   int theirsReserve, int theirsAlive, float theirsPerBody,
                                                   int theirsStart)
{
    if (std::min(oursAlive, theirsAlive) < 0 || oursStart <= 0 || theirsStart <= 0 || !std::isfinite(oursPerBody) ||
        !std::isfinite(theirsPerBody) || oursPerBody <= 0.f || theirsPerBody <= 0.f)
        return std::nullopt;
    const auto men = [](int bodies, float perBody) { return static_cast<int>(std::lround(bodies * perBody)); };
    return LandBoarding{men(std::max(0, oursReserve) + oursAlive, oursPerBody),
                        men(std::max(0, theirsReserve) + theirsAlive, theirsPerBody), men(oursStart, oursPerBody),
                        men(theirsStart, theirsPerBody)};
}
// The scripts make the hero walk at a full stick by giving him the walk animation as his run (characters.c
// CheckAndSetOverloadMode); why (the load, a quest) is theirs and not told.
inline bool landWalksOnly(std::string_view run, std::string_view walk)
{
    return !run.empty() && run == walk;
}
struct LandVitals
{
    float health = 0.f, energy = 0.f, reload = 0.f;
    int loaded = 0, capacity = 0;
    // The hero: character index, name, FaceId and, without a FaceId, the picture the land interface draws instead
    // (the party's first portrait).
    LandPortrait hero;
    // "poison", "drunk", "healing" (the hero's chr_ai.poison / chr_ai.drunk / chr_ai.hp_bottle,
    // Loc_ai/LAi_character.c), then "walkonly" while his run is the walk (landWalksOnly).
    std::vector<std::string> effects;
    int potions = 0; // the game's potions the hero carries (land_status.hpp potionIds)
    // data.icons.id1… of the land interface (landinterface.c SetCharacterIconData: without a FaceId its texture and uv,
    // or the generic two.tga figure when the game has that file).
    std::vector<LandOfficer> officers;
    // The enemy captain of a boarding cabin fight while alive: the fantom the boarding script gives the captain's
    // attributes and CaptanId (Loc_ai/LAi_boarding.c LAi_SetBoardingActors); health = chr_ai.hp / chr_ai.hp_max.
    std::optional<LandOpponent> opponent;
    std::optional<LandBoarding> boarding;
    std::optional<float> dash;            // 0…1, the backstep's remaining cooldown in fight mode (Character RecoilWait)
    std::vector<BattleAbility> abilities; // the land abilities (land_status.hpp readLandAbilities)
    bool operator==(const LandVitals &) const = default;
};
// When the vitals go to the UI: a change at most every 100 ms, the hero's disappearance at once, the same state
// never again.
class VitalsPublication
{
    std::optional<std::optional<LandVitals>> published_;
    uint32_t at_ = 0;

  public:
    bool due(const std::optional<LandVitals> &vitals, uint32_t tick)
    {
        if ((published_ && *published_ == vitals) || (vitals && tick - at_ < 100))
            return false;
        published_ = vitals;
        at_ = tick;
        return true;
    }
};
// The channel's state; `picture(portrait)` is the picture drawn for a portrait (none: an empty one).
template <class Picture> storm::ui::hud::Vitals landVitalsHud(const LandVitals &v, Picture &&picture)
{
    storm::ui::hud::Vitals out;
    out.known = true;
    out.health = hudReading(v.health);
    out.energy = hudReading(v.energy);
    out.reload = hudReading(v.reload);
    out.capacity = v.capacity;
    out.loaded = std::min(v.loaded, v.capacity);
    out.hero = v.hero.id;
    out.name = v.hero.name;
    out.face = v.hero.face;
    out.picture = picture(v.hero);
    out.effects = v.effects;
    out.potions = v.potions;
    for (const auto &officer : v.officers)
        out.officers.push_back({officer.who.id, officer.who.name, officer.who.face, picture(officer.who),
                                hudReading(officer.health), hudReading(officer.energy)});
    if (v.opponent)
    {
        out.opponent = v.opponent->name;
        out.opponent_health = hudReading(v.opponent->health);
    }
    if (v.boarding)
        out.boarding = storm::ui::hud::Vitals::Boarding{v.boarding->ours, v.boarding->theirs, v.boarding->oursStart,
                                                        v.boarding->theirsStart};
    if (v.dash)
        out.dash = hudReading(std::clamp(*v.dash, 0.f, 1.f));
    out.abilities = hudAbilities(v.abilities);
    return out;
}
} // namespace storm::android
