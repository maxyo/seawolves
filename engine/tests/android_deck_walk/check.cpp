#include "deck_walk.hpp"
#include "platform/android_deck_walk.hpp"
#include <cmath>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
using Walk = storm::ui::hud::DeckWalk;

TEST_CASE("android_deck_walk")
{
    DeckWalk walk;
    bool allowed = true;
    walk.guard = [&] { return allowed; };
    walk.update(7);
    const auto epoch = [] { return combatCancelEpoch.load(); };
    const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

    // nothing until the stick moves; a walk forward and to the side, as far as it is pushed
    REQUIRE(!walk.read());
    REQUIRE(walk.offer(Walk{7, 40, 0}, epoch()));
    auto step = walk.read();
    REQUIRE((step && near(step->x, 0.4f) && near(step->y, 0.0f)));
    REQUIRE(walk.offer(Walk{7, -38, 38}, epoch()));
    step = walk.read();
    REQUIRE((step && near(step->x, -0.38f) && near(step->y, 0.38f)));
    // held through any number of frames, until its stop
    for (int frame = 0; frame < 1000; ++frame)
        REQUIRE(walk.read());
    REQUIRE((walk.offer(Walk{7, 0, 0}, epoch()) && !walk.read()));

    // a diagonal rounded a little past 100 stays inside the unit circle
    REQUIRE(walk.offer(Walk{7, 71, 71}, epoch()));
    step = walk.read();
    REQUIRE((step && std::hypot(step->x, step->y) <= 1.0f + 1e-5f));

    // a new walk is taken in the current revision only; the stop counts in any
    walk.update(8);
    REQUIRE(walk.read()); // a taken walk carries on
    REQUIRE((walk.offer(Walk{7, 0, 0}, epoch()) && !walk.read()));
    REQUIRE((!walk.offer(Walk{7, 0, 100}, epoch()) && walk.offer(Walk{8, 0, 100}, epoch())));

    // a failing guard and a cancelled input stop it
    allowed = false;
    REQUIRE(!walk.read());
    REQUIRE(!walk.offer(Walk{8, 0, 100}, epoch()));
    allowed = true;
    REQUIRE((walk.offer(Walk{8, 0, -100}, epoch()) && walk.read()));
    const auto before = epoch();
    cancelAndroidCombatInput();
    REQUIRE(!walk.read());
    REQUIRE(!walk.offer(Walk{8, 0, 100}, before)); // sent before the cancellation
    REQUIRE(walk.offer(Walk{8, 0, 100}, epoch()));

    // Beyond the stick's travel, or longer than it.
    for (const Walk &bad : {Walk{8, 101, 0}, Walk{8, 0, -101}, Walk{8, 80, 80}})
        REQUIRE(!walk.offer(bad, epoch()));
    DeckWalk unguarded;
    unguarded.update(1);
    REQUIRE((!unguarded.offer(Walk{1, 0, 50}, epoch()) && !unguarded.read()));
    // the walker's pace is real time: x1…x8 of the game's time scale walk the same distance a
    // frame; frozen time stands still
    for (const uint32_t scale : {1u, 2u, 4u, 8u})
        REQUIRE(deckWalkTime(16u * scale, 16u) == 16u);
    REQUIRE(deckWalkTime(0u, 16u) == 0u);

    // the way: ahead along the view, sideways to its right, the length as pushed
    const float quarter = 1.57079633f;
    auto way = deckWalkShift(0.0f, 1.0f, 0.0f, 0.0f);
    REQUIRE((near(way.x, 0.0f) && near(way.z, 1.0f))); // looking along +z, ahead is +z
    way = deckWalkShift(1.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE((near(way.x, 1.0f) && near(way.z, 0.0f))); // the right is +x
    way = deckWalkShift(1.0f, 0.0f, 0.0f, quarter);
    REQUIRE((near(way.x, 0.0f) && near(way.z, -1.0f))); // turned right (+x ahead), the right is -z
    way = deckWalkShift(0.0f, 1.0f, 0.5f, 0.0f);
    REQUIRE(near(way.z, std::cos(0.5f))); // ahead slower while looking down, as the original walk
    way = deckWalkShift(0.0f, -0.4f, 0.0f, 0.0f);
    REQUIRE(near(way.z, -0.4f)); // back, as far as pushed
    way = deckWalkShift(-0.6f, 0.8f, 0.0f, 0.7f);
    REQUIRE(near(std::hypot(way.x, way.z), 1.0f)); // a full push in any direction is a full step

    std::cout << "deck walk: real-time pace under time acceleration, the way ahead and sideways, analog vector, held "
                 "until stopped, revision, guard, cancellation, bounds, malformed requests PASS\n";
}
