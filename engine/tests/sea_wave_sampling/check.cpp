#include "../../src/libs/sea/src/sea_wave.hpp"
#include <cstring>
#include <random>
#include <vector>

#include <catch2/catch.hpp>

namespace
{
constexpr int kTexels = sea_gpu::kSize * sea_gpu::kSize;

struct Frames
{
    std::vector<uint8_t> h1, h2;
    std::vector<uint32_t> n1, n2;
    explicit Frames(unsigned seed) : h1(kTexels), h2(kTexels), n1(kTexels), n2(kTexels)
    {
        std::mt19937 random(seed);
        for (int i = 0; i < kTexels; ++i)
        {
            h1[i] = static_cast<uint8_t>(random()), h2[i] = static_cast<uint8_t>(random());
            n1[i] = random(), n2[i] = random();
        }
    }
    storm::sea::FrameBlend Blend(float delta) const
    {
        return {h1.data(), h2.data(), n1.data(), n2.data(), delta};
    }
};

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
} // namespace

// Most callers ask for the height only: it must be the very height the callers with a normal get (the sea's motion
// is the original's), at every texel phase, across the wrap and at any blend of the frames.
TEST_CASE("sea_wave_sampling: the height alone is the height with the normal, bit for bit")
{
    const Frames a(1), b(2);
    int compared = 0;
    for (const float delta : {0.0f, 0.37f, 0.999f})
    {
        const storm::sea::Wave waves[2] = {{a.Blend(delta), 0.35f, 6.0f, glm::vec3(37.5f, 0.0f, -12.25f)},
                                           {b.Blend(1.0f - delta), 1.3f, 1.5f, glm::vec3(-8.0f, 0.0f, 21.75f)}};
        for (float z = -400.0f; z < 400.0f; z += 3.17f)
            for (float x = -400.0f; x < 400.0f; x += 2.93f)
            {
                glm::vec3 normal;
                glm::vec2 waveNormal;
                REQUIRE(Bits(storm::sea::SurfaceXZ(waves, x, z, nullptr)) ==
                        Bits(storm::sea::SurfaceXZ(waves, x, z, &normal)));
                REQUIRE(Bits(storm::sea::SampleWave(waves[1], x, z, nullptr)) ==
                        Bits(storm::sea::SampleWave(waves[1], x, z, &waveNormal)));
                ++compared;
            }
    }
    CHECK(compared > 200000);
}

// At a texel's corner the bilinear sample is that texel: its height byte / 255 times the amplitude, its packed
// normal's shorts / 32767.5, blended between the two frames.
TEST_CASE("sea_wave_sampling: a texel's own height and normal at its corner")
{
    Frames frames(3);
    const int texel = 5 + 9 * sea_gpu::kSize;
    frames.h1[texel] = 51, frames.h2[texel] = 153;
    frames.n1[texel] = 16384u | (static_cast<uint32_t>(static_cast<uint16_t>(-8192)) << 16);
    frames.n2[texel] = 0;
    const storm::sea::Wave wave{frames.Blend(0.5f), 0.5f, 2.0f, glm::vec3(0.0f)};
    glm::vec2 normal{};
    const float height = storm::sea::SampleWave(wave, 10.0f, 18.0f, &normal);
    CHECK(height == Approx(2.0f * 102.0f / 255.0f).epsilon(1e-6));
    CHECK(normal.x == Approx(0.5f * 16384.0f / 32767.5f).epsilon(1e-6));
    CHECK(normal.y == Approx(0.5f * -8192.0f / 32767.5f).epsilon(1e-6));
    CHECK(storm::sea::SampleWave(wave, 10.0f, 18.0f, nullptr) == height);
}
