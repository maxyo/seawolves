// The hero's locomotion under the touch stick (android_motion.hpp): Character::Move's displacement, Player's choice of
// gait from the stick and from the native controls, and the location camera the stick steers relative to, run with
// owner types of the check's own.
#include "core_fake.hpp"
#include "android_motion.hpp"
#include "platform/android_movement.hpp"

#include <catch2/catch.hpp>

#include <limits>
#include <string_view>

static storm::test::FakeCore fake;
Core &core = fake;

using namespace storm::android;
using storm::location::CameraMotion;
using storm::location::CharacterMotion;
using storm::location::PlayerMotion;
using Vec3 = glm::vec3;

namespace
{
bool near(float a, float b)
{
    return std::fabs(a - b) < .00001f;
}
void same(Vec3 a, Vec3 b)
{
    REQUIRE((near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z)));
}

// The native strafe keys (ChrStrafeLeft / ChrStrafeRight) as held or not.
struct StrafeKeys final : CONTROLS
{
    int left = 0, right = 0;
    bool GetControlState(const char *name, CONTROL_STATE &state) override
    {
        state.lValue = std::string_view(name) == "ChrStrafeLeft" ? left : right;
        return true;
    }
} controls;

struct Camera
{
    enum
    {
        cwm_none,
        cwm_follow,
        cwm_topos,
        cwm_free
    };
    int wmode = cwm_follow;
    bool isLookMode = false, isLockFPMode = false, isSleep = false, m_bTrackMode = false;
    Vec3 lookTo{0, 0, 1}, camPos{}, pos{}, fromLook{};
    float lheight = 1;
    Vec3 movementForward{0, 0, 1};
    Vec3 MovementViewDirection() const
    {
        return movementForward;
    }
    bool IsMovementCamera() const
    {
        return CameraMotion::IsMovementCamera(*this);
    }
    void UpdateMovementHeading()
    {
        CameraMotion::UpdateMovementHeading(*this);
    }
    void ExecuteTopos(float)
    {
        CameraMotion::ExecuteTopos(*this);
    }
} camera;

// Character's members that Character::Move's locomotion reads.
struct Motion
{
    static constexpr int fgt_none = 0;
    bool fight = false, isTurnLock = false, androidCameraRelative = false;
    bool IsFight()
    {
        return fight;
    }
    bool UsesTouchCamera() const
    {
        return androidCameraRelative && !fight && !isSwim && !isJump;
    }
    float nay = 0, turnDir = 0;
    bool androidMotion = true, androidMotionStop = false, androidMotionApplied = false, isMove = true, lockMove = false,
         isJump = false, isSwim = false;
    struct
    {
        const char *name = nullptr;
    } priorityAction;
    int fgtCurType = fgt_none, fgtSetType = fgt_none;
    float androidMotionSpeed = .2f, androidMotionWalkSpeed = 2, speed = 2, dltTime = .1f, k = 1, ay = .7f,
          strafeMove = .5f, strafeVel = .5f;
    struct Action
    {
        float speed = 2;
    } walk, stsUp, stsDown, stsUpBack, stsDownBack;
    Action backwalk{-2}, run{5}, backrun{-5}, fightwalk{2}, fightrun{5};
    Action *curMove = &walk;
    // A character walking by heading: no directional actions.
    const void *directional = nullptr;
    storm::location::Stride stride;
    float DirectionalPace() const
    {
        return 1.0f;
    }
    Vec3 androidMotionDirection{1, 0, 0}, curPos, impulse;
    // One frame of Character::Move's travel (no animation, collision or turning).
    void step()
    {
        const auto touch = CharacterMotion::touchStep(*this);
        CharacterMotion::displace(*this, dltTime, k, touch);
        CharacterMotion::strafeInPlace(*this, dltTime, touch);
    }
};

