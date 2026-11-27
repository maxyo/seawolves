#include "picture_fan.hpp"

#include <catch2/catch.hpp>

using namespace storm::xi;
using Catch::Detail::Approx;

TEST_CASE("picture_fan.step_and_end")
{
    CHECK(FanStep(3.0f) == Approx(0.0523599f));
    CHECK(FanStep(0.0f) == Approx(0.0523599f)); // not positive: 3 degrees
    CHECK(FanStep(-5.0f) == Approx(0.0523599f));
    CHECK(FanStep(0.000001f) == Approx(0.00153398f)); // at least 1/4096 of a turn
    CHECK(FanStep(720.0f) == Approx(6.2831853f));     // at most a turn
    CHECK(FanEnd(1.0f, 0.5f) == 1.0f);                // not before the start
    CHECK(FanEnd(0.0f, 10.0f) == Approx(6.2831853f));
    CHECK(FanEnd(0.0f, 1.0f) == 1.0f);
}

TEST_CASE("picture_fan.vertices")
{
    // a quarter of the circle inscribed in 100 x 50, in two steps
    const auto fan = FanVertices(0, 0, 100, 50, 0, 0, 1, 1, 0.0f, 1.5707963f, 0.7853982f);
    REQUIRE(fan.size() == 4);
    CHECK(fan[0].x == 50.0f);
    CHECK(fan[0].y == 25.0f);
    CHECK(fan[0].u == 0.5f);
    CHECK(fan[1].x == Approx(75.0f));
    CHECK(fan[1].y == Approx(25.0f));
    CHECK(fan[1].u == Approx(1.0f));
    CHECK(fan[2].x == Approx(67.6777f));
    CHECK(fan[2].y == Approx(42.6777f));
    CHECK(fan[2].v == Approx(0.853553f));
    CHECK(fan[3].x == Approx(50.0f));
    CHECK(fan[3].y == Approx(50.0f));
    CHECK(fan[3].v == Approx(1.0f));

    // the last step ends at the arc's end
    const auto partial = FanVertices(0, 0, 100, 100, 0, 0, 1, 1, 0.0f, 0.5f, 0.3f);
    REQUIRE(partial.size() == 4);
    CHECK(partial[3].x == Approx(50.0f + 50.0f * 0.8775826f));

    CHECK(FanVertices(0, 0, 100, 100, 0, 0, 1, 1, 1.0f, 1.0f, 0.1f).empty());
}
