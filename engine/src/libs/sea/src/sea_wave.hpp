// The sea's height field sampled on the CPU (SEA::WaveXZ, the CPU surface); the GPU samples it the same way
// (sea_surface_gpu.hpp).
#pragma once

#include "math_inlines.h"
#include "sea_surface_gpu.hpp"

namespace storm::sea
{
// A wave's two frames and the blend between them: a texel's height (0..1, the bytes / 255) and normal (x, z of
// SEA's aNormals).
struct FrameBlend
{
    const uint8_t *h1 = nullptr, *h2 = nullptr;
    const uint32_t *n1 = nullptr, *n2 = nullptr;
    float delta = 0.0f;

    float Height(uint32_t texel) const
    {
        const float f1 = h1[texel], f2 = h2[texel];
        return (1.0f / 255.0f) * (f1 + (f2 - f1) * delta);
    }
    void Normal(uint32_t texel, float &nx, float &nz) const
    {
        const float nx1 = static_cast<float>(static_cast<short>(n1[texel])) / 32767.5f;
        const float nx2 = static_cast<float>(static_cast<short>(n2[texel])) / 32767.5f;
        const float nz1 = static_cast<float>(static_cast<short>(n1[texel] >> 0x10)) / 32767.5f;
        const float nz2 = static_cast<float>(static_cast<short>(n2[texel] >> 0x10)) / 32767.5f;
        nx = nx1 + (nx2 - nx1) * delta;
        nz = nz1 + (nz2 - nz1) * delta;
    }
};

// One of the sea's two waves: its frames' blend, texels per metre, amplitude and move.
struct Wave
{
    FrameBlend blend;
    float scale = 0.0f, amp = 0.0f;
    glm::vec3 move{};
};

// One wave at the world position (x, z): its height in metres (the frames' blend sampled bilinearly, wrapping) and,
// when `normal` is given, the bilinear blend of the per-texel normals' x and z (most callers ask for the height only).
inline float SampleWave(const Wave &wave, float x, float z, glm::vec2 *normal)
{
    constexpr int32_t kSize = sea_gpu::kSize;
    const float x1 = (x + wave.move.x) * wave.scale;
    const float z1 = (z + wave.move.z) * wave.scale;
    int32_t iX1 = ffloor(x1 + 0.0f), iX2 = iX1 + 1;
    int32_t iY1 = ffloor(z1 + 0.0f), iY2 = iY1 + 1;
    const float fX = (x1 - iX1);
    const float fZ = (z1 - iY1);
    iX1 &= (kSize - 1);
    iX2 &= (kSize - 1);
    iY1 &= (kSize - 1);
    iY2 &= (kSize - 1);

    const FrameBlend &b = wave.blend;
    const float a1 = b.Height(iX1 + iY1 * kSize);
    const float a2 = b.Height(iX2 + iY1 * kSize);
    const float a3 = b.Height(iX1 + iY2 * kSize);
    const float a4 = b.Height(iX2 + iY2 * kSize);

    if (normal)
    {
        float nx1, nx2, nx3, nx4, nz1, nz2, nz3, nz4;
        b.Normal(iX1 + iY1 * kSize, nx1, nz1);
        b.Normal(iX2 + iY1 * kSize, nx2, nz2);
        b.Normal(iX1 + iY2 * kSize, nx3, nz3);
        b.Normal(iX2 + iY2 * kSize, nx4, nz4);
        normal->x = (nx1 + fX * (nx2 - nx1) + fZ * (nx3 - nx1) + fX * fZ * (nx4 + nx1 - nx2 - nx3));
        normal->y = (nz1 + fX * (nz2 - nz1) + fZ * (nz3 - nz1) + fX * fZ * (nz4 + nz1 - nz2 - nz3));
    }
    return wave.amp * (a1 + fX * (a2 - a1) + fZ * (a3 - a1) + fX * fZ * (a4 + a1 - a2 - a3));
}

// The surface of the two waves at (x, z), within the sea's reach: its height, and its unit normal in `pNormal`.
inline float SurfaceXZ(const Wave (&waves)[2], float x, float z, glm::vec3 *pNormal)
{
    glm::vec2 n1, n2;
    float fRes = SampleWave(waves[0], x, z, pNormal ? &n1 : nullptr);
    fRes += SampleWave(waves[1], x, z, pNormal ? &n2 : nullptr);
    if (!pNormal)
        return fRes;
    const float nY1 = sqrtf(1.0f - (Sqr(n1.x) + Sqr(n1.y)));
    const float nY2 = sqrtf(1.0f - (Sqr(n2.x) + Sqr(n2.y)));
    glm::vec3 vNormal;
    vNormal.x = waves[0].scale * waves[0].amp * n1.x + waves[1].scale * waves[1].amp * n2.x;
    vNormal.z = waves[0].scale * waves[0].amp * n1.y + waves[1].scale * waves[1].amp * n2.y;
    vNormal.y = nY1 + nY2;
    *pNormal = storm::math::normalizeOrZero(vNormal);
    return fRes;
}
} // namespace storm::sea
