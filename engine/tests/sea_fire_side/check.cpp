// The player's camera fire picks a gun side over the full circle (fire_side.hpp): every view
// azimuth selects the nearest usable side, which aims inside its own zone.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "fire_side.hpp"

#include <catch2/catch.hpp>

namespace
{
using storm::fire_side::Side;
constexpr float kPi = 3.14159265358979f;

float deg(float d)
{
    return d * kPi / 180.0f;
}

// The stock ship sides (ships_init.c): bow 30°, starboard 80° at 90°, stern 30°, port 80° at 270°.
const Side kStock[4] = {{deg(0), deg(30)}, {deg(90), deg(80)}, {deg(180), deg(30)}, {deg(270), deg(80)}};

int pick(float lookDeg, unsigned usable = 0xF)
{
    return storm::fire_side::nearest(deg(lookDeg), 4, [&](int i) -> std::optional<Side> {
        if (!(usable & (1u << i)))
            return std::nullopt;
        return kStock[i];
    });
}

bool near(float a, float b)
{
    return std::fabs(std::remainder(a - b, 2 * kPi)) < 1e-4f;
}
} // namespace

TEST_CASE("sea_fire_side")
{
    // Inside a zone: that side, as the stock zone rule has it.
    REQUIRE((pick(0) == 0 && pick(10) == 0 && pick(90) == 1 && pick(125) == 1 && pick(180) == 2 &&
             pick(-90) == 3)); // a view inside a zone selects that side
    // The stock dead sectors (15°…50°, 130°…165° and their mirrors) now fire the nearest side.
    REQUIRE((pick(30) == 0 && pick(35) == 1 && pick(140) == 1 && pick(160) == 2 && pick(-30) == 0 &&
             pick(-40) == 3)); // a view between zones selects the side with the nearest zone edge
    // No dead zone: every azimuth over the full circle selects a side, and it aims inside its zone.
    for (int d = -360; d <= 360; ++d)
    {
        const int side = pick(static_cast<float>(d));
        INFO("no side at " + std::to_string(d) + "°");
        REQUIRE(side >= 0);
        const float aim = storm::fire_side::clampInto(deg(static_cast<float>(d)), kStock[side]);
        INFO("the aim leaves the zone at " + std::to_string(d) + "°");
        REQUIRE(storm::fire_side::gap(aim, kStock[side]) < 1e-4f);
        if (storm::fire_side::gap(deg(static_cast<float>(d)), kStock[side]) == 0.0f)
        {
            INFO("a view inside the zone is aimed as it is at " + std::to_string(d) + "°");
            REQUIRE(near(aim, deg(static_cast<float>(d))));
        }
    }
    // Clamped to the nearer edge.
    REQUIRE((near(storm::fire_side::clampInto(deg(35), kStock[1]), deg(50)) &&
             near(storm::fire_side::clampInto(deg(30), kStock[0]), deg(15)) &&
             near(storm::fire_side::clampInto(deg(-40), kStock[3]),
                  deg(-50)))); // the aim is the zone edge nearest to the view
    // A side that cannot fire is skipped: the bow without guns hands its sector to the broadsides.
    REQUIRE((pick(0, 0b1110) == 1 || pick(0, 0b1110) == 3));    // without bow guns the view ahead picks a broadside
    REQUIRE((pick(10, 0b1110) == 1 && pick(-10, 0b1110) == 3)); // the nearer broadside
    REQUIRE((pick(90, 0b0001) == 0 && pick(180, 0b0001) == 0)); // a single usable side covers the whole circle
    REQUIRE(pick(0, 0) == -1);                                  // no usable side: none fires
    std::puts("sea_fire_side: ok");
}