// Player's members that the gait choice reads and the actions it takes, counted.
struct Player : Motion
{
    static constexpr int npct_none = 0, aicmd_none = 0;
    struct
    {
        int task = 0;
    } task;
    struct
    {
        int cmd = 0;
    } command;
    bool androidMovementHeld = true, androidRunTier = false;
    uint64_t androidCameraGeneration = 0;
    Camera *locCam = &camera;
    bool lastBack = false, lastRun = false;
    bool nativeForward = false, nativeBack = false;
    bool GoForward(float)
    {
        return nativeForward;
    }
    bool GoBack(float)
    {
        return nativeBack;
    }
    bool IsRunMode(float)
    {
        return true;
    }
    bool isSpecialMode = false, allowDodge = true, isRunDisable = false;
    int dodges = 0, leftDodges = 0, recoils = 0;
    void StrafeLeft()
    {
        ++dodges;
        ++leftDodges;
        if (allowDodge)
            fgtSetType = 1;
    }
    void StrafeRight()
    {
        ++dodges;
        if (allowDodge)
            fgtSetType = 1;
    }
    void SetRunMode(bool run)
    {
        lastRun = run;
    }
    float turnX = 0, turnZ = 0;
    int turns = 0;
    void Turn(float x, float z)
    {
        turnX = x;
        turnZ = z;
        ++turns;
    }
    void StartMove(bool back)
    {
        isMove = true;
        lastBack = back;
    }
    void StrafeWhenMove(float dltTime)
    {
        PlayerMotion::StrafeWhenMove(*this, dltTime);
    }
    void StrafeWhenStop(float dltTime)
    {
        PlayerMotion::StrafeWhenStop(*this, dltTime);
    }
    void Recoil()
    {
        ++recoils;
        if (allowDodge)
            fgtSetType = 1;
    }
    void StopAndroidMovement()
    {
        PlayerMotion::StopAndroidMovement(*this);
    }
    // Player::Move with the stick held: the per-frame vector cleared, the sample taken, the gait chosen.
    void analog(MovementAxes sample)
    {
        androidMotion = false;
        release(sample);
        PlayerMotion::stickGait(*this, sample);
    }
    // Player::Move without the stick: the native controls choose.
    void physical()
    {
        PlayerMotion::controlsGait(*this, dltTime);
    }
    unsigned GetId()
    {
        return 7;
    }
    void StopMove()
    {
        if (!priorityAction.name)
            isMove = false;
    }
    void release(std::optional<MovementAxes> androidAxes)
    {
        PlayerMotion::takeAxes(*this, androidAxes);
    }
};
} // namespace

