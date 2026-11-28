// Where an NPC's move ends (location/src/npc_navigation.hpp): the stop radius of "Goto point" and the arrival in an
// area of the area tasks.
#include "npc_navigation.hpp"

#include <catch2/catch.hpp>

#include <limits>

using namespace storm::location;

TEST_CASE("a goto stops at the scripts' radius when it is a positive number, else at the engine's")
{
    CHECK(GotoStopRadius(1.25f, 0.3f) == 1.25f);
    CHECK(GotoStopRadius(-1.0f, 0.3f) == 0.3f); // the scripts' "no radius"
    CHECK(GotoStopRadius(0.0f, 0.3f) == 0.3f);
    CHECK(GotoStopRadius(std::numeric_limits<float>::quiet_NaN(), 0.3f) == 0.3f);
    CHECK(GotoStopRadius(std::numeric_limits<float>::infinity(), 0.3f) == 0.3f);
}

TEST_CASE("an area is reached anywhere within its radius on the plane, on the centre's level")
{
    const glm::vec3 centre(10.0f, 2.0f, -4.0f);
    CHECK(InsideArea({13.0f, 2.0f, 0.0f}, centre, 5.0f, 1.8f)); // 5 m away: on the edge
    CHECK(InsideArea({10.0f, 3.5f, -4.0f}, centre, 5.0f, 1.8f));
    CHECK_FALSE(InsideArea({13.1f, 2.0f, 0.0f}, centre, 5.0f, 1.8f));
    CHECK_FALSE(InsideArea({10.0f, 4.0f, -4.0f}, centre, 5.0f, 1.8f)); // a floor above
    CHECK_FALSE(InsideArea({10.0f, 0.1f, -4.0f}, centre, 5.0f, 1.8f)); // a floor below
}

TEST_CASE("no arrival in an area without a valid radius or at an invalid place")
{
    const glm::vec3 centre(0.0f, 0.0f, 0.0f);
    CHECK_FALSE(InsideArea(centre, centre, 0.0f, 1.8f));
    CHECK_FALSE(InsideArea(centre, centre, -2.0f, 1.8f));
    CHECK_FALSE(InsideArea(centre, centre, std::numeric_limits<float>::quiet_NaN(), 1.8f));
    CHECK_FALSE(InsideArea({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, centre, 3.0f, 1.8f));
}
