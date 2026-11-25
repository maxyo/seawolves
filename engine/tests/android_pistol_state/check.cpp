#include "platform/android_pistol_state.hpp"
#include <limits>

#include <catch2/catch.hpp>
using storm::android::pistolState;
TEST_CASE("android_pistol_state")
{
    using O = std::optional<double>;
    REQUIRE(pistolState(false, true, 8, 0).state == "no_weapon");
    REQUIRE(pistolState(true, false, 1, 0).state == "unknown");
    REQUIRE(pistolState(true, true, O{}, 1).state == "unknown");
    REQUIRE(pistolState(true, true, 1, O{}).state == "unknown");
    REQUIRE(pistolState(true, true, -1, 0).state == "unknown");
    REQUIRE(pistolState(true, true, std::numeric_limits<double>::quiet_NaN(), 0).state == "unknown");
    REQUIRE(pistolState(true, true, 1, std::numeric_limits<double>::infinity()).state == "unknown");
    REQUIRE(pistolState(true, true, 0, 1).state == "unloaded");
    REQUIRE(pistolState(true, true, .54, 1).progress == 10);
    REQUIRE(pistolState(true, true, .999999, 1).progress == 19);
    REQUIRE(pistolState(true, true, 1, 1).state == "loaded");
    REQUIRE(pistolState(true, true, 1.2, 1).progress == 20); // usable partial multi-shot reload
    REQUIRE(pistolState(true, true, 4, 0).state == "loaded");
}