TEST_CASE("the touch stick moves the hero as ordinary locomotion and leaves native movement alone")
{
    fake.Controls = &controls;
    fake.entities["Player"] = 7;
    // Reading the published heading (including diagnostics) never changes it.
    // Only the completed camera update publishes a new basis, even without input.
    {
        Camera observed;
        const Camera &readOnly = observed;
        observed.lookTo = {3, -4, 0};
        same(readOnly.MovementViewDirection(), {0, 0, 1});
        same(readOnly.MovementViewDirection(), {0, 0, 1});
        observed.UpdateMovementHeading();
        same(readOnly.MovementViewDirection(), {1, 0, 0});
        for (const Vec3 invalid :
             {Vec3(0, -10, 0), Vec3(.00001f, -10, .00001f), Vec3(std::numeric_limits<float>::infinity(), 0, 1),
              Vec3(1, std::numeric_limits<float>::quiet_NaN(), 0)})
        {
            observed.lookTo = invalid;
            observed.UpdateMovementHeading();
            same(readOnly.MovementViewDirection(), {1, 0, 0});
        }
        observed.lookTo = {0, -4, -3};
        same(readOnly.MovementViewDirection(), {1, 0, 0});
        observed.UpdateMovementHeading();
        same(readOnly.MovementViewDirection(), {0, 0, -1});
    }
    Player slow;
    slow.step();
    same(slow.curPos, {.02f, 0, 0}); // full native speed=2, touch target=.2
    slow.release(std::nullopt);
    slow.androidMotion = false;
    REQUIRE((slow.androidMotionStop && !slow.isMove));
    auto stopped = slow.curPos;
    for (int frame = 0; frame < 4; ++frame)
    {
        slow.speed -= .2f;
        slow.step();
        same(slow.curPos, stopped);
    }
    REQUIRE((slow.speed > .1f)); // suppress the residual, never mutate native blending
    slow.curMove = nullptr;
    slow.step();
    same(slow.curPos, stopped); // idle animation after release
    slow.impulse = {.4f, 0, -.3f};
    slow.step();
    same(slow.curPos, {.06f, 0, -.03f});
    Player center;
    center.release(MovementAxes{0, 0, 0, true});
    center.androidMotion = false;
    center.step();
    same(center.curPos, {});
    REQUIRE((center.androidMotionStop));
    // Special actions retain their original native displacement and impulse,
    // even if a stop was latched immediately before that action began.
    for (int special = 0; special < 5; ++special)
    {
        Motion native, touch;
        native.androidMotion = false;
        touch.androidMotion = false;
        touch.androidMotionStop = true;
        touch.isMove = false;
        native.isMove = false;
        native.impulse = touch.impulse = {.4f, 0, -.3f};
        if (special == 0)
            native.priorityAction.name = touch.priorityAction.name = "hit";
        if (special == 1)
            native.fgtCurType = touch.fgtCurType = 1;
        if (special == 2)
            native.fgtSetType = touch.fgtSetType = 1;
        if (special == 3)
            native.isSwim = touch.isSwim = true;
        if (special == 4)
            native.isJump = touch.isJump = true;
        native.step();
        touch.step();
        same(native.curPos, touch.curPos);
        REQUIRE((!touch.androidMotionApplied));
    }
    // Swimming: the held stick steers the native swim, relative to the camera, through every
    // change between walking and swimming (it used to be dropped in water: the waves carried
    // the hero one way and the stick could not change it).
    {
        Player swimmer;
        swimmer.androidCameraRelative = true;
        camera.lookTo = {0, 0, 1};
        camera.UpdateMovementHeading();
        swimmer.isSwim = true;
        swimmer.isMove = false;
        swimmer.androidRunTier = true;
        swimmer.analog(MovementAxes{1, 0, 1, true}); // pushed right
        REQUIRE((swimmer.isMove && !swimmer.lastRun && !swimmer.androidRunTier && swimmer.strafeMove == 0 &&
                 !swimmer.androidMotion));
        REQUIRE((swimmer.turns == 1 && near(swimmer.turnX, 1) && near(swimmer.turnZ, 0)));
        swimmer.analog(MovementAxes{0, -1, 1, true}); // pulled back: swims towards the camera
        REQUIRE((swimmer.turns == 2 && near(swimmer.turnX, 0) && near(swimmer.turnZ, -1)));
        swimmer.isSwim = false;
        swimmer.analog(MovementAxes{0, 1, 1, true}); // ashore: the touch walk again
        REQUIRE((swimmer.androidMotion && swimmer.turns == 2 && near(swimmer.androidMotionDirection.z, 1)));
        swimmer.isSwim = true;
        swimmer.analog(MovementAxes{-1, 0, 1, true}); // back in the water, same finger
        REQUIRE((swimmer.turns == 3 && near(swimmer.turnX, -1) && swimmer.isMove));
        swimmer.analog(MovementAxes{0, 0, 0, true}); // the thumb in the dead zone stops the swim
        REQUIRE((!swimmer.isMove && swimmer.androidMotionStop));
        REQUIRE((touchGait(MovementAxes{0, -1, 1, true}, true, true) == TouchGait::swim &&
                 touchGait(MovementAxes{0, -1, 1, true}, false, true) == TouchGait::combatBack &&
                 touchGait(MovementAxes{1, 0, 1, true}, false, true) == TouchGait::combatSide &&
                 touchGait(MovementAxes{0, 1, 1, true}, false, false) == TouchGait::walk &&
                 touchGait(MovementAxes{}, true, false) == TouchGait::stop));
    }
    Player scripted;
    scripted.androidMotionStop = true;
    scripted.androidRunTier = true;
    scripted.androidCameraRelative = true;
    scripted.task.task = 1;
    scripted.release(std::nullopt);
    REQUIRE((!scripted.androidMotionStop && !scripted.androidRunTier && !scripted.androidCameraRelative));
    scripted.task.task = 0;
    scripted.command.cmd = 1;
    scripted.androidMotionStop = true;
    scripted.release(std::nullopt);
    REQUIRE((!scripted.androidMotionStop));
    // A physical movement request clears the stop latch and resumes the exact
    // untouched native displacement route (including its strafe contribution).
    Player physical;
    physical.androidMotion = false;
    physical.androidMotionStop = true;
    physical.nativeForward = true;
    physical.physical();
    REQUIRE((!physical.androidMotionStop));
    physical.androidMotionStop = true;
    physical.nativeForward = false;
    physical.nativeBack = true;
    physical.physical();
    REQUIRE((!physical.androidMotionStop));
    physical.step();
    Vec3 expected(sinf(physical.ay) * .2f, 0, cosf(physical.ay) * .2f);
    expected += Vec3(cosf(physical.ay), 0, -sinf(physical.ay)) * .045f;
    same(physical.curPos, expected);
    for (bool left : {false, true})
    {
        Player lateral;
        lateral.androidMotion = false;
        lateral.androidMotionStop = true;
        controls.left = left ? 1 : 0;
        controls.right = left ? 0 : 1;
        lateral.physical();
        REQUIRE((!lateral.androidMotionStop));
        REQUIRE((lateral.strafeMove == 0)); // stopped explore strafe keeps its native semantics
        lateral.step();
        same(lateral.curPos, expected); // native residual strafeVel survives
        Player fight;
        fight.androidMotion = false;
        fight.androidMotionStop = true;
        fight.fight = true;
        fight.physical();
        REQUIRE((!fight.androidMotionStop && fight.dodges == 1 && fight.fgtSetType == 1));
        fight.step();
        REQUIRE((!fight.androidMotionApplied)); // native dodge is not a touch stop
    }
    controls.left = controls.right = 0;
    // Exercise the entire production analog decision block: slow forward
    // transitions to held low-radius side/back input, not just UP/center.
    for (float x : {-1.0f, 0.0f, 1.0f})
    {
        Player melee;
        melee.fight = true;
        melee.analog({0, 1, .1f, true});
        melee.step();
        auto before = melee.curPos;
        melee.analog({x, x == 0 ? -1.0f : 0.0f, .1f, true});
        melee.step();
        same(melee.curPos, before);
        REQUIRE((melee.androidMotionStop && !melee.isMove));
        REQUIRE((melee.dodges == 0 && melee.recoils == 0));
        // A high-radius special denied by native cooldown/energy must also
        // remain stopped; accepting it must leave native combat physics alone.
        melee.allowDodge = false;
        melee.analog({x, x == 0 ? -1.0f : 0.0f, .8f, true});
        melee.step();
        same(melee.curPos, before);
        REQUIRE((melee.dodges + melee.recoils == 1));
        melee.allowDodge = true;
        melee.analog({x, x == 0 ? -1.0f : 0.0f, .8f, true});
        melee.step();
        REQUIRE((melee.fgtSetType == 1 && !melee.androidMotionApplied));
    }
    Player analogCenter;
    analogCenter.analog({0, 1, .1f, true});
    analogCenter.step();
    auto beforeCenter = analogCenter.curPos;
    analogCenter.analog({0, 0, 0, true});
    analogCenter.step();
    same(analogCenter.curPos, beforeCenter);
    REQUIRE((analogCenter.androidMotionStop));
    // Real Player decision + Character displacement: a front-facing camera
    // or a diagonal may not produce backwards gait / motion against heading.
    for (int cam = 0; cam < 360; cam += 30)
        for (int angle = 0; angle < 360; angle += 5)
        {
            const float c = cam * .017453292519943295f, a = angle * .017453292519943295f;
            camera.lookTo = {std::sin(c), 0, std::cos(c)};
            camera.UpdateMovementHeading();
            Player moving;
            moving.analog({std::sin(a), std::cos(a), .5f, true});
            moving.step();
            REQUIRE((!moving.lastBack && !moving.lastRun));
            REQUIRE((near(std::hypot(moving.curPos.x, moving.curPos.z), .2f)));
            REQUIRE((near(std::sin(moving.ay), moving.androidMotionDirection.x)));
            REQUIRE((near(std::cos(moving.ay), moving.androidMotionDirection.z)));
            REQUIRE((moving.strafeMove == 0));
        }
    // The same held stick follows the live locator camera as the actor moves
    // across the location. Actor heading changes must never feed back into its basis.
    for (int mode : {Camera::cwm_topos, Camera::cwm_follow})
    {
        camera.wmode = mode;
        Player moving;
        MovementInput held;
        held.guard = [](std::string_view) { return std::optional<MovementPermission>({true, true, true, true, true}); };
        held.updateOwner("camera-test");
        REQUIRE((held.offer(storm::ui::hud::Move{"camera-test", held.generation(), true, 500, 0})));
        for (int frame = 0; frame < 360; ++frame)
        {
            const float yaw = frame * .017453292519943295f;
            camera.lookTo = {std::sin(yaw) * 3, -4, std::cos(yaw) * 3};
            if (mode == Camera::cwm_topos)
            {
                camera.pos = camera.lookTo - Vec3(0, camera.lheight, 0);
                camera.ExecuteTopos(.1f); // real locator camera tracks the actor position
            }
            camera.UpdateMovementHeading(); // publish the completed camera frame, independent of input
            const Vec3 before = moving.curPos;
            moving.analog(*held.read());
            moving.step(); // no new pointer/MOVE packet
            REQUIRE((moving.androidCameraRelative && "live locator camera must own movement basis"));
            same(moving.curPos - before, Vec3(std::cos(yaw) * .2f, 0, -std::sin(yaw) * .2f));
            REQUIRE((moving.androidCameraRelative && !moving.lastBack && moving.strafeMove == 0));
        }
        // A pitched/vertical view keeps the last camera heading independently
        // of character yaw; leaving the pole picks up the new camera immediately.
        camera.lookTo = {3, -4, 0};
        camera.UpdateMovementHeading();
        moving.analog({0, 1, .5f, true});
        moving.step();
        for (Vec3 pole : {Vec3(0, -10, 0), Vec3(.00001f, -10, .00001f)})
        {
            camera.lookTo = pole;
            camera.UpdateMovementHeading();
            moving.ay = 2;
            moving.analog({1, 0, .5f, true});
            same(moving.androidMotionDirection, {0, 0, -1});
        }
        camera.lookTo = {0, -4, -3};
        camera.UpdateMovementHeading();
        moving.analog({0, 1, .5f, true});
        same(moving.androidMotionDirection, {0, 0, -1});
        moving.release(std::nullopt);
        moving.androidMotion = false;
        const Vec3 stopped = moving.curPos;
        for (int frame = 0; frame < 10; ++frame)
        {
            moving.step();
            same(moving.curPos, stopped);
        }
        Player swimmer;
        swimmer.isSwim = true;
        swimmer.analog({0, 1, .5f, true});
        REQUIRE((near(swimmer.turnX, 0) && near(swimmer.turnZ, -1)));
    }
    // A first-person flag left over from the previous follow mode must not
    // disable the now-visible locator camera. Free/track/sleep never opt in.
    camera.wmode = Camera::cwm_topos;
    camera.isLookMode = true;
    Player locator;
    locator.analog({0, 1, .5f, true});
    REQUIRE((locator.androidCameraRelative));
    camera.isLookMode = false;
    for (int excluded = 0; excluded < 3; ++excluded)
    {
        camera.wmode = excluded == 0 ? Camera::cwm_free : Camera::cwm_follow;
        camera.m_bTrackMode = excluded == 1;
        camera.isSleep = excluded == 2;
        Player disabled;
        disabled.analog({0, 1, .5f, true});
        REQUIRE((!disabled.androidCameraRelative));
    }
    camera.m_bTrackMode = camera.isSleep = false;
    camera.wmode = Camera::cwm_follow;
    camera.lookTo = {0, 0, -1};
    Player combat;
    combat.fight = true;
    combat.ay = 0;
    combat.analog({0, 1, .5f, true});
    combat.step();
    REQUIRE((combat.curPos.z > 0 && combat.curPos.x == 0 && !combat.lastBack));
    camera.lookTo = {0, 0, 1};
    // First-person uses actor-relative forward/back and preserves look yaw.
    camera.isLookMode = true;
    camera.lookTo = {0, 0, -1};
    Player eyes;
    eyes.ay = 0;
    eyes.analog({0, -1, .5f, true});
    eyes.step();
    REQUIRE((!eyes.androidCameraRelative && eyes.lastBack && eyes.ay == 0 && eyes.curPos.z < 0));
    eyes.analog({1, 0, .5f, true});
    eyes.step();
    REQUIRE((eyes.ay == 0 && eyes.androidMotionDirection.x == 1 && eyes.androidMotionDirection.z == 0));
    camera.isLookMode = false;
    camera.lookTo = {0, 0, 1};
    camera.UpdateMovementHeading();
    Player tiers;
    for (float radius : {.2f, .4f, .79f})
    {
        tiers.analog({0, 1, radius, true});
        REQUIRE((tiers.androidMotionSpeed == 2 && !tiers.lastRun));
    }
    for (float radius : {.8f, 1.f, .7f, .65f})
    {
        tiers.analog({0, 1, radius, true});
        REQUIRE((tiers.androidMotionSpeed == 5 && tiers.lastRun));
    }
    tiers.analog({0, 1, .64f, true});
    REQUIRE((tiers.androidMotionSpeed == 2 && !tiers.lastRun));
    tiers.analog({0, 1, 1, true});
    tiers.analog({0, 0, 0, true});
    REQUIRE((!tiers.androidRunTier));
    tiers.analog({0, 1, .7f, true});
    REQUIRE((tiers.androidMotionSpeed == 2 && !tiers.lastRun));
    fake.Controls = nullptr;
}

