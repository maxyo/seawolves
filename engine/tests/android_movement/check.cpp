// The touch stick's input (platform/android_movement.hpp): the mailbox of its samples and who may move the hero, the
// axes, pace and direction it gives, and which native controls allow which direction.
#include "platform/android_movement.hpp"

#include <catch2/catch.hpp>

#include <limits>
using namespace storm::android;
static bool near(float a, float b)
{
    return std::fabs(a - b) < .0001f;
}

TEST_CASE("the stick's samples reach the hero only from the current owner, generation and gesture")
{
    MovementInput input;
    bool ready = true;
    GameplaySnapshot state;
    state.mode = "explore";
    state.inputOwner = "scene:player";
    MovementPermission permission{true, true, true, true, true};
    input.guard = [&](std::string_view owner) -> std::optional<MovementPermission> {
        if (!ready || !movementOwnerCurrent(owner, state))
            return std::nullopt;
        return permission;
    };
    input.updateOwner(state.inputOwner);
    // The HUD's stick of the current owner and movement generation.
    auto wire = [&](bool active = true, int x = 0, int y = 1000) {
        return storm::ui::hud::Move{state.inputOwner, input.generation(), active, x, y};
    };
    REQUIRE((!input.read())); // legacy keyboard remains the default
    REQUIRE((input.offer(wire())));
    auto axes = input.read();
    REQUIRE((axes && near(axes->y, 1) && near(axes->magnitude, 1)));
    const auto generation = input.generation();
    state.mode = "melee";
    input.updateOwner(state.inputOwner);
    REQUIRE((input.generation() == generation && input.read())); // stance retains gesture
    // Beyond the stick's travel, a released stick that still points somewhere, another owner or generation.
    for (const auto &bad : {wire(true, 1001, 0), wire(true, 0, -1001), wire(false, 1, 0)})
        REQUIRE((!input.offer(bad)));
    REQUIRE((!input.offer(storm::ui::hud::Move{"wrong", generation, true, 0, 1})));
    REQUIRE((!input.offer(storm::ui::hud::Move{state.inputOwner, generation + 1, true, 0, 1})));
    REQUIRE((input.offer(wire(true, 1000, 1000)))); // circular clamp, no diagonal boost
    axes = input.read();
    REQUIRE((axes && near(std::hypot(axes->x, axes->y), 1) && near(axes->magnitude, 1)));
    permission.right = false;
    axes = input.read();
    REQUIRE((axes && axes->x == 0 && axes->y == 1)); // live lock
    permission.forward = false;
    axes = input.read();
    REQUIRE((axes && axes->magnitude == 0));
    permission = {true, true, true, true, true};
    REQUIRE((input.offer(wire(false, 0, 0))));
    REQUIRE((!input.read()));
    REQUIRE((input.offer(wire())));
    auto stale = wire();
    cancelAndroidCombatInput();
    REQUIRE((!input.read() && input.generation() != generation));
    REQUIRE((!input.offer(stale))); // hard cancellation cannot revive from Java queue
    REQUIRE((input.offer(wire())));
    auto epoch = combatCancelEpoch.load();
    auto drained = wire();
    cancelAndroidCombatInput();
    REQUIRE((!input.offer(drained, epoch))); // JNI drain raced cancellation
    // Event model: a held stick stays held however long the UI is silent (slow frames, seconds
    // apart); only its release, a cancellation, a new owner or the guard end it.
    REQUIRE((input.offer(wire())));
    for (int frame = 0; frame < 1000; ++frame)
    {
        axes = input.read();
        REQUIRE((axes && near(axes->magnitude, 1)));
    }
    REQUIRE((input.offer(wire()))); // the next change, seconds later, still belongs to the same gesture
    REQUIRE((input.offer(wire(false, 0, 0)) && !input.read()));
    REQUIRE((input.offer(wire())));
    stale = wire();
    ready = false;
    REQUIRE((!input.read()));
    ready = true;
    input.updateOwner(state.inputOwner);
    REQUIRE((!input.offer(stale)));
    REQUIRE((input.offer(wire())));
    stale = wire();
    state.inputOwner = "scene:newplayer";
    input.updateOwner(state.inputOwner);
    REQUIRE((!input.offer(stale)));
    REQUIRE((!input.read()));
    REQUIRE((input.offer(wire())));
    stale = wire();
    input.resetActivity();
    REQUIRE((!input.offer(stale)));
    for (const auto *mode : {"unknown", "sailing", "naval"})
    {
        state.mode = mode;
        REQUIRE((!movementOwnerCurrent(state.inputOwner, state)));
    }
    state.mode = "explore";
    REQUIRE((movementOwnerCurrent(state.inputOwner, state)));
    REQUIRE((!movementOwnerCurrent("", state)));
    REQUIRE((ordinaryMovementAllowed(true, true, false, false, false, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(false, true, false, false, false, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, false, false, false, false, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, true, false, false, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, true, false, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, false, true, false, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, false, false, true, false, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, false, false, false, true, false, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, false, false, false, false, true, true)));
    REQUIRE((!ordinaryMovementAllowed(true, true, false, false, false, false, false, false, false)));
    REQUIRE((movementAxes(180, 0, permission).magnitude == 0));
    REQUIRE((movementAxes(181, 0, permission).magnitude > 0));
    REQUIRE((movementAxes(121, 0, permission, true).magnitude > 0));
    REQUIRE((movementAxes(120, 0, permission, true).magnitude == 0));
    REQUIRE((near(movementAxes(0, -1000, permission).y, -1)));
    bool running = false;
    float previous = -1;
    for (int i = 0; i <= 1000; ++i)
    {
        auto pace = movementPace(i / 1000.0f, 2, 5, true, running);
        running = pace.run;
        REQUIRE((pace.speed >= previous));
        previous = pace.speed;
        REQUIRE((pace.speed == 0 || pace.speed == 2 || pace.speed == 5));
        REQUIRE((pace.run == (i >= 800)));
    }
    previous = 5;
    for (int i = 1000; i >= 0; --i)
    {
        auto pace = movementPace(i / 1000.0f, 2, 5, true, running);
        running = pace.run;
        REQUIRE((pace.speed <= previous));
        previous = pace.speed;
        REQUIRE((pace.run == (i >= 650)));
    }
    REQUIRE((!movementPace(.79f, 2, 5, true, false).run));
    REQUIRE((movementPace(.66f, 2, 5, true, true).run));
    REQUIRE((!movementPace(.64f, 2, 5, true, true).run));
    REQUIRE((near(movementPace(.3f, -2, -5, true).speed, 2)));
    REQUIRE((near(movementPace(1, -2, -5, true).speed, 5)));
    REQUIRE((!movementPace(1, 2, 5, false, true).run && near(movementPace(1, 2, 5, false, true).speed, 2)));
    // Direction is continuous, camera relative, unit length and independent of
    // the gait tier, including cameras looking at the character from the front.
    for (int camera = 0; camera < 360; camera += 15)
        for (int stick = 0; stick < 360; ++stick)
        {
            const float c = camera * .017453292519943295f, a = stick * .017453292519943295f;
            MovementAxes sample{std::sin(a), std::cos(a), .5f, true};
            const auto direction = movementDirection(sample, std::sin(c), std::cos(c), 0);
            REQUIRE((near(std::hypot(direction.x, direction.z), 1)));
            REQUIRE((near(direction.x, std::sin(c + a)) && near(direction.z, std::cos(c + a))));
        }
    const auto vertical = movementDirection({0, 1, .5f, true}, 0, 0, 1.2f);
    REQUIRE((near(vertical.x, std::sin(1.2f)) && near(vertical.z, std::cos(1.2f))));
    input.resetActivity();
    REQUIRE((input.offer(wire(true, 0, 200))));
    REQUIRE((input.read()->magnitude > 0));
    REQUIRE((input.offer(wire(true, 0, 140))));
    REQUIRE((input.read()->magnitude > 0));
    REQUIRE((input.offer(wire(true, 0, 120))));
    REQUIRE((input.read()->magnitude == 0));
    REQUIRE((input.offer(wire(true, 0, 140))));
    REQUIRE((input.read()->magnitude == 0));
    REQUIRE((input.offer(wire(true, 0, 200))));
    REQUIRE((input.read()->magnitude > 0));
    REQUIRE((input.offer(wire(false, 0, 0))));
    REQUIRE((!input.read()));
    REQUIRE((input.offer(wire(true, 0, 140))));
    REQUIRE((input.read()->magnitude == 0));
    GameplayState published;
    state.analogMovement = true;
    state.movementGeneration = 123;
    published.update(state);
    REQUIRE((published.hud().analog_movement && published.hud().movement_generation == 123)); // one player walks
}

// The native control descriptors (USER_CONTROL's fields) as the check needs them.
constexpr int UCT_ControlTree = 1, UNASSIGNED_CONTROL = -1;
struct Descriptor
{
    const char *name;
    bool bLocked = false, bSystemLocked = false;
    int control_type = 0, system_code = 0;
};
struct Controls
{
    std::vector<Descriptor> entries;
    int reads = 0;
    int GetControlsNum()
    {
        return static_cast<int>(entries.size());
    }
    bool GetControlDesc(int i, Descriptor &out)
    {
        ++reads;
        out = entries.at(i);
        return true;
    }
};

TEST_CASE("each direction is allowed by its first native control, in one pass over the descriptors")
{
    Controls controls;
    GameplaySnapshot snapshot;
    const auto permissions = [&] {
        return movementPermission<Descriptor>(controls, snapshot.capabilities, UCT_ControlTree, UNASSIGNED_CONTROL);
    };
    snapshot.capabilities = {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight", "ChrRun"};
    controls.entries = {{"irrelevant"},
                        {"ChrForward"},
                        {"ChrBackward", true},
                        {"ChrStrafeLeft", false, true},
                        {"ChrStrafeRight", false, false, 0, UNASSIGNED_CONTROL},
                        {"ChrRun", false, false, UCT_ControlTree, UNASSIGNED_CONTROL},
                        {"unused-after-all-five"},
                        {"ChrBackward"}};
    auto allowed = permissions();
    REQUIRE((allowed.forward && !allowed.backward && !allowed.left && !allowed.right && allowed.run));
    REQUIRE(controls.reads == 6); // stop after all five names, including locked/unmapped
    controls.reads = 0;
    snapshot.capabilities = {"ChrBackward"};
    allowed = permissions();
    REQUIRE((!allowed.forward && !allowed.backward && !allowed.left && !allowed.right && !allowed.run));
    REQUIRE(controls.reads == 6);
    // A duplicate name cannot count as another resolved direction, and a later
    // unlocked duplicate cannot override the first locked native descriptor.
    controls.entries = {{"ChrForward", true}, {"ChrForward"}, {"ChrBackward"},
                        {"ChrStrafeLeft"},    {"ChrRun"},     {"unrelated"}};
    controls.reads = 0;
    snapshot.capabilities = {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrRun"};
    allowed = permissions();
    REQUIRE((!allowed.forward && allowed.backward && allowed.left && !allowed.right && allowed.run));
    REQUIRE(controls.reads == int(controls.entries.size()));
}
