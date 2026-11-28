// The sea orders adapter (android_sea_orders_source.hpp) on the real ship list across the end of a sea scene: the
// scripts delete the battle interface and the ships in one frame, the entity manager frees them, and only then does
// the touch HUD poll (AndroidGame::AfterFrame). The poll must not reach the freed ship, and the order still waiting
// for the gone scene is answered as refused.
#include "android_sea_orders_source.hpp"
#include "core_fake.hpp"
#include "ship_fake.hpp"

#include <catch2/catch.hpp>

#include <memory>

static storm::test::FakeCore fake;
Core &core = fake;
SHIP_DESCRIBE_LIST g_ShipList;

using namespace storm::android;

namespace
{
constexpr entid_t battleId = 5, shipId = 20;

// The battle interface as the adapter uses it: the scripts' command menu with the flagship as its one recipient.
struct Battle
{
    bool closed = false;
    entid_t GetId() const
    {
        return battleId;
    }
    bool TakeAndroidOrdersChanged()
    {
        return false;
    }
    bool AndroidCommandActive() const
    {
        return false;
    }
    bool AndroidOrdersShown() const
    {
        return true;
    }
    bool ReadAndroidOrders(SeaOrdersFrame &frame)
    {
        frame.available = true;
        frame.recipients.push_back({"r20_0", "Flagship", "20", 0, true});
        frame.recipientId = "r20_0";
        return true;
    }
    bool ExecuteAndroidOrder(const HudMenuRequest &)
    {
        return true;
    }
    void CloseAndroidOrders()
    {
        closed = true;
    }
};
} // namespace

TEST_CASE("an order pending when the sea scene ends is refused, the freed ship is not read")
{
    storm::test::Codec codec;
    ATTRIBUTES hero(codec), shipType(codec);
    hero.SetAttribute("index", "0");
    hero.CreateAttribute("Ship");
    fake.event = [](std::string_view) {}; // BI_GetData unanswered: the list's defaults

    Battle battle;
    auto ship = std::make_unique<storm::test::FakeShip>(shipId, &hero, &shipType);
    fake.entityPointers[battleId] = nullptr; // valid; the adapter is handed `battle` below
    fake.entityPointers[shipId] = ship.get();
    fake.entityLists["ship"] = {shipId};
    g_ShipList.Bind(battleId);
    g_ShipList.Add(0, 0, &hero, &shipType, true, true, 0, 0);

    SeaOrdersSource source{[&]() -> Battle * { return fake.IsEntityValid(battleId) ? &battle : nullptr; },
                           [] { return std::string("russian"); }, g_ShipList};
    source.poll(true);
    REQUIRE(seaOrders.frame().available);
    storm::ui::hud::MenuStep open{storm::ui::hud::MenuStep::Menu::Sea, seaOrders.revision(), "open", "", 41};
    REQUIRE(seaOrders.offer(open));

    // The frame's entity lifecycle frees both before the HUD's poll.
    fake.entityPointers.erase(battleId);
    fake.entityPointers.erase(shipId);
    fake.entityLists["ship"] = {};
    ship.reset();

    source.poll(true);
    CHECK_FALSE(seaOrders.frame().available);
    CHECK_FALSE(seaOrders.take());
    const auto answers = hudAnswers.take();
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].request == 41);
    CHECK_FALSE(answers[0].applied);
}
