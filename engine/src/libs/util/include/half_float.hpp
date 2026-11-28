#pragma once

#include <bit>
#include <cstdint>

// IEEE half floats without branches on the common path: glm's packHalf takes ~4 ns a value in software and
// _Float16 is a library call on x86 without F16C. After F. Giesen's public-domain float_to_half_fast3_rtne and
// half_to_float (rounding to nearest even, NaN and infinity kept).
namespace storm
{
inline uint16_t HalfFromFloat(float value)
{
    constexpr uint32_t kInfinity = 255u << 23, kHalfMax = (127u + 16u) << 23;
    constexpr uint32_t kDenormMagic = ((127u - 15u) + (23u - 10u) + 1u) << 23;
    uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = bits & 0x80000000u;
    bits ^= sign;
    uint32_t out;
    if (bits >= kHalfMax)
        out = bits > kInfinity ? 0x7e00u : 0x7c00u;
    else if (bits < (113u << 23)) // a subnormal half or zero
        out = std::bit_cast<uint32_t>(std::bit_cast<float>(bits) + std::bit_cast<float>(kDenormMagic)) - kDenormMagic;
    else
    {
        const uint32_t odd = (bits >> 13) & 1u;
        bits += (static_cast<uint32_t>(15 - 127) << 23) + 0xfffu + odd;
        out = bits >> 13;
    }
    return static_cast<uint16_t>(out | (sign >> 16));
}

inline float FloatFromHalf(uint16_t half)
{
    constexpr uint32_t kShiftedExponent = 0x7c00u << 13;
    uint32_t bits = (half & 0x7fffu) << 13;
    const uint32_t exponent = kShiftedExponent & bits;
    bits += (127u - 15u) << 23;
    if (exponent == kShiftedExponent) // infinity or NaN
        bits += (128u - 16u) << 23;
    else if (exponent == 0) // zero or subnormal
        bits = std::bit_cast<uint32_t>(std::bit_cast<float>(bits + (1u << 23)) - std::bit_cast<float>(113u << 23));
    return std::bit_cast<float>(bits | (static_cast<uint32_t>(half & 0x8000u) << 16));
}

// Four values as one texel of an RGBA16F texture (x in the low bits).
inline uint64_t PackHalves(float x, float y, float z, float w)
{
    return static_cast<uint64_t>(HalfFromFloat(x)) | static_cast<uint64_t>(HalfFromFloat(y)) << 16 |
           static_cast<uint64_t>(HalfFromFloat(z)) << 32 | static_cast<uint64_t>(HalfFromFloat(w)) << 48;
}
} // namespace storm
