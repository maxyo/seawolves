#include "platform/android_held_controls.hpp"
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
using storm::ui::hud::Hold;

TEST_CASE("android_held_controls")
{
    HeldControls held;
    bool allowed = true;
    held.guard = [&](std::string_view control) { return allowed && control != "WMapShipSailUp"; };
    held.update(7);
    const auto state = [&](const char *control) { return held.merge(control, 0); };

    // activated, active, released: no repetition needed
    REQUIRE(held.offer(Hold{7, "Ship_TurnLeft", true, 0}, 0));
    held.advance(0);
    REQUIRE((state("Ship_TurnLeft") == 3 && state("Ship_TurnRight") == 0));
    held.advance(0);
    REQUIRE(state("Ship_TurnLeft") == 1);
    REQUIRE(held.offer(Hold{7, "Ship_TurnLeft", true, 0}, 0));
    held.advance(0);
    REQUIRE(state("Ship_TurnLeft") == 1);
    REQUIRE(held.offer(Hold{7, "Ship_TurnLeft", false, 0}, 0));
    held.advance(0);
    REQUIRE(state("Ship_TurnLeft") == 2);
    held.advance(0);
    REQUIRE(state("Ship_TurnLeft") == 0);

    // a tap shorter than a frame (taken and let go between two frames) still activates once
    REQUIRE(
        (held.offer(Hold{7, "WMapCameraSwitch", true, 0}, 0) && held.offer(Hold{7, "WMapCameraSwitch", false, 0}, 0)));
    held.advance(0);
    REQUIRE(state("WMapCameraSwitch") == 3);
    held.advance(0);
    REQUIRE(state("WMapCameraSwitch") == 2);
    held.advance(0);
    REQUIRE(state("WMapCameraSwitch") == 0);

    // the spyglass is a toggle of taps (TelescopeIn / TelescopeOut, android_gameplay.hpp), not a hold
    REQUIRE(!held.offer(Hold{7, "TelescopeIn", true, 0}, 0));

    // Silence does not let go (event model): held through any number of frames, slow or fast,
    // until its "0".
    REQUIRE(held.offer(Hold{7, "Ship_TurnRight", true, 0}, 0));
    for (int frame = 0; frame < 1000; ++frame)
        held.advance(0);
    REQUIRE((state("Ship_TurnRight") == 1 && held.holding("Ship_TurnRight")));
    REQUIRE(held.offer(Hold{7, "Ship_TurnRight", false, 0}, 0));
    held.advance(0);
    REQUIRE(state("Ship_TurnRight") == 2);

    // a physical key keeps priority; an unknown control is left alone
    REQUIRE(held.offer(Hold{7, "Ship_TurnRight", true, 0}, 0));
    held.advance(0);
    REQUIRE(
        (held.merge("Ship_TurnRight", 1) == 1 && held.merge("Ship_Fire", 0) == 0 && held.merge("Ship_Fire", 3) == 3));

    // another gameplay revision keeps a taken hold (the guard decides) but refuses a new one made
    // in the old revision; a cancelled input and a control the gameplay does not bind let go
    held.update(8);
    held.advance(0);
    REQUIRE(state("Ship_TurnRight") == 1);
    REQUIRE(held.offer(Hold{7, "Ship_TurnRight", false, 0}, 0)); // the release always counts
    held.advance(0);
    REQUIRE((!held.offer(Hold{7, "Ship_TurnRight", true, 0}, 0) && held.offer(Hold{8, "Ship_TurnRight", true, 0}, 0)));
    held.advance(1);
    REQUIRE(!held.holding("Ship_TurnRight"));
    REQUIRE(
        (!held.offer(Hold{8, "WMapShipSailUp", true, 0}, 1) && held.offer(Hold{8, "WMapShipSailDown", true, 0}, 1)));
    allowed = false;
    held.advance(1);
    REQUIRE(!held.holding("WMapShipSailDown"));
    allowed = true;

    // the analog part: the helm's deflection rides on the hold as fValue (strength / 100); a
    // new strength changes it without restarting the activation; a plain hold is all the way
    REQUIRE(held.strength("Ship_TurnLeft") < 0.0f);
    REQUIRE(held.offer(Hold{8, "Ship_TurnLeft", true, 40}, 1));
    held.advance(1);
    REQUIRE((state("Ship_TurnLeft") == 3 && held.strength("Ship_TurnLeft") == 0.4f));
    REQUIRE(held.offer(Hold{8, "Ship_TurnLeft", true, 75}, 1));
    held.advance(1);
    REQUIRE((state("Ship_TurnLeft") == 1 && held.strength("Ship_TurnLeft") == 0.75f));
    REQUIRE(held.offer(Hold{8, "Ship_TurnLeft", true, 0}, 1));
    held.advance(1);
    REQUIRE(held.strength("Ship_TurnLeft") == 1.0f);
    REQUIRE(held.offer(Hold{8, "Ship_TurnLeft", false, 0}, 1));
    held.advance(1);
    REQUIRE((state("Ship_TurnLeft") == 2 && held.strength("Ship_TurnLeft") < 0.0f));
    held.advance(1);

    // Not a held control, a strength beyond 1…100, a revision the gameplay has left.
    for (const Hold &bad :
         {Hold{8, "Ship_Fire", true, 0}, Hold{8, "ChrForward", true, 0}, Hold{8, "DeckCamera_Forward", true, 0},
          Hold{8, "Ship_TurnLeft", true, 101}, Hold{8, "Ship_TurnLeft", true, -5}, Hold{7, "Ship_TurnLeft", true, 0}})
        REQUIRE(!held.offer(bad, 1));
    HeldControls unguarded;
    unguarded.update(1);
    REQUIRE(!unguarded.offer(Hold{1, "Ship_TurnLeft", true, 0}, 0));
    std::cout << "held controls: activation phases, held through silence until released, priority, revision, "
                 "cancellation, guard, analog strength, malformed requests PASS\n";
}
