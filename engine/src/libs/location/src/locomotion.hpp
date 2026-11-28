// Directional locomotion (Caribbean Legend's land movement): the hero moves in eight directions relative to where he
// faces, at a walk, a run or a sprint, with start and stop clips. The pure part, shared by the player's input, the
// NPCs and the animation choice of Character. A character walks this way when its scripts give it the directional
// actions (actions.forward_walk ...); otherwise by heading (forward / back along it, turning: actions.walk ...).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace storm::location
{

// In the order the games' engines number the directions.
enum class MoveDir : uint8_t
{
    none,
    forward,
    back,
    right,
    left,
    forwardRight,
    forwardLeft,
    backRight,
    backLeft,
};
inline constexpr size_t kMoveDirs = 8; // without none; table index = dir - 1

enum class Gait : uint8_t
{
    walk,
    run,
    sprint,
};
inline constexpr size_t kGaits = 3;

// What every input (keys, gamepad stick, touch stick, an NPC's travel) asks of the legs.
struct MoveIntent
{
    MoveDir dir = MoveDir::none;
    bool run = false;
};

constexpr size_t dirIndex(MoveDir dir)
{
    return static_cast<size_t>(dir) - 1;
}

// Held direction keys; opposite keys cancel each other.
constexpr MoveDir directionFromKeys(bool forward, bool back, bool right, bool left)
{
    if (forward && back)
        forward = back = false;
    if (right && left)
        right = left = false;
    if (forward)
        return right ? MoveDir::forwardRight : left ? MoveDir::forwardLeft : MoveDir::forward;
    if (back)
        return right ? MoveDir::backRight : left ? MoveDir::backLeft : MoveDir::back;
    if (right)
        return MoveDir::right;
    if (left)
        return MoveDir::left;
    return MoveDir::none;
}

// A stick (x right, y forward, each -1..1): an axis past the dead zone is a held key; past the run line, a run.
inline constexpr float kStickDeadZone = 0.2f;
inline constexpr float kStickRun = 0.6f;
constexpr MoveIntent intentFromStick(float x, float y)
{
    const auto magnitude = [](float v) { return v < 0.0f ? -v : v; };
    return {directionFromKeys(y > kStickDeadZone, y<-kStickDeadZone, x> kStickDeadZone, x < -kStickDeadZone),
            magnitude(x) > kStickRun || magnitude(y) > kStickRun};
}

// A travel vector in the mover's own frame (right, forward): the nearest of the eight directions (none for zero).
MoveDir directionFromLocal(float right, float forward);

// The unit travel of a direction in the mover's frame.
struct LocalVector
{
    float right, forward;
};
LocalVector localVector(MoveDir dir);
// The unit travel of a direction in the world (x, z) for a mover heading `ay` (forward = (sin ay, cos ay)).
struct WorldVector
{
    float x, z;
};
WorldVector worldTravel(MoveDir dir, float ay);

// The names the games' scripts give the actions: attribute "fight_back-left_run", ani action "fight back-left run".
std::string_view dirName(MoveDir dir);
std::string_view gaitName(Gait gait);
std::string moveAttribute(bool fight, Gait gait, MoveDir dir);
std::string startAttribute(bool fight, Gait gait, MoveDir dir);

// The foot that last touched the ground (the clips' RStep / LStep events) and the stop clip it calls for: the
// other foot finishes the stride. Stop clips: plain, "_rstep", "_lstep".
enum class Foot : uint8_t
{
    none,
    right,
    left,
};
inline constexpr size_t kStopSteps = 3;
constexpr size_t stopStepAfter(Foot last)
{
    return last == Foot::right ? 2 : last == Foot::left ? 1 : 0;
}
std::string stopAttribute(Gait gait, MoveDir dir, size_t step);

// Stairs clips exist for the four straight directions; a diagonal climbs with the straight one of its half.
inline constexpr size_t kStairsDirs = 4;
size_t stairsIndex(MoveDir dir);
std::string stairsAttribute(bool fight, size_t stairsDir, bool up);

// The speed curve of a start or stop clip (the ani's "Easing" data) over its progress t in 0..1.
enum class Easing : uint8_t
{
    none,
    startCubic,
    startQuart,
    startQuint,
    startExpo,
    stopCubic,
    stopQuart,
    stopQuint,
    stopExpo,
};
std::optional<Easing> parseEasing(std::string_view name); // nullopt: a name this engine does not know
float ease(Easing easing, float t);

// The legs' state: the direction walked and whether a start or stop clip plays. Driven once per frame by the wanted
// direction and the clips the game has for it; a clip's end moves it on.
enum class StridePhase : uint8_t
{
    still,
    starting,
    moving,
    stopping,
};
struct Stride
{
    MoveDir dir = MoveDir::none; // kept through a stop clip: it plays the direction it ends
    StridePhase phase = StridePhase::still;
    Foot lastStep = Foot::none;

    // hasStart / hasStop: the game has the start clip of `wanted` / the stop clip of the current direction.
    void want(MoveDir wanted, bool hasStart, bool hasStop);
    void clipEnded();
    void reset();
};

} // namespace storm::location
