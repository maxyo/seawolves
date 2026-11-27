#pragma once
#ifdef __ANDROID__
#include "platform/android_ship_telemetry.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_sea_orders.hpp"
#include "android_hud_read_error.hpp"
#include "core.h"
#include "v_data.h"
#include "ship_base.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace storm::android
{
inline ATTRIBUTES *shipMainCharacter()
{
    auto *index = static_cast<VDATA *>(core.GetScriptVariable("nMainCharacterIndex"));
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    int32_t number = -1;
    if (!index || !index->Get(number) || !characters || number < 0 ||
        static_cast<uint32_t>(number) >= characters->GetElementsNum())
        return nullptr;
    return characters->GetAClass(number);
}
inline SHIP_BASE *shipForCharacter(ATTRIBUTES *character)
{
    if (!character)
        return nullptr;
    for (const auto id : core.GetEntityIds("ship"))
        if (auto *ship = static_cast<SHIP_BASE *>(core.GetEntityPointer(id)))
            if (ship->GetACharacter() == character && ship->GetAShip())
                return ship;
    return nullptr;
}
inline std::optional<double> shipNumber(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    if (!value || !value->HasValue())
        return {};
    return shipNumberValue(value->GetValue());
}
inline std::string shipText(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    if (!value || !value->HasValue())
        return {};
    return scriptText(value->GetValue(), "ship");
}
// The companions the scripts keep in the character's slots (Fellows.Companions.id<slot>, written by
// SetCompanionIndex within the game's COMPANION_MAX), in slot order: the character indices of the slots that
// hold one (GetCompanionIndex: an index below 1 is an empty slot). Slot 0, the character himself, is not stored.
inline std::vector<int> companionSlots(ATTRIBUTES *character)
{
    auto *companions = character ? character->FindAClass(character, "Fellows.Companions") : nullptr;
    std::vector<std::pair<int, int>> slots;
    for (uint32_t i = 0; companions && i < companions->GetAttributesNum(); ++i)
    {
        auto *slot = companions->GetAttributeClass(i);
        const std::string_view name = slot->GetThisName();
        int number = 0;
        if (!name.starts_with("id") ||
            std::from_chars(name.data() + 2, name.data() + name.size(), number).ptr != name.data() + name.size())
            continue;
        const auto index = slot->HasValue() ? shipNumberValue(slot->GetValue()) : std::nullopt;
        if (index && *index >= 1 && std::floor(*index) == *index && *index <= std::numeric_limits<int>::max())
            slots.emplace_back(number, static_cast<int>(*index));
    }
    std::sort(slots.begin(), slots.end());
    std::vector<int> out;
    for (const auto &[slot, index] : slots)
        out.push_back(index);
    return out;
}
// Stock CreateBaseShip preserves the canonical type before adding a model-folder suffix.
inline std::string shipCanonicalType(ATTRIBUTES *attributes)
{
    auto base = shipText(attributes, "BaseName");
    return base.empty() ? shipText(attributes, "Name") : base;
}
inline ShipTelemetryEntry shipEntry(SHIP_BASE *ship)
{
    ShipTelemetryEntry entry;
    auto *character = ship->GetACharacter();
    auto *type = ship->GetAShip(); // Live RealShips attributes, not cached battle-interface maxima.
    entry.entityId = std::to_string(ship->GetId());
    entry.characterIndex = static_cast<int>(character->GetAttributeAsDword("index", -1));
    entry.name = shipText(character, "Ship.Name");
    entry.type = shipCanonicalType(type);
    if (const auto value = shipNumber(type, "Class");
        value && *value >= 1 && *value <= std::numeric_limits<int32_t>::max() && std::floor(*value) == *value)
        entry.shipClass = static_cast<int32_t>(*value);
    const auto metric = [character, type](const char *currentPath,
                                          const char *maximumPath) -> std::optional<ShipMetric> {
        const auto current = shipNumber(character, currentPath), maximum = shipNumber(type, maximumPath);
        return current && maximum ? shipMetric(*current, *maximum) : std::nullopt;
    };
    entry.hull = metric("Ship.HP", "HP");
    entry.crew = metric("Ship.Crew.Quantity", "MaxCrew");
    entry.sails = metric("Ship.SP", "SP");
    entry.picture = seaShipPicture(entry.characterIndex);
    return entry;
}
inline bool shipSailControlAllowed(SHIP_BASE *ship, const GameplayState &gameplay)
{
    if (!ship || !ship->GetACharacter())
        return false;
    const auto &mode = gameplay.snapshot().mode;
    auto *character = ship->GetACharacter();
    auto *stopped = character->FindAClass(character, "Ship.Stopped");
    auto *canDrop = character->GetAttributeClass("CanDropSails");
    ShipSailGuard guard;
    guard.sea = mode == "sailing" || mode == "naval";
    guard.up = gameplay.allows("Ship_SailUp");
    guard.down = gameplay.allows("Ship_SailDown");
    guard.mainCharacter = character->GetAttributeAsDword("MainCharacter", 0) != 0;
    guard.stopped = stopped && stopped->GetAttributeAsDword();
    guard.dead = ship->isDead();
    guard.canDropSails = !canDrop || canDrop->GetAttributeAsDword() != 0;
    guard.state = ship->GetSailState();
    return shipSailControlAllowed(guard);
}
inline ShipTelemetryFrame readShipTelemetry(const GameplayState &gameplay)
{
    ShipTelemetryFrame out;
    const auto &mode = gameplay.snapshot().mode;
    // The open sea command menu blocks gameplay (mode "unknown") but the ship is still at sea:
    // a touch HUD keeps its bars under the orders panel and covers the engine's own.
    if (mode != "sailing" && mode != "naval" && !seaOrders.frame().active)
        return out;
    try
    {
        auto *character = shipMainCharacter();
        auto *ship = shipForCharacter(character);
        if (!ship)
            return out;
        out.valid = true;
        out.ship = shipEntry(ship);
        readShipCaptions(out, ship->GetId());
        auto *powder = character->FindAClass(character, "Ship.Cargo.Goods.powder");
        if (powder && powder->HasValue())
            out.powderCount = shipPowderCount(powder->GetValue());
        const auto sailState = ship->GetSailState();
        if (std::isfinite(sailState) && sailState >= 0 && sailState <= 1)
            out.sailState = sailState;
        out.canSetSails = shipSailControlAllowed(ship, gameplay);
        if (const float scale = core.GetTimeScale(); std::isfinite(scale) && scale >= 0.f && scale <= 100.f)
            out.timeScale = std::round(scale * 100.0) / 100.0;
        for (size_t i = 0; i < storm::ui::hud::Ship::kAmmo.size(); ++i)
        {
            const std::string path = std::string("Ship.Cargo.Goods.") + storm::ui::hud::Ship::kAmmo[i];
            auto *cargo = character->FindAClass(character, path.c_str());
            if (cargo && cargo->HasValue())
                out.ammoCounts[i] = shipAmmoCount(cargo->GetValue());
        }
        const auto ammoType = shipNumber(character, "Ship.Cannons.Charge.Type");
        auto *goods = static_cast<VDATA *>(core.GetScriptVariable("Goods"));
        if (ammoType && goods && *ammoType < goods->GetElementsNum() && std::floor(*ammoType) == *ammoType)
        {
            const auto index = static_cast<uint32_t>(*ammoType);
            const auto name = storm::AsciiLower(shipText(goods->GetAClass(index), "Name"));
            if (std::find(storm::ui::hud::Ship::kAmmo.begin(), storm::ui::hud::Ship::kAmmo.end(), name) !=
                storm::ui::hud::Ship::kAmmo.end())
            {
                out.ammo = name;
                out.ammoType = static_cast<int>(index);
                out.ammoCount = shipNumber(character, ("Ship.Cargo.Goods." + name).c_str());
            }
        }
        // Original AddShipToInterface marks myShip from Fellows.Companions, not
        // relation/faction. Export only live companions, never remote stale stats.
        {
            std::vector<int> added;
            for (const int index : companionSlots(character))
            {
                if (index == out.ship.characterIndex || std::find(added.begin(), added.end(), index) != added.end())
                    continue;
                for (const auto id : core.GetEntityIds("ship"))
                {
                    auto *companion = static_cast<SHIP_BASE *>(core.GetEntityPointer(id));
                    auto *owner = companion ? companion->GetACharacter() : nullptr;
                    if (owner && companion->GetAShip() &&
                        static_cast<int>(owner->GetAttributeAsDword("index", -1)) == index)
                    {
                        out.fleet.push_back(shipEntry(companion));
                        added.push_back(index);
                        break;
                    }
                }
            }
        }
    }
    catch (...)
    {
        static HudReadError error("ship telemetry");
        error.report(std::current_exception(), "no ship bars");
        return {};
    }
    return out;
}
inline bool applyShipSailTarget(const GameplayState &gameplay, int detent)
{
    if (detent < 0 || detent > 2)
        return false;
    auto *ship = shipForCharacter(shipMainCharacter());
    if (!shipSailControlAllowed(ship, gameplay))
        return false;
    // The same native state and canonical 0/.5/1 positions as W/S in SHIP::Execute.
    // An arbitrary analog fraction would bypass the script's discrete sail tuning.
    ship->SetSailState(detent * 0.5f);
    return true;
}
} // namespace storm::android
#endif
