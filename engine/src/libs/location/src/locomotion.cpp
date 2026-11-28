#include "locomotion.hpp"

#include <cmath>

namespace storm::location
{

MoveDir directionFromLocal(float right, float forward)
{
    // tan(22.5 deg): an axis counts when the other is not more than this many times larger.
    constexpr float kOctant = 0.41421356f;
    const float r = std::fabs(right), f = std::fabs(forward);
    if (r * r + f * f <= 1e-6f)
        return MoveDir::none;
    return directionFromKeys(forward > 0.0f && f > r * kOctant, forward < 0.0f && f > r * kOctant,
                             right > 0.0f && r > f * kOctant, right < 0.0f && r > f * kOctant);
}

LocalVector localVector(MoveDir dir)
{
    constexpr float d = 0.70710678f;
    switch (dir)
    {
    case MoveDir::forward:
        return {0.0f, 1.0f};
    case MoveDir::back:
        return {0.0f, -1.0f};
    case MoveDir::right:
        return {1.0f, 0.0f};
    case MoveDir::left:
        return {-1.0f, 0.0f};
    case MoveDir::forwardRight:
        return {d, d};
    case MoveDir::forwardLeft:
        return {-d, d};
    case MoveDir::backRight:
        return {d, -d};
    case MoveDir::backLeft:
        return {-d, -d};
    case MoveDir::none:
        break;
    }
    return {0.0f, 0.0f};
}

WorldVector worldTravel(MoveDir dir, float ay)
{
    const auto local = localVector(dir);
    const float s = std::sin(ay), c = std::cos(ay);
    return {c * local.right + s * local.forward, c * local.forward - s * local.right};
}

std::string_view dirName(MoveDir dir)
{
    static constexpr std::array<std::string_view, kMoveDirs> names = {
        "forward", "back", "right", "left", "forward-right", "forward-left", "back-right", "back-left"};
    return names[dirIndex(dir)];
}

std::string_view gaitName(Gait gait)
{
    static constexpr std::array<std::string_view, kGaits> names = {"walk", "run", "sprint"};
    return names[static_cast<size_t>(gait)];
}

std::string moveAttribute(bool fight, Gait gait, MoveDir dir)
{
    return (fight ? "fight_" : "") + std::string(dirName(dir)) + "_" + std::string(gaitName(gait));
}

std::string startAttribute(bool fight, Gait gait, MoveDir dir)
{
    return "start_" + moveAttribute(fight, gait, dir);
}

std::string stopAttribute(Gait gait, MoveDir dir, size_t step)
{
    static constexpr std::array<std::string_view, kStopSteps> steps = {"", "_rstep", "_lstep"};
    return "stop_" + moveAttribute(false, gait, dir) + std::string(steps[step]);
}

size_t stairsIndex(MoveDir dir)
{
    switch (dir)
    {
    case MoveDir::back:
    case MoveDir::backRight:
    case MoveDir::backLeft:
        return 1;
    case MoveDir::right:
        return 2;
    case MoveDir::left:
        return 3;
    default:
        return 0;
    }
}

std::string stairsAttribute(bool fight, size_t stairsDir, bool up)
{
    return (fight ? "fight_" : "") + std::string(dirName(static_cast<MoveDir>(stairsDir + 1))) + "_walk_stairs_" +
           (up ? "up" : "down");
}

std::optional<Easing> parseEasing(std::string_view name)
{
    static constexpr std::array<std::pair<std::string_view, Easing>, 8> names = {{
        {"StartCubic", Easing::startCubic},
        {"StartQuart", Easing::startQuart},
        {"StartQuint", Easing::startQuint},
        {"StartExpo", Easing::startExpo},
        {"StopCubic", Easing::stopCubic},
        {"StopQuart", Easing::stopQuart},
        {"StopQuint", Easing::stopQuint},
        {"StopExpo", Easing::stopExpo},
    }};
    for (const auto &[text, easing] : names)
        if (text == name)
            return easing;
    return std::nullopt;
}

float ease(Easing easing, float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float rest = 1.0f - t;
    // Start curves rise from 0 fast and settle at 1 (ease-out); stop curves fall from 1 and settle at 0.
    switch (easing)
    {
    case Easing::startCubic:
        return 1.0f - rest * rest * rest;
    case Easing::startQuart:
        return 1.0f - rest * rest * rest * rest;
    case Easing::startQuint:
        return 1.0f - rest * rest * rest * rest * rest;
    case Easing::startExpo:
        return t >= 1.0f ? 1.0f : 1.0f - std::exp2(-10.0f * t);
    case Easing::stopCubic:
        return rest * rest * rest;
    case Easing::stopQuart:
        return rest * rest * rest * rest;
    case Easing::stopQuint:
        return rest * rest * rest * rest * rest;
    case Easing::stopExpo:
        return t >= 1.0f ? 0.0f : std::exp2(-10.0f * t);
    case Easing::none:
        break;
    }
    return 1.0f;
}

void Stride::want(MoveDir wanted, bool hasStart, bool hasStop)
{
    if (wanted == MoveDir::none)
    {
        if (phase == StridePhase::starting || phase == StridePhase::moving)
        {
            phase = hasStop ? StridePhase::stopping : StridePhase::still;
            if (!hasStop)
                dir = MoveDir::none;
        }
        return;
    }
    if (phase == StridePhase::still || phase == StridePhase::stopping)
    {
        dir = wanted;
        phase = hasStart ? StridePhase::starting : StridePhase::moving;
    }
    else if (wanted != dir)
    {
        // A turn while under way changes the loop at once; the start clips are made from standing only.
        dir = wanted;
        phase = StridePhase::moving;
    }
}

void Stride::clipEnded()
{
    if (phase == StridePhase::starting)
        phase = StridePhase::moving;
    else if (phase == StridePhase::stopping)
        reset();
}

void Stride::reset()
{
    dir = MoveDir::none;
    phase = StridePhase::still;
    lastStep = Foot::none;
}

} // namespace storm::location
