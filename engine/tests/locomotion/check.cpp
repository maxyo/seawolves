// Directional locomotion (src/libs/location/src/locomotion.hpp): which direction an input asks for, where a direction
// takes the mover, the action names the scripts set, the clips' speed curves and the legs' state.
#include "locomotion.hpp"

#include <cmath>

#include <catch2/catch.hpp>

using namespace storm::location;

TEST_CASE("locomotion.keys")
{
    REQUIRE(directionFromKeys(false, false, false, false) == MoveDir::none);
    REQUIRE(directionFromKeys(true, false, false, false) == MoveDir::forward);
    REQUIRE(directionFromKeys(false, true, false, false) == MoveDir::back);
    REQUIRE(directionFromKeys(false, false, true, false) == MoveDir::right);
    REQUIRE(directionFromKeys(true, false, false, true) == MoveDir::forwardLeft);
    REQUIRE(directionFromKeys(false, true, true, false) == MoveDir::backRight);
    // opposite keys cancel, the other axis stays
    REQUIRE(directionFromKeys(true, true, false, false) == MoveDir::none);
    REQUIRE(directionFromKeys(true, true, true, false) == MoveDir::right);
    REQUIRE(directionFromKeys(true, false, true, true) == MoveDir::forward);
}

TEST_CASE("locomotion.stick")
{
    REQUIRE(intentFromStick(0.1f, 0.15f).dir == MoveDir::none); // inside the dead zone
    REQUIRE(intentFromStick(0.0f, 0.5f).dir == MoveDir::forward);
    REQUIRE_FALSE(intentFromStick(0.0f, 0.5f).run);
    REQUIRE(intentFromStick(0.0f, -0.7f).run);
    REQUIRE(intentFromStick(0.0f, -0.7f).dir == MoveDir::back);
    REQUIRE(intentFromStick(0.5f, 0.5f).dir == MoveDir::forwardRight);
    REQUIRE(intentFromStick(-0.9f, 0.1f).dir == MoveDir::left);
}

TEST_CASE("locomotion.local")
{
    REQUIRE(directionFromLocal(0.0f, 0.0f) == MoveDir::none);
    REQUIRE(directionFromLocal(0.0f, 2.0f) == MoveDir::forward);
    REQUIRE(directionFromLocal(1.0f, 0.3f) == MoveDir::right);        // within 22.5 degrees of the axis
    REQUIRE(directionFromLocal(1.0f, 0.5f) == MoveDir::forwardRight); // beyond it
    REQUIRE(directionFromLocal(-0.3f, -1.0f) == MoveDir::back);
    REQUIRE(directionFromLocal(-1.0f, -1.0f) == MoveDir::backLeft);
    for (int i = 1; i <= 8; i++)
    {
        const auto dir = static_cast<MoveDir>(i);
        const auto v = localVector(dir);
        REQUIRE(std::fabs(v.right * v.right + v.forward * v.forward - 1.0f) < 1e-5f);
        REQUIRE(directionFromLocal(v.right, v.forward) == dir);
    }
}

TEST_CASE("locomotion.world")
{
    const auto near = [](WorldVector v, float x, float z) {
        return std::fabs(v.x - x) < 1e-5f && std::fabs(v.z - z) < 1e-5f;
    };
    // heading 0 faces +z; right is +x (the engine's strafe side)
    REQUIRE(near(worldTravel(MoveDir::forward, 0.0f), 0.0f, 1.0f));
    REQUIRE(near(worldTravel(MoveDir::back, 0.0f), 0.0f, -1.0f));
    REQUIRE(near(worldTravel(MoveDir::right, 0.0f), 1.0f, 0.0f));
    REQUIRE(near(worldTravel(MoveDir::left, 0.0f), -1.0f, 0.0f));
    // heading a quarter turn faces +x; his right is then -z
    const float quarter = 1.5707963f;
    REQUIRE(near(worldTravel(MoveDir::forward, quarter), 1.0f, 0.0f));
    REQUIRE(near(worldTravel(MoveDir::back, quarter), -1.0f, 0.0f));
    REQUIRE(near(worldTravel(MoveDir::right, quarter), 0.0f, -1.0f));
    REQUIRE(near(worldTravel(MoveDir::none, quarter), 0.0f, 0.0f));
}

