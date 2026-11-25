// The GLSL scalar functions the shader includes shared with the CPU checks use (fx_wake_trail.sh,
// sea_wakes_math.sh), for including those files in a check program. Functions of float, not templates: the shaders'
// literals are doubles here and convert.
#pragma once

#include <algorithm>
#include <cmath>

namespace shader
{
inline float abs(float x)
{
    return std::fabs(x);
}
inline float exp(float x)
{
    return std::exp(x);
}
inline float sin(float x)
{
    return std::sin(x);
}
inline float sqrt(float x)
{
    return std::sqrt(x);
}
inline float max(float a, float b)
{
    return a > b ? a : b;
}
inline float min(float a, float b)
{
    return a < b ? a : b;
}
inline float clamp(float x, float low, float high)
{
    return min(max(x, low), high);
}
inline float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}
inline float smoothstep(float a, float b, float x)
{
    const float t = clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace shader
