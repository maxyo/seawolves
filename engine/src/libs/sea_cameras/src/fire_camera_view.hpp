#pragma once

// The aiming camera's azimuth, from its locator's direction: at most an eighth of a turn either way.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace storm::sea_cameras
{
inline constexpr float kAimAyLimit = std::numbers::pi_v<float> / 4.0f;

inline float WrapAngle(float angle)
{
    return std::remainder(angle, 2.0f * std::numbers::pi_v<float>);
}

// The azimuth the camera starts at, given from the ship's heading, for a locator facing `locatorAy` on the ship.
inline float AimStartAy(float shipAy, float locatorAy)
{
    return std::clamp(WrapAngle(shipAy - locatorAy), -kAimAyLimit, kAimAyLimit);
}

// One frame's turn by the FireCamera_Turn_H control.
inline float AimTurnAy(float ay, float control, float sensitivityAzimuth, float mouseSensitivity, float mouseSpeedAy)
{
    return std::clamp(ay + 2.0f * sensitivityAzimuth * mouseSensitivity * mouseSpeedAy * control, -kAimAyLimit,
                      kAimAyLimit);
}
} // namespace storm::sea_cameras
