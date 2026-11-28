#pragma once
#ifdef STORM_TOUCH_UI
#include "android_gameplay_state.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_worldmap.hpp"
namespace storm::android
{
inline std::string mapText(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    return value && value->HasValue() ? scriptCaption(value->GetValue(), "worldMap") : std::string{};
}
// A script flag (bool or int attribute): absent or empty is false, as sti() of it; else 0 / 1.
inline std::optional<bool> mapFlag(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    if (!value || !value->HasValue() || value->GetValue().empty())
        return false;
    const auto number = shipNumberValue(value->GetValue());
    if (!number || (*number != 0 && *number != 1))
        return std::nullopt;
    return *number == 1;
}
// Each hour's Night flag from the scripts' Weathers, the record FindWeatherByHour takes for it: the first with that
// hour.min that is neither skipped nor a storm.
inline std::optional<std::pair<int, int>> readMapDayHours()
{
    auto *weathers = static_cast<VDATA *>(core.GetScriptVariable("Weathers"));
    if (!weathers)
        return std::nullopt;
    std::array<std::optional<bool>, 24> night;
    for (uint32_t n = 0; n < weathers->GetElementsNum(); ++n)
    {
        auto *record = weathers->GetAClass(n);
        const auto hour = shipNumber(record, "Hour.Min");
        if (!hour || mapFlag(record, "skip").value_or(true) || mapFlag(record, "Storm").value_or(true))
            continue;
        const int h = static_cast<int>(*hour);
        if (h >= 0 && h < 24 && !night[h])
            night[h] = mapFlag(record, "Night");
    }
    return mapDayHours(night);
}
inline WorldMapFrame readWorldMapFrame(const GameplayState &gameplay, bool ready)
{
    WorldMapFrame frame;
    if (!ready || gameplay.snapshot().mode != "worldmap" ||
        (!gameplaySnapshotCurrent() && readGameplaySnapshot(true).mode != "worldmap"))
        return frame;
    frame.owner = core.GetEntityId("WorldMap");
    auto *main = shipMainCharacter();
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    auto *profile = static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
    auto *profileAttrs = profile ? profile->GetAClass() : nullptr;
    auto *profileValue = profileAttrs ? profileAttrs->FindAClass(profileAttrs, "name") : nullptr;
    // Profile identity is exact, not a trimmed display caption.
    const std::string profileName = profileValue && profileValue->HasValue() ? profileValue->GetValue() : "";
    if (frame.owner == invalid_entity || !main || !characters || scriptCaption(profileName, "worldMap").empty())
        return {};
    frame.available = true;
    frame.gameplayRevision = gameplay.revision();
    frame.identity = std::to_string(frame.owner) + ":" + std::to_string(profileName.size()) + ":" + profileName + ":" +
                     shipText(main, "index");
    frame.context = worldMapContext(frame.owner);
    if (frame.context.empty())
        return {};
    if (worldMapHud.source.owner == frame.owner)
    {
        frame.instruments = worldMapHud.source;
        frame.instrumentsVisible = frame.instruments.visible;
        frame.instruments.dayHours = readMapDayHours();
    }
    auto *battle = core.Entity_GetAttributeClass(core.GetEntityId("WM_INTERFACE"), "wm_sign");
    auto *realShips = static_cast<VDATA *>(core.GetScriptVariable("RealShips"));
    unsigned sign = 1;
    // The signs follow WM_SetShipData: the hero, then his companion slots in order (companionSlots);
    // characters without a ship are not fleet signs.
    std::vector<double> members;
    if (const auto index = shipNumber(main, "index"))
        members.push_back(*index);
    for (const int index : companionSlots(main))
        members.push_back(index);
    for (size_t member = 0; member < members.size(); ++member)
    {
        const double index = members[member];
        if (std::floor(index) != index || index < 0 || index >= characters->GetElementsNum())
            continue;
        auto *character = characters->GetAClass(static_cast<int>(index));
        if (!character)
            continue;
        const auto type = shipNumber(character, "Ship.Type");
        if (member > 0 && (!type || std::floor(*type) != *type))
            continue;
        MapShipFrame ship;
        ship.name = mapText(character, "Ship.Name");
        const auto characterId = mapText(character, "id");
        if (!characterId.empty())
            ship.id =
                std::to_string(static_cast<int>(index)) + ":" + characterId + ":" + shipText(character, "Ship.Type");
        auto *badge = battle ? battle->GetAttributeClass(("sign" + std::to_string(sign++)).c_str()) : nullptr;
        if (const auto v = shipNumber(badge, "leftprogress"); v && *v <= 1)
            ship.hull = *v * 100;
        if (const auto v = shipNumber(badge, "rightprogress"); v && *v <= 1)
            ship.sails = *v * 100;
        if (const auto v = shipNumber(badge, "text"); v && *v <= 100000 && std::floor(*v) == *v)
            ship.crew = static_cast<int>(*v);
        if (auto *uv = badge ? badge->FindAClass(badge, "faceuv") : nullptr; uv && uv->HasValue() && battle)
        {
            std::array<float, 4> rect{};
            if (std::sscanf(uv->GetValue().c_str(), "%f,%f,%f,%f", &rect[0], &rect[1], &rect[2], &rect[3]) == 4)
                ship.picture = shipPicture(shipText(battle, "shiptexturename"), rect);
        }
        if (type && realShips && *type < realShips->GetElementsNum() && std::floor(*type) == *type)
        {
            auto *shipType = realShips->GetAClass(static_cast<int>(*type));
            ship.type = scriptCaption(shipCanonicalType(shipType), "worldMap");
            if (const auto c = shipNumber(shipType, "Class");
                c && *c >= 1 && *c <= std::numeric_limits<int>::max() && std::floor(*c) == *c)
                ship.shipClass = static_cast<int>(*c);
            if (const auto m = shipNumber(shipType, "MaxCrew"); m && *m >= 1 && *m <= 100000 && std::floor(*m) == *m)
                ship.crewMax = static_cast<int>(*m);
        }
        frame.identity += "|" + std::to_string(ship.id.size()) + ":" + ship.id + ":" +
                          std::to_string(ship.name.size()) + ":" + ship.name;
        frame.ships.push_back(std::move(ship));
    }
    frame.shipsComplete = battle && !battle->GetAttributeClass(("sign" + std::to_string(sign)).c_str());
    readWorldMapCommands(frame);
    // Respect the actual fast-action visibility as well as the command list.
    // Stock deliberately labels the island action EnterToSea in ILogAndActions.
    // An encounter offers several commands (attack, enter the storm, go to sea); the native
    // fast action must be one of them.
    const auto offered = [&](const std::string &id) {
        return std::any_of(frame.commands.begin(), frame.commands.end(),
                           [&](const MapCommand &c) { return c.id == id; });
    };
    if (worldMapHud.contextActionOwner != frame.owner || frame.commands.empty() ||
        (!offered(worldMapHud.contextAction) &&
         !(worldMapHud.contextAction == "EnterToSea" && offered("EnterToIsland"))))
        frame.commandsReady = false;
    if (core.GetEntityId("fader") != invalid_entity)
        frame.commandsReady = false;
    return frame;
}
} // namespace storm::android
#endif
