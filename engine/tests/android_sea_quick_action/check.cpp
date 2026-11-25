#include "platform/android_sea_quick_action.hpp"
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;

static SeaQuickActionFrame frame(std::string action = "Map", std::string target = "sea:42")
{
    return {19, true, true, true, "main:17", std::move(action), "Native caption", std::move(target)};
}
// The HUD's tap on `action` of the offer it shows now.
static storm::ui::hud::SeaQuickTap tap(const SeaQuickActionState &state, const std::string &action)
{
    return {state.revision(), action};
}

TEST_CASE("android_sea_quick_action")
{
    SeaQuickActionState state;
    state.update(frame());
    REQUIRE(state.valid());
    REQUIRE((state.hud().available && state.hud().action == "Map" && state.hud().label == "Native caption"));

    // exactly one activation per offer: the tap's echo is stale
    REQUIRE(!state.offer(tap(state, "Moor")));
    const auto first = tap(state, "Map");
    REQUIRE((state.offer(first) && !state.offer(first)));
    REQUIRE((state.consume() == "Map" && !state.consume()));
    REQUIRE(!state.offer(first));
    REQUIRE(state.offer(tap(state, "Map")));
    // a tap on an offer the engine has replaced since does not act
    const auto onOld = tap(state, "Map");
    state.update(frame("Map", "sea:43"));
    REQUIRE((!state.consume() && !state.offer(onOld)));
    {
        SeaQuickActionState both;
        auto moor = frame("Moor");
        moor.map = true;
        moor.mapLabel = "Map caption";
        both.update(moor);
        REQUIRE((both.hud().action == "Moor" && both.hud().map && both.hud().map_label == "Map caption"));
        REQUIRE(!both.offer(tap(both, "Board")));
        REQUIRE((both.offer(tap(both, "Map")) && !both.offer(tap(both, "Moor"))));
        REQUIRE(both.consume() == "Map");
        REQUIRE((both.offer(tap(both, "Moor")) && both.consume() == "Moor"));
        // without the map permission Map is refused, and the state says so
        moor.map = false;
        both.update(moor);
        REQUIRE(!both.hud().map);
        REQUIRE(!both.offer(tap(both, "Map")));
    }
    for (const auto *id : {"Map", "Moor", "Board", "LandTroops", "Reload"})
    {
        SeaQuickActionState one;
        one.update(frame(id));
        REQUIRE((one.offer(tap(one, id)) && one.consume() == id));
    }
    for (const auto *id : {"Nothing", "Closed", "EnterToSea", "Unknown", ""})
    {
        SeaQuickActionState one;
        one.update(frame(id));
        REQUIRE((!one.valid() && !one.offer(tap(one, id))));
        REQUIRE((!one.hud().available && one.hud().action.empty()));
    }
    {
        auto incomplete = frame();
        incomplete.complete = false;
        SeaQuickActionState one;
        one.update(incomplete);
        REQUIRE((!one.valid() && !one.offer(tap(one, "Map"))));
    }
    std::cout << "sea quick action: exact single activation, stale offer and tap, unknown actions PASS\n";
}