TEST_CASE("locomotion.names")
{
    REQUIRE(moveAttribute(false, Gait::walk, MoveDir::forward) == "forward_walk");
    REQUIRE(moveAttribute(true, Gait::run, MoveDir::backLeft) == "fight_back-left_run");
    REQUIRE(startAttribute(false, Gait::sprint, MoveDir::forwardRight) == "start_forward-right_sprint");
    REQUIRE(stopAttribute(Gait::run, MoveDir::left, 0) == "stop_left_run");
    REQUIRE(stopAttribute(Gait::run, MoveDir::left, stopStepAfter(Foot::right)) == "stop_left_run_lstep");
    REQUIRE(stopAttribute(Gait::run, MoveDir::left, stopStepAfter(Foot::left)) == "stop_left_run_rstep");
    REQUIRE(stairsAttribute(true, stairsIndex(MoveDir::backRight), false) == "fight_back_walk_stairs_down");
    REQUIRE(stairsAttribute(false, stairsIndex(MoveDir::forwardLeft), true) == "forward_walk_stairs_up");
    REQUIRE(stairsAttribute(false, stairsIndex(MoveDir::left), true) == "left_walk_stairs_up");
}

TEST_CASE("locomotion.easing")
{
    REQUIRE_FALSE(parseEasing("StartLinear").has_value());
    REQUIRE(parseEasing("StopQuart") == Easing::stopQuart);
    REQUIRE(ease(Easing::none, 0.3f) == 1.0f);
    for (const auto e : {Easing::startCubic, Easing::startQuart, Easing::startQuint, Easing::startExpo})
    {
        REQUIRE(ease(e, 0.0f) == Approx(0.0f).margin(1e-6));
        REQUIRE(ease(e, 1.0f) == Approx(1.0f));
        REQUIRE(ease(e, 0.25f) < ease(e, 0.5f));
        REQUIRE(ease(e, 0.5f) > 0.5f); // fast off the mark
    }
    for (const auto e : {Easing::stopCubic, Easing::stopQuart, Easing::stopQuint, Easing::stopExpo})
    {
        REQUIRE(ease(e, 0.0f) == Approx(1.0f));
        REQUIRE(ease(e, 1.0f) == Approx(0.0f).margin(1e-6));
        REQUIRE(ease(e, 0.25f) > ease(e, 0.5f));
        REQUIRE(ease(e, 0.5f) < 0.5f); // most of the speed gone by half way
    }
    REQUIRE(ease(Easing::startCubic, 0.5f) == Approx(0.875f));
    REQUIRE(ease(Easing::stopQuart, 0.5f) == Approx(0.0625f));
}

TEST_CASE("locomotion.stride")
{
    Stride s;
    s.want(MoveDir::none, true, true);
    REQUIRE(s.phase == StridePhase::still);

    // a run from standing starts with its start clip, then loops
    s.want(MoveDir::forward, true, true);
    REQUIRE(s.phase == StridePhase::starting);
    REQUIRE(s.dir == MoveDir::forward);
    s.want(MoveDir::forward, true, true);
    REQUIRE(s.phase == StridePhase::starting); // holding the key does not cut the start short
    s.clipEnded();
    REQUIRE(s.phase == StridePhase::moving);

    // a turn under way changes the loop at once
    s.want(MoveDir::left, true, true);
    REQUIRE(s.phase == StridePhase::moving);
    REQUIRE(s.dir == MoveDir::left);

    // letting go plays the stop clip of the direction walked, then stands
    s.lastStep = Foot::right;
    s.want(MoveDir::none, true, true);
    REQUIRE(s.phase == StridePhase::stopping);
    REQUIRE(s.dir == MoveDir::left);
    s.want(MoveDir::none, true, true);
    REQUIRE(s.phase == StridePhase::stopping);
    s.clipEnded();
    REQUIRE(s.phase == StridePhase::still);
    REQUIRE(s.dir == MoveDir::none);
    REQUIRE(s.lastStep == Foot::none);

    // without clips (a walk, a fight stop) the legs start and stop at once
    s.want(MoveDir::back, false, false);
    REQUIRE(s.phase == StridePhase::moving);
    s.want(MoveDir::none, false, false);
    REQUIRE(s.phase == StridePhase::still);
    REQUIRE(s.dir == MoveDir::none);

    // walking again during a stop starts anew
    s.want(MoveDir::forward, false, false);
    s.want(MoveDir::none, false, true);
    REQUIRE(s.phase == StridePhase::stopping);
    s.want(MoveDir::right, true, true);
    REQUIRE(s.phase == StridePhase::starting);
    REQUIRE(s.dir == MoveDir::right);
}
