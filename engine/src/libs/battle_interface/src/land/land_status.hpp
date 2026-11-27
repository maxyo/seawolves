// The land hero's status for the portable HUD, published as extra fields of the "vitals" channel next to the bars
// (battle_man_sign.cpp): what each LandVitals field (platform/android_land_vitals.hpp) is read from.
#pragma once
#ifdef __ANDROID__
#include "core.h"
#include "entity.h"
#include "shared/messages.h"
#include "v_data.h"
#include "v_file_service.h"
#include <platform/android_gameplay.hpp>
#include <platform/android_land_vitals.hpp>
#include <platform/android_ui.hpp>
#include <platform/android_ship_telemetry.hpp>

#include <cstdio>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <string_view>

namespace storm::android
{

inline ATTRIBUTES *landCharacter(int index)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || index < 0 || static_cast<uint32_t>(index) >= characters->GetElementsNum())
        return nullptr;
    return characters->GetAClass(static_cast<uint32_t>(index));
}

// Text of the scripts' attributes for the land HUD: UTF-8 (scriptText) or nothing.
inline std::string landText(ATTRIBUTES *root, const char *path)
{
    auto *a = root ? root->FindAClass(root, path) : nullptr;
    return a && a->HasValue() ? scriptText(a->GetValue(), "vitals") : std::string();
}

inline float landNumber(ATTRIBUTES *root, const char *path)
{
    const std::string text = landText(root, path);
    const float value = text.empty() ? 0.f : std::strtof(text.c_str(), nullptr);
    return std::isfinite(value) ? value : 0.f;
}

// Ids of the game's potions: items with a potion class, as its potion menu and potion key take them
// (items_utilite.c FindPotionFromChr). Read once: the item table does not change.
inline const std::set<std::string> &potionIds()
{
    static std::set<std::string> ids;
    static bool read = false;
    if (read)
        return ids;
    auto *items = static_cast<VDATA *>(core.GetScriptVariable("Items"));
    if (!items || items->GetElementsNum() == 0)
        return ids;
    for (uint32_t i = 0; i < items->GetElementsNum(); ++i)
        if (auto *item = items->GetAClass(i); item && item->GetAttributeClass("potion"))
            if (const std::string id = landText(item, "id"); !id.empty())
                ids.insert(id);
    read = true;
    return ids;
}

inline int potions(ATTRIBUTES *hero)
{
    auto *items = hero ? hero->GetAttributeClass("items") : nullptr;
    if (!items)
        return 0;
    const auto &ids = potionIds();
    int count = 0;
    for (size_t i = 0; i < items->GetAttributesNum(); ++i)
        if (auto *item = items->GetAttributeClass(static_cast<uint32_t>(i)); item && ids.count(item->GetThisName()))
            count += std::max(
                0, static_cast<int>(std::strtol(item->HasValue() ? item->GetValue().c_str() : "0", nullptr, 10)));
    return count;
}

// For a character without a FaceId: the picture the land interface draws instead (the icon's
// texture and uv, landinterface.c: battle_interface\two.tga); none when the character has a
// FaceId (the UI shows the game's 128 px face), or when that file is not in the game's data (TEHO
// ships no two.tga: the game itself has no picture then). Asked only when the vitals change.
inline storm::ui::hud::Picture landPortraitPicture(const LandPortrait &portrait)
{
    if (!portrait.face.empty())
        return {};
    std::array<float, 4> uv{0.f, 0.f, 1.f, 1.f};
    if (!portrait.uv.empty() && std::sscanf(portrait.uv.c_str(), "%f,%f,%f,%f", &uv[0], &uv[1], &uv[2], &uv[3]) != 4)
        return {};
    if (portrait.texture.empty() ||
        !fio->_FileOrDirectoryExists(("resource\\textures\\" + portrait.texture + ".tx").c_str()))
        return {};
    return hudPicture(shipPicture(portrait.texture, uv));
}

inline LandPortrait landPortrait(int index, ATTRIBUTES *character, ATTRIBUTES *icon)
{
    const std::string name = landText(character, "name"), last = landText(character, "lastname");
    return {index, last.empty() ? name : name + " " + last, scriptFaceId(landText(character, "FaceId"), "vitals"),
            landText(icon, "texture"), landText(icon, "uv")};
}

