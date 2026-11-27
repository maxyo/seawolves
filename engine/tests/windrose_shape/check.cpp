#include "windrose_shape.hpp"

#include <catch2/catch.hpp>

#include <array>

using namespace storm::xi;
using Catch::Detail::Approx;

TEST_CASE("windrose_shape.outline")
{
    const std::array<float, 4> speeds{1.0f, 0.5f, 1.0f, 0.5f};
    const auto outline = WindRoseOutline(100, 100, 10, speeds);
    REQUIRE(outline.size() == 5);
    CHECK(outline[0].x == Approx(100.0f)); // heading 0 up
    CHECK(outline[0].y == Approx(90.0f));
    CHECK(outline[1].x == Approx(105.0f)); // a quarter clockwise, half the speed
    CHECK(outline[1].y == Approx(100.0f));
    CHECK(outline[2].y == Approx(110.0f));
    CHECK(outline[3].x == Approx(95.0f));
    CHECK(outline[4].x == outline[0].x); // closed
    CHECK(outline[4].y == outline[0].y);
    CHECK(WindRoseOutline(0, 0, 10, std::span<const float>{}).empty());
}

TEST_CASE("windrose_shape.ring")
{
    const std::array<float, 2> speeds{0.2f, 0.8f};
    const auto ring = WindRoseRing(0, 0, 10, 20, speeds, [](float s) { return s > 0.5f ? 2u : 1u; });
    REQUIRE(ring.size() == 12);
    CHECK(ring[0].y == Approx(-10.0f)); // inner edge of heading 0
    CHECK(ring[1].y == Approx(-20.0f)); // outer
    CHECK(ring[0].color == 1u);
    CHECK(ring[2].y == Approx(10.0f)); // the next heading, in its colour
    CHECK(ring[2].color == 2u);
    CHECK(ring[8].y == Approx(-10.0f)); // the last segment closes on heading 0
    CHECK(ring[8].color == 1u);
}
