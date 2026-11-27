#pragma once

// The camera shake a game's scripts start (SetCameraShake): noise on the camera's position and angles, fading out
// over its time. Free of the engine: tests/camera_shake runs it as it is.

#include <storm_glm.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace storm::render
{
struct CameraShakeRequest
{
    float time = 0.0f;           // seconds; 0 stops a shake, below 0 shakes until stopped and never fades
    float move_frequency = 0.0f; // of the position's noise, per second
    float turn_frequency = 0.0f; // of the angles' noise, per second
    float move_range = 0.0f;     // metres
    float turn_range = 0.0f;     // radians
    bool camera_axes = false;    // the position moves along the camera's right and up, else the world's x and y
    bool scaled = false;         // the scripts scale it each frame (event CamShakeScaler)
    int32_t fade = -1;           // how it fades over its time: 0..8 (the scripts' CAM_EASING_*), any other none
};

struct CameraShakeOffset
{
    glm::vec2 move{}; // right/up or world x/y, metres
    glm::vec3 turn{}; // pitch, yaw, roll, radians
};

class CameraShake
{
  public:
    void Start(const CameraShakeRequest &request, uint32_t seed)
    {
        request_ = request;
        active_ = request.time != 0.0f;
        elapsed_ = 0.0f;
        seed_ = static_cast<float>(seed % 1024u);
    }

    bool Active() const
    {
        return active_;
    }

    bool Scaled() const
    {
        return active_ && request_.scaled;
    }

    bool CameraAxes() const
    {
        return request_.camera_axes;
    }

    void Advance(float seconds)
    {
        if (!active_)
            return;
        elapsed_ += seconds;
        if (request_.time > 0.0f && elapsed_ >= request_.time)
            active_ = false;
    }

    // `scale`: the scripts' factor of this frame (1 when the shake is not scaled).
    CameraShakeOffset Offset(float scale) const
    {
        if (!active_)
            return {};
        float amplitude = scale;
        if (request_.time > 0.0f)
            amplitude *= 1.0f - Ease(request_.fade, elapsed_ / request_.time);
        const float move = elapsed_ * request_.move_frequency, turn = elapsed_ * request_.turn_frequency;
        const float a_move = amplitude * request_.move_range, a_turn = amplitude * request_.turn_range;
        return {{Noise(move, 0.0f, 6) * a_move, Noise(move, 1.0f, 6) * a_move},
                {Noise(turn, 2.0f, 4) * a_turn, Noise(turn, 3.0f, 4) * a_turn, Noise(turn, 4.0f, 4) * a_turn}};
    }

    // The scripts' easing curves at x in 0..1 (0 at the start, 1 at the end); another kind: 0 (no fade).
    static float Ease(int32_t kind, float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        switch (kind)
        {
        case 0:
            return x * x * (3.0f - 2.0f * x);
        case 1:
            return x;
        case 2:
            return x * x;
        case 3:
            return x * x * x;
        case 4:
            return x * x * x * x;
        case 5:
            return 1.0f - std::cos(x * glm::half_pi<float>());
        case 6:
            return 1.0f - std::sqrt(1.0f - x * x);
        case 7:
            return x * x / (x * x + (1.0f - x) * (1.0f - x));
        case 8: // an elastic ease-in
            return x <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (x - 1.0f)) * std::sin(6.5f * glm::pi<float>() * x);
        default:
            return 0.0f;
        }
    }

  private:
    // Octaves of gradient noise along one axis of its own, within -0.5..0.5.
    float Noise(float x, float axis, int octaves) const
    {
        float sum = 0.0f, weight = 0.0f, amplitude = 1.0f, frequency = 1.0f;
        for (int i = 0; i < octaves; ++i)
        {
            sum +=
                amplitude * glm::perlin(glm::vec2(x * frequency, seed_ + axis * 37.0f + static_cast<float>(i) * 0.5f));
            weight += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return 0.5f * sum / weight;
    }

    CameraShakeRequest request_;
    bool active_ = false;
    float elapsed_ = 0.0f;
    float seed_ = 0.0f;
};
} // namespace storm::render
