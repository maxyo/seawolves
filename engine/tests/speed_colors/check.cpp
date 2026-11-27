#include "speed_colors.hpp"

#include <catch2/catch.hpp>

using namespace storm;

TEST_CASE("speed_colors.gradient")
{
    // Red at standstill to green at full speed through yellow at half.
    const SpeedColorStop stops[] = {{0.0f, 0xFFFF0000u}, {0.5f, 0xFFFFFF00u}, {1.0f, 0xFF00FF00u}};
    const SpeedColorTable table = BuildSpeedColors(stops);
    REQUIRE(table.front() == 0xFFFF0000u);
    REQUIRE(table.back() == 0xFF00FF00u);
    const uint32_t quarter = table[kSpeedColors / 4];
    REQUIRE((quarter >> 16 & 0xFF) == 0xFF);
    REQUIRE((quarter >> 8 & 0xFF) > 0x60);
    REQUIRE((quarter >> 8 & 0xFF) < 0xA0);
    REQUIRE((quarter & 0xFF) == 0);
}

TEST_CASE("speed_colors.ends")
{
    // Outside the stops: the colour of the nearest one.
    const SpeedColorStop stops[] = {{0.25f, 0x80102030u}, {0.75f, 0x80405060u}};
    const SpeedColorTable table = BuildSpeedColors(stops);
    REQUIRE(table.front() == 0x80102030u);
    REQUIRE(table.back() == 0x80405060u);
}

TEST_CASE("speed_colors.invalid")
{
    const SpeedColorStop one[] = {{0.0f, 0xFF000000u}};
    const SpeedColorStop negative[] = {{-0.1f, 0xFF000000u}, {1.0f, 0xFF000000u}};
    for (const SpeedColorTable &table : {BuildSpeedColors(one), BuildSpeedColors(negative)})
        REQUIRE(table[kSpeedColors / 2] == 0xFFFFFFFFu);
}
