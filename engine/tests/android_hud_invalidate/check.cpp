// When the touch HUD asks the scripts for the land menu again (LandRefreshGate, android_land_commands.hpp).
#include "platform/android_land_commands.hpp"
#include <cstdio>

#include <catch2/catch.hpp>

using storm::android::LandRefreshGate;

TEST_CASE("android_hud_invalidate")
{
    LandRefreshGate gate;
    // The first poll asks: nothing has been read yet.
    REQUIRE(gate.due(true, 7, false, false));
    // Nothing invalidated: never again, however many loop iterations pass.
    for (int i = 0; i < 10000; ++i)
        REQUIRE(!gate.due(true, 7, false, false));
    // The land interface was told its list changed (update message, menu opened, command executed).
    REQUIRE(gate.due(true, 7, true, false));
    REQUIRE(!gate.due(true, 7, false, false));
    // Readiness changes both ways ask once each.
    REQUIRE(gate.due(false, 7, false, false));
    REQUIRE(!gate.due(false, 7, false, false));
    REQUIRE(gate.due(true, 7, false, false));
    REQUIRE(!gate.due(true, 7, false, false));
    // Another land interface entity (a new location) asks once; losing it too.
    REQUIRE(gate.due(true, 8, false, false));
    REQUIRE(!gate.due(true, 8, false, false));
    REQUIRE(gate.due(true, 0, false, false));
    REQUIRE(!gate.due(true, 0, false, false));
    // the hero draws or sheathes his weapon: the offered commands change (no transitions in a fight)
    REQUIRE(gate.due(true, 0, false, true));
    REQUIRE(!gate.due(true, 0, false, true));
    REQUIRE(gate.due(true, 0, false, false));
    // force(): the menu was emptied natively (closeLandCommands); the next poll asks once.
    gate.force();
    REQUIRE(gate.due(true, 0, false, false));
    REQUIRE(!gate.due(true, 0, false, false));
    std::puts("android_hud_invalidate: ok");
}
