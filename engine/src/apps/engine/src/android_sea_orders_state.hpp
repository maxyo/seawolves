#pragma once
#ifdef STORM_TOUCH_UI
#include "android_gameplay_hold.hpp"
#include "android_ship_state.hpp"
#include "platform/android_sea_orders.hpp"

namespace storm::android
{
inline bool seaOrdersReady(bool ready)
{
    if (!ready)
        return false;
    const auto integer = [](const char *name) {
        auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
        int32_t result = 0;
        if (value)
            value->Get(result);
        return result;
    };
    if (!integer("bSeaActive") || integer("bAbordageStarted") || core.GetEntityId("WorldMap") != invalid_entity)
        return false;
    auto *character = shipMainCharacter();
    auto *ship = shipForCharacter(character);
    if (!character || !ship || ship->isDead() || !gameplayHold(character).empty())
        return false;
    auto *group = static_cast<VDATA *>(core.GetScriptVariable("curKeyGroupName"));
    const char *name = nullptr;
    if (!group || !group->Get(name) || !name)
        return false;
    const std::string_view current(name);
    // This is a separate order capability; normal gameplay remains blocked by
    // ComState and never receives movement/fire while the menu is active.
    return current == "Sailing1Pers" || current == "Sailing3Pers" || current == "BattleInterfaceControls";
}
} // namespace storm::android
#endif