// A script global of the boarding (LAi_boarding.c) by its declared type; a game without it (not TEHO) or of
// another type: false, said once per name.
template <class T> bool boardingGlobal(const char *name, T &value)
{
    auto *data = static_cast<VDATA *>(core.GetScriptVariable(name));
    if (data && data->Get(value))
        return true;
    static std::set<std::string> told;
    if (told.insert(name).second)
        core.Trace("HUD boarding: no script variable %s of the expected type; the boarding bars are not shown", name);
    return false;
}

// The deck fight's men (see "boarding" above); nothing outside a boarding, in the cabin (the captain is the
// opponent) or without the script's counters. `officers` are the land interface's officers (not crew).
inline std::optional<LandBoarding> readLandBoarding(const std::vector<LandOfficer> &officers, bool cabin)
{
    int32_t location = -1;
    if (cabin || !boardingGlobal("boarding_location", location) || location < 0)
        return std::nullopt;
    int32_t oursReserve = 0, oursStart = 0, theirsReserve = 0, theirsStart = 0;
    float oursPerBody = 0.f, theirsPerBody = 0.f;
    if (!boardingGlobal("boarding_player_crew", oursReserve) ||
        !boardingGlobal("boarding_player_crew_start", oursStart) ||
        !boardingGlobal("boarding_player_crew_per_chr", oursPerBody) ||
        !boardingGlobal("boarding_enemy_crew", theirsReserve) ||
        !boardingGlobal("boarding_enemy_crew_start", theirsStart) ||
        !boardingGlobal("boarding_enemy_crew_per_chr", theirsPerBody))
        return std::nullopt;
    int oursAlive = 0, theirsAlive = 0;
    for (const auto id : core.GetEntityIds("NPCharacter"))
    {
        auto *entity = core.GetEntityPointer(id);
        ATTRIBUTES *character = entity ? entity->AttributesPointer : nullptr;
        if (!character || landNumber(character, "chr_ai.hp") <= 0.f)
            continue;
        const std::string group = landText(character, "chr_ai.group");
        if (group == "brdenemy")
            ++theirsAlive;
        else if (group == "player")
        {
            const int index = static_cast<int>(landNumber(character, "index"));
            if (std::none_of(officers.begin(), officers.end(), [&](const LandOfficer &o) { return o.who.id == index; }))
                ++oursAlive;
        }
    }
    auto men = landBoardingMen(oursReserve, oursAlive, oursPerBody, oursStart, theirsReserve, theirsAlive,
                               theirsPerBody, theirsStart);
    if (!men)
    {
        static bool told = false;
        if (!told)
            core.Trace("HUD boarding: the script's counters make no crew (player %d+%d x %f of %d, enemy %d+%d x %f of "
                       "%d); the bars are not shown",
                       oursReserve, oursAlive, oursPerBody, oursStart, theirsReserve, theirsAlive, theirsPerBody,
                       theirsStart);
        told = true;
    }
    return men;
}

// The land abilities: the land menu's commands that activate a perk, named Activate<perk> by the scripts
// (landinterface.c: ActivateRush runs ActivateCharacterPerk(pchar, "Rush")), for perks the hero owns. `root` is the
// land interface (objLandInterface). Offered while the scripts enable the command or the perk's effect / delay
// runs; the enable is refreshed whenever the land menu is (android_land_commands.hpp LandRefreshGate).
inline std::vector<BattleAbility> readLandAbilities(ATTRIBUTES *root, ATTRIBUTES *hero)
{
    std::vector<BattleAbility> out;
    auto *data = static_cast<VDATA *>(core.GetScriptVariable("ChrPerksList"));
    auto *perks = data ? data->GetAClass() : nullptr;
    auto *commands = root ? root->GetAttributeClass("Commands") : nullptr;
    if (!commands || !hero || !perks)
        return out;
    const auto number = [](ATTRIBUTES *a, const std::string &path) -> std::optional<double> {
        const std::string text = landText(a, path.c_str());
        if (text.empty())
            return std::nullopt;
        const double value = std::strtod(text.c_str(), nullptr);
        return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
    };
    constexpr std::string_view kActivate = "Activate";
    for (size_t i = 0; i < commands->GetAttributesNum(); ++i)
    {
        auto *command = commands->GetAttributeClass(static_cast<uint32_t>(i));
        const std::string_view name = command ? std::string_view(command->GetThisName()) : std::string_view();
        if (name.size() <= kActivate.size() || name.substr(0, kActivate.size()) != kActivate)
            continue;
        const std::string perk(name.substr(kActivate.size()));
        const std::string own = "perks.list." + perk, base = "list." + perk;
        if (!perks->FindAClass(perks, base.c_str()) || !hero->FindAClass(hero, own.c_str()))
            continue;
        BattleAbility ability;
        ability.id = perk;
        ability.label = landText(command, "note");
        ability.command = landText(command, "event");
        battleAbilityTiming(ability, number(hero, own + ".active"), number(hero, own + ".delay"),
                            number(perks, base + ".TimeDuration"), number(perks, base + ".TimeDelay"),
                            number(command, "enable").value_or(0) != 0);
        if (ability.state != BattleAbility::Blocked)
            out.push_back(std::move(ability));
    }
    return out;
}