TEST_CASE("the guards that keep native movement and the camera's frame")
{
    fake.Controls = &controls;
    fake.entities["Player"] = 7;
    SECTION("a stop of the stick holds the hero only while he does not move")
    {
        Motion latched, free;
        latched.androidMotion = free.androidMotion = false;
        latched.androidMotionStop = true;
        latched.step();
        free.step();
        same(latched.curPos, free.curPos);
        REQUIRE(!latched.androidMotionApplied);
    }
    SECTION("a priority action keeps its native displacement under a held stick")
    {
        Motion native, touch;
        native.androidMotion = false;
        native.priorityAction.name = touch.priorityAction.name = "hit";
        native.step();
        touch.step();
        same(native.curPos, touch.curPos);
        REQUIRE(!touch.androidMotionApplied);
    }
    SECTION("the stick turns the hero to its vector unless his turn is locked")
    {
        Motion turning, locked;
        turning.androidCameraRelative = locked.androidCameraRelative = true;
        locked.isTurnLock = true;
        turning.step();
        locked.step();
        REQUIRE((near(turning.ay, std::atan2(1.0f, 0.0f)) && near(locked.ay, .7f)));
    }
    SECTION("on stairs the stick's speed is the stairs gait's share of the walk")
    {
        Motion climbing;
        climbing.stsUp.speed = 1;
        climbing.curMove = &climbing.stsUp;
        climbing.step();
        same(climbing.curPos, {.01f, 0, 0}); // .2 m/s * 1/2 for .1 s
    }
    SECTION("a native key takes the hero back from the camera's frame; released with the stick held, he stops")
    {
        Player forward;
        forward.androidCameraRelative = true;
        forward.nativeForward = true;
        forward.physical();
        REQUIRE(!forward.androidCameraRelative);
        Player held, idle;
        held.androidMotionStop = idle.androidMotionStop = false;
        idle.androidMovementHeld = false;
        held.physical();
        idle.physical();
        REQUIRE((held.androidMotionStop && !held.isMove && !idle.androidMotionStop && !idle.isMove));
    }
    SECTION("a camera that is not the stick's frame ends the camera-relative walk")
    {
        Player walker;
        walker.androidCameraRelative = true;
        walker.androidCameraGeneration = movementInput.generation();
        camera.isSleep = true;
        walker.release(std::nullopt);
        camera.isSleep = false;
        REQUIRE(!walker.androidCameraRelative);
    }
    SECTION("a new gesture of the stick ends the camera-relative walk until the stick is sampled")
    {
        Player walker;
        walker.androidCameraRelative = true;
        walker.androidCameraGeneration = movementInput.generation() + 1;
        walker.release(std::nullopt);
        REQUIRE(!walker.androidCameraRelative);
    }
    SECTION("in a fight the stick sideways dodges to its own side")
    {
        Player left, right;
        left.fight = right.fight = true;
        left.analog({-1, 0, 1, true});
        right.analog({1, 0, 1, true});
        REQUIRE((left.dodges == 1 && left.leftDodges == 1 && right.dodges == 1 && right.leftDodges == 0));
    }
    SECTION("the stick's walk is the gait's walk, the measure of the stairs' share")
    {
        Player walker;
        walker.androidMotionWalkSpeed = 0;
        walker.analog({0, 1, .5f, true});
        REQUIRE(walker.androidMotionWalkSpeed == std::fabs(walker.walk.speed));
    }
    SECTION("the animation runs at the stick's share of the gait; native movement at its own pace")
    {
        Motion touch, native;
        native.androidMotion = false;
        REQUIRE((near(CharacterMotion::touchStep(touch).pace, .1f) && CharacterMotion::touchStep(native).pace == 1));
    }
    SECTION("a stick held in one frame and gone in the next stops the hero")
    {
        Player walker;
        walker.androidMovementHeld = false;
        walker.release(MovementAxes{0, 1, .5f, true});
        walker.androidMotionStop = false;
        walker.release(std::nullopt);
        REQUIRE(walker.androidMotionStop);
    }
    SECTION("a steep view keeps the last heading; the locator camera looks at the actor's head from its point")
    {
        Camera steep;
        steep.lookTo = {.002f, -10, 0}; // horizontal, but less than a thousandth of the drop
        steep.UpdateMovementHeading();
        same(steep.MovementViewDirection(), {0, 0, 1});
        Camera locator;
        locator.pos = {1, 2, 3};
        locator.lheight = 1.5f;
        locator.fromLook = {4, 5, 6};
        locator.ExecuteTopos(.1f);
        same(locator.lookTo, {1, 3.5f, 3});
        same(locator.camPos, {4, 5, 6});
    }
    fake.Controls = nullptr;
}
