#pragma once
#ifdef __ANDROID__
// The ship's battle abilities a touch HUD offers (ShipAbilityNames): the perk commands of the scripts' battle
// interface (BattleInterface.c BI_InitializeCommands / procSetUsingAbility). Everything is read from the scripts'
// own state:
//   holder   the character whose perk counters run: the main character when it has the perk, otherwise the officer
//            of the perk's OfficerType (pchar.Fellows.Passengers.<type>), where CheckOfficersPerkWOSelf reads them;
//            none = not offered;
//   label    BattleInterface.Commands.<X>.note, the game's own name (AbilityDescribe.txt);
//   active   holder.perks.list.<X>.active counts down from ChrPerksList TimeDuration;
//   recovery holder.perks.list.<X>.delay counts down from TimeDelay while not active;
//   ready    BattleInterface.Commands.<X>.enable, which the scripts' procSetUsingAbility
//            computes (asked once a second, never inside a command menu: see refreshShipAbilities).
// A tap launches the ability exactly as the native command list does:
// Event("BI_LaunchCommand", "lsls", mainIndex, "BI_<X>", -1, "").
#include "platform/android_ship_telemetry.hpp"
#include "android_ship_state.hpp"
#include "core.h"
#include "v_data.h"

#include <chrono>
#include <optional>
#include <cmath>
#include <cstdlib>

namespace storm::android
{
namespace abilities
{
inline ATTRIBUTES *scriptObject(const char *name)
{
    auto *data = static_cast<VDATA *>(core.GetScriptVariable(name));
    return data ? data->GetAClass() : nullptr;
}
inline std::optional<double> number(ATTRIBUTES *attributes, const std::string &path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path.c_str()) : nullptr;
    return value && value->HasValue() ? shipNumberValue(value->GetValue()) : std::nullopt;
}
inline bool has(ATTRIBUTES *attributes, const std::string &path)
{
    return attributes && attributes->FindAClass(attributes, path.c_str());
}
// The character who holds the perk for the main character, or nullptr.
inline ATTRIBUTES *holder(ATTRIBUTES *main, ATTRIBUTES *perks, std::string_view name)
{
    const std::string perk = "perks.list." + std::string(name);
    if (has(main, perk))
        return main;
    auto *type = perks ? perks->FindAClass(perks, ("list." + std::string(name) + ".OfficerType").c_str()) : nullptr;
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!type || !type->HasValue() || !characters)
        return nullptr;
    const auto index = number(main, "Fellows.Passengers." + std::string(type->GetValue()));
    if (!index || *index < 0 || *index >= characters->GetElementsNum() || std::floor(*index) != *index)
        return nullptr;
    auto *officer = characters->GetAClass(static_cast<int>(*index));
    return has(officer, perk) ? officer : nullptr;
}
} // namespace abilities

// The abilities as the HUD shows them; empty outside a sea scene or without the scripts' data.
inline std::vector<BattleAbility> readShipAbilities()
{
    std::vector<BattleAbility> out;
    try
    {
        auto *main = shipMainCharacter();
        auto *perks = abilities::scriptObject("ChrPerksList");
        auto *battle = abilities::scriptObject("BattleInterface");
        if (!main || !perks || !battle)
            return out;
        for (const auto name : ShipAbilityNames)
        {
            auto *owner = abilities::holder(main, perks, name);
            if (!owner)
                continue;
            const std::string perk = "perks.list." + std::string(name), base = "list." + std::string(name),
                              command = "Commands." + std::string(name);
            BattleAbility ability;
            ability.id = std::string(name);
            auto *note = battle->FindAClass(battle, (command + ".note").c_str());
            ability.label = note && note->HasValue() ? note->GetValue() : std::string();
            battleAbilityTiming(
                ability, abilities::number(owner, perk + ".active"), abilities::number(owner, perk + ".delay"),
                abilities::number(perks, base + ".TimeDuration"), abilities::number(perks, base + ".TimeDelay"),
                abilities::number(battle, command + ".enable").value_or(0) != 0);
            out.push_back(std::move(ability));
        }
    }
    catch (...)
    {
        static HudReadError error("battle abilities");
        error.report(std::current_exception(), "no ability buttons"); // missing or incompatible scripts
        out.clear();
    }
    return out;
}

// Asks the scripts which abilities can be used now (procSetUsingAbility, the menu's own ask). The original asks
// only when its command menu opens and shows readiness only there; the touch HUD shows the buttons all the time and
// the enable has no script-side signal (a hit, an emptied hold, a delay running out), so it is asked once a second:
// the scripts' perk counters move once a second (evntChrPerkDelay). Inside the command menu the menu asks itself;
// right after it the HUD asks again.
inline void refreshShipAbilities(bool commandMenuOpen)
{
    using Clock = std::chrono::steady_clock;
    static std::optional<Clock::time_point> asked;
    const auto now = Clock::now();
    if (commandMenuOpen)
    {
        asked.reset();
        return;
    }
    if (asked && now - *asked < std::chrono::seconds(1))
        return;
    auto *main = shipMainCharacter();
    const auto index = main ? abilities::number(main, "index") : std::nullopt;
    if (!index || *index < 0)
        return;
    asked = now;
    core.Event("evntSetUsingAbility", "l", static_cast<int32_t>(*index));
}

// A tap: the ability is launched only while the scripts call it ready.
inline bool launchShipAbility(std::string_view control)
{
    for (const auto name : ShipAbilityNames)
    {
        if (control != "BI_" + std::string(name))
            continue;
        for (const auto &ability : readShipAbilities())
            if (ability.id == name && ability.state == BattleAbility::Ready)
            {
                const auto index = abilities::number(shipMainCharacter(), "index");
                if (!index)
                    return false;
                core.Event("BI_LaunchCommand", "lsls", static_cast<int32_t>(*index), std::string(control).c_str(), -1,
                           "");
                return true;
            }
        return false;
    }
    return false;
}
// The ship telemetry with the abilities; the scripts recompute what can be used first (once a second).
inline ShipTelemetryFrame readShipFrame(const GameplayState &gameplay, bool commandMenuOpen)
{
    auto frame = readShipTelemetry(gameplay);
    if (frame.valid)
    {
        refreshShipAbilities(commandMenuOpen);
        frame.abilities = readShipAbilities();
    }
    return frame;
}
} // namespace storm::android
#endif