// `icons` is data.icons of the land interface; id0 is the hero; `root` the land interface.
inline void readLandStatus(ATTRIBUTES *root, ATTRIBUTES *icons, LandVitals &v)
{
    auto *heroIcon = icons ? icons->GetAttributeClass("id0") : nullptr;
    ATTRIBUTES *hero =
        heroIcon ? landCharacter(static_cast<int>(heroIcon->GetAttributeAsDword("chrindex", -1))) : nullptr;
    for (const auto &[name, path] :
         {std::pair{"poison", "chr_ai.poison"}, {"drunk", "chr_ai.drunk"}, {"healing", "chr_ai.hp_bottle"}})
        if (landNumber(hero, path) > 0.f)
            v.effects.emplace_back(name);
    if (landWalksOnly(landText(hero, "actions.run"), landText(hero, "actions.walk")))
        v.effects.emplace_back("walkonly");
    v.hero =
        landPortrait(heroIcon ? static_cast<int>(heroIcon->GetAttributeAsDword("chrindex", -1)) : -1, hero, heroIcon);
    v.potions = potions(hero);
    v.abilities = readLandAbilities(root, hero);
    for (size_t i = 0; icons && i < icons->GetAttributesNum(); ++i)
    {
        auto *icon = icons->GetAttributeClass(static_cast<uint32_t>(i));
        if (!icon || icon == heroIcon || std::string_view(icon->GetThisName()).substr(0, 2) != "id")
            continue;
        const int index = static_cast<int>(icon->GetAttributeAsDword("chrindex", -1));
        auto *officer = landCharacter(index);
        if (!officer)
            continue;
        const float health = std::clamp(icon->GetAttributeAsFloat("health", 0.f), 0.f, 1.f);
        const float energy = std::clamp(icon->GetAttributeAsFloat("energy", 0.f), 0.f, 1.f);
        v.officers.push_back({landPortrait(index, officer, icon), std::isfinite(health) ? health : 0.f,
                              std::isfinite(energy) ? energy : 0.f});
    }
    for (const auto id : core.GetEntityIds("NPCharacter"))
    {
        auto *entity = core.GetEntityPointer(id);
        ATTRIBUTES *captain = entity ? entity->AttributesPointer : nullptr;
        if (!captain || !captain->GetAttribute("CaptanId"))
            continue;
        const float hp = landNumber(captain, "chr_ai.hp"), max = landNumber(captain, "chr_ai.hp_max");
        if (hp <= 0.f || max <= 0.f)
            continue;
        const std::string name = landText(captain, "name"), last = landText(captain, "lastname");
        v.opponent = LandOpponent{last.empty() ? name : name + " " + last, std::clamp(hp / max, 0.f, 1.f)};
        break;
    }
    v.boarding = readLandBoarding(v.officers, v.opponent.has_value());
    const auto player = core.GetEntityId("Player");
    if (player != invalid_entity && core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsFightMode"))
    {
        const auto wait = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "RecoilWait");
        v.dash = std::clamp(static_cast<float>(wait) / 1000.f, 0.f, 1.f);
    }
}

} // namespace storm::android
#endif
