#pragma once
#ifdef STORM_TOUCH_UI
// The touch HUD's sea orders (platform/android_sea_orders.hpp) read from the battle interface and its ship list.
// They run after the frame's entity lifecycle (AndroidGame::AfterFrame): the battle interface is resolved by id on
// every call (`battle()`), and the ship list answers only while that interface lives (SHIP_DESCRIBE_LIST).
#include "ships_list.h"
#include <platform/android_sea_orders.hpp>

#include <string>
#include <utility>

namespace storm::android
{
// BattleOf: () -> the live battle interface or nullptr. LanguageOf: () -> the game's language or "".
template <class BattleOf, class LanguageOf> struct SeaOrdersSource
{
    BattleOf battle;
    LanguageOf language;
    const SHIP_DESCRIBE_LIST &ships;

    void refresh(bool ready)
    {
        auto *interface = battle();
        if (!ready || !interface)
        {
            if (interface && interface->AndroidCommandActive())
                interface->CloseAndroidOrders();
            seaOrders.update({}, {});
            return;
        }
        auto *main = ships.GetMainCharacterShip();
        if (!main || !main->pShip || main->isDead)
        {
            interface->CloseAndroidOrders();
            seaOrders.update({}, {});
            return;
        }
        const auto key = std::to_string(interface->GetId()) + ":" + std::to_string(main->pShip->GetId()) + ":" +
                         std::to_string(main->characterIndex);
        SeaOrdersFrame frame;
        frame.language = language();
        interface->ReadAndroidOrders(frame);
        if (!frame.available)
            interface->CloseAndroidOrders();
        seaOrders.update(key, std::move(frame));
    }
    // Refreshes when seaOrdersRefresh says so.
    void poll(bool ready)
    {
        auto *interface = battle();
        auto *main = ships.GetMainCharacterShip();
        const bool available = ready && interface && main && main->pShip && !main->isDead;
        const bool changed = interface && interface->TakeAndroidOrdersChanged();
        if (seaOrdersRefresh.due(available, interface ? interface->GetId() : 0,
                                 main && main->pShip ? main->pShip->GetId() : 0, main ? main->characterIndex : -1,
                                 interface && interface->AndroidCommandActive(),
                                 interface && interface->AndroidOrdersShown(), changed))
            refresh(ready);
    }
    bool execute(const HudMenuRequest &request)
    {
        const auto current = [&] {
            return request.scene == seaOrders.sceneEpoch() && request.revision == seaOrders.revision() &&
                   seaOrders.frame().available;
        };
        if (!current())
            return false;
        // The closed-menu snapshot is read only on invalidation. Read recipients
        // and command predicates now; never dispatch against an obsolete identity.
        refresh(true);
        if (!current())
            return false;
        auto *interface = battle();
        if (!interface)
            return false;
        const bool executed = interface->ExecuteAndroidOrder(request);
        // Even idempotent selection consumes this revision, preventing a duplicated
        // confirmation from running against another stage or dispatching twice.
        seaOrders.invalidate();
        seaOrdersRefresh.force();
        return executed;
    }
    void close()
    {
        if (auto *interface = battle())
            if (interface->AndroidCommandActive())
                interface->CloseAndroidOrders();
        seaOrders.update({}, {});
        seaOrdersRefresh.force();
    }
};
template <class BattleOf, class LanguageOf>
SeaOrdersSource(BattleOf, LanguageOf, const SHIP_DESCRIBE_LIST &) -> SeaOrdersSource<BattleOf, LanguageOf>;
} // namespace storm::android
#endif
