#pragma once
#include "android_combat_input.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace storm::android
{
struct MovementPermission
{
    bool forward = false, backward = false, left = false, right = false, run = false;
};
struct MovementAxes
{
    float x = 0, y = 0, magnitude = 0;
    bool runAllowed = false;
};
inline MovementAxes movementAxes(int x, int y, const MovementPermission &permission, bool wasMoving = false)
{
    if ((x < 0 && !permission.left) || (x > 0 && !permission.right))
        x = 0;
    if ((y < 0 && !permission.backward) || (y > 0 && !permission.forward))
        y = 0;
    const float length = std::hypot(float(x), float(y));
    // Radial hysteresis prevents a resting thumb from repeatedly starting and
    // stopping. Normalize direction only; radius selects a gait, never speed.
    if (length <= (wasMoving ? 120.0f : 180.0f))
        return {0, 0, 0, permission.run};
    return {x / length, y / length, std::min(length, 1000.0f) / 1000.0f, permission.run};
}
inline bool movementOwnerCurrent(std::string_view owner, const GameplaySnapshot &current)
{
    return !owner.empty() && current.inputOwner == owner && (current.mode == "explore" || current.mode == "melee");
}
// What the stick may do now: each of the five controls by its first descriptor, unlocked, bound (a control tree or a
// key) and among the gameplay's capabilities. Locked and unmapped names are resolved too; nothing is cached, so the
// next call sees remapping and the current locks. The descriptor type and its two constants are the core's
// (USER_CONTROL, UCT_ControlTree, UNASSIGNED_CONTROL).
template <class Descriptor, class Controls>
MovementPermission movementPermission(Controls &controls, const std::vector<std::string> &capabilities, int controlTree,
                                      int unassigned)
{
    MovementPermission permission;
    unsigned processed = 0;
    const auto count = controls.GetControlsNum();
    for (int32_t i = 0; i < count; ++i)
    {
        Descriptor descriptor{};
        if (!controls.GetControlDesc(i, descriptor) || !descriptor.name)
            continue;
        const std::string_view name = descriptor.name;
        unsigned bit = 0;
        bool *allowed = nullptr;
        if (name == "ChrForward")
            bit = 1, allowed = &permission.forward;
        else if (name == "ChrBackward")
            bit = 2, allowed = &permission.backward;
        else if (name == "ChrStrafeLeft")
            bit = 4, allowed = &permission.left;
        else if (name == "ChrStrafeRight")
            bit = 8, allowed = &permission.right;
        else if (name == "ChrRun")
            bit = 16, allowed = &permission.run;
        if (!bit || (processed & bit))
            continue;
        processed |= bit;
        *allowed = !descriptor.bLocked && !descriptor.bSystemLocked &&
                   (descriptor.control_type == controlTree || descriptor.system_code != unassigned) &&
                   std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end();
        if (processed == 31)
            break;
    }
    return permission;
}
inline bool ordinaryMovementAllowed(bool requested, bool moving, bool locked, bool priority, bool jumping,
                                    bool swimming, bool fighting, bool queuedFight, bool locomotion)
{
    return requested && moving && !locked && !priority && !jumping && !swimming && !fighting && !queuedFight &&
           locomotion;
}
// What a held stick does to the hero in this frame. Swimming takes it too: the native swim
// (its own pace, the waves) goes the stick's way, so the stick can steer him in water.
enum class TouchGait
{
    stop,
    swim,
    combatBack,
    combatSide,
    walk
};
inline TouchGait touchGait(const MovementAxes &axes, bool swimming, bool fighting)
{
    if (axes.magnitude <= 0.0f)
        return TouchGait::stop;
    if (swimming)
        return TouchGait::swim;
    if (fighting && axes.y < -0.22f)
        return TouchGait::combatBack;
    if (fighting && std::fabs(axes.y) < .22f)
        return TouchGait::combatSide;
    return TouchGait::walk;
}
struct MovementPace
{
    bool run;
    float speed;
};
// Two native gait speeds. Enter run at 80% travel and return to walk below
// 65%; permission changes always take effect immediately. Direction stays analog.
inline MovementPace movementPace(float magnitude, float walkSpeed, float runSpeed, bool canRun, bool wasRunning = false)
{
    if (magnitude <= 0.0f)
        return {false, 0.0f};
    walkSpeed = std::fabs(walkSpeed);
    runSpeed = std::max(walkSpeed, std::fabs(runSpeed));
    const bool run = canRun && magnitude >= (wasRunning ? .65f : .80f);
    return {run, run ? runSpeed : walkSpeed};
}
struct MovementDirection
{
    float x, z;
};
inline MovementDirection movementDirection(const MovementAxes &axes, float cameraX, float cameraZ, float fallbackYaw)
{
    const float length = std::hypot(cameraX, cameraZ);
    if (length > .001f)
    {
        cameraX /= length;
        cameraZ /= length;
    }
    else
    {
        cameraX = std::sin(fallbackYaw);
        cameraZ = std::cos(fallbackYaw);
    }
    return {cameraZ * axes.x + cameraX * axes.y, -cameraX * axes.x + cameraZ * axes.y};
}
// One latest-value slot, owned by the engine thread; the atomic cancellation epoch is the
// hard-cancel fence of the activity's cancel call. The stick is held from its "1" packet until its "0" packet (pointer
// up or cancel in the UI), a hard cancellation (lifecycle, screen, UI loss: cancelAndroidCombatInput), a new owner or a
// failing guard: events, never a clock. A UI frame may take seconds on a slow device; a held stick must not let go
// meanwhile.
class MovementInput
{
    inline static std::atomic<uint64_t> nextGeneration_{0};
    std::string owner_;
    uint64_t generation_ = 0, cancel_ = 0;
    int x_ = 0, y_ = 0;
    bool active_ = false, moving_ = false;
    void synchronizeCancel()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch != cancel_)
        {
            cancel_ = epoch;
            resetActivity();
        }
    }

  public:
    // nullopt means a hard context change; individual false bits mean a locked
    // or unmapped real native control and suppress only that requested direction.
    std::function<std::optional<MovementPermission>(std::string_view)> guard;
    void resetActivity()
    {
        generation_ = nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1;
        active_ = moving_ = false;
        x_ = y_ = 0;
    }
    void updateOwner(std::string owner)
    {
        synchronizeCancel();
        if (owner != owner_)
        {
            owner_ = std::move(owner);
            resetActivity();
        }
    }
    uint64_t generation()
    {
        synchronizeCancel();
        return generation_;
    }
    const std::string &owner() const
    {
        return owner_;
    }
    bool offer(const storm::ui::hud::Move &move,
               uint64_t enqueueEpoch = combatCancelEpoch.load(std::memory_order_acquire))
    {
        synchronizeCancel();
        if (enqueueEpoch != cancel_ || owner_.empty() || move.owner != owner_ || move.generation != generation_ ||
            !generation_ || move.x < -1000 || move.x > 1000 || move.y < -1000 || move.y > 1000 ||
            (!move.active && (move.x || move.y)))
            return false;
        if (!guard || !guard(owner_))
        {
            updateOwner({});
            return false;
        }
        x_ = move.x;
        y_ = move.y;
        active_ = move.active;
        if (!active_)
            moving_ = false;
        return true;
    }
    std::optional<MovementAxes> read()
    {
        synchronizeCancel();
        if (!active_)
            return std::nullopt;
        const auto permission = guard ? guard(owner_) : std::nullopt;
        if (!permission)
        {
            updateOwner({});
            return std::nullopt;
        }
        const auto axes = movementAxes(x_, y_, *permission, moving_);
        moving_ = axes.magnitude > 0.0f;
        return axes;
    }
};
inline MovementInput movementInput;
} // namespace storm::android
