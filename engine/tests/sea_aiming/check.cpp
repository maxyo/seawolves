// The cannon range overlay's shades and the aiming camera's azimuth limits.
#include "cannons_range.hpp"
#include "fire_camera_view.hpp"

#include <catch2/catch.hpp>

using storm::sea_ai::CannonsRange;

TEST_CASE("a charged board keeps its colours, a reloading one blinks")
{
    const CannonsRange range{0x00FFFFFF, 0x19FFFFFF, 0x00FF0000, 0x4BFF0000, 2.0f, 1.0f, true};
    const auto charged = range.BoardShades(true, 0.7f);
    REQUIRE((charged.nearColor == 0x00FFFFFF && charged.edgeColor == 0x19FFFFFF));

    const auto alpha = [&](float seconds) { return range.BoardShades(false, seconds).edgeColor >> 24; };
    REQUIRE(alpha(0.0f) == 0);         // the rise begins
    REQUIRE(alpha(1.0f) == 38);        // half way up: 75 / 2, rounded
    REQUIRE(alpha(1.999f) == 75);      // the top
    REQUIRE(alpha(2.5f) == 38);        // half way down
    REQUIRE(alpha(3.0f + 1.0f) == 38); // the next period
    REQUIRE((range.BoardShades(false, 1.0f).edgeColor & 0xFFFFFF) == 0xFF0000);
    REQUIRE(range.BoardShades(false, 1.0f).nearColor == 0x00FF0000); // no alpha to blink
}

TEST_CASE("the aiming camera turns within an eighth of a turn of its locator")
{
    using namespace storm::sea_cameras;
    constexpr float kHalf = std::numbers::pi_v<float>;
    REQUIRE(AimStartAy(kHalf / 2 + 0.1f, kHalf / 2) == Approx(0.1f));          // the right board, looking just aft
    REQUIRE(AimStartAy(-kHalf / 2 + 0.2f, kHalf / 2) == Approx(-kAimAyLimit)); // the other side: as far as it turns
    REQUIRE(AimStartAy(kHalf / 2 - 2.0f * kHalf, kHalf / 2) == Approx(0.0f).margin(1e-5)); // a full turn apart
    REQUIRE(AimTurnAy(0.0f, 10.0f, 0.0015f, 1.0f, 0.8f) == Approx(0.024f));
    REQUIRE(AimTurnAy(0.7f, 1000.0f, 0.0015f, 1.0f, 0.8f) == Approx(kAimAyLimit));
    REQUIRE(AimTurnAy(-0.7f, -1000.0f, 0.0015f, 1.0f, 0.8f) == Approx(-kAimAyLimit));
}
