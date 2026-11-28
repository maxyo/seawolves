// The sea's surface on the GPU (shaders/include/sea_surface.sh): what SEA::WaveXZ reads on the CPU, from the same
// samples (src/libs/sea/src/ocean_field.hpp, Field::Point).
//
// The mesh is a flat grid of points of the still water (SEA rebuilds it only when its blocks change); the vertex shader
// moves each point by the wave field's displacement, the sum over the cascades of the bilinear sample of each, and
// takes the displaced surface's normal from the same samples' derivatives. The field is one 2D RGBA32F texture read
// with texelFetch, the format and binding of the skinned draws' bone palettes (the vertex texture fetch proven on every
// device): the cascades stacked, cascade c in rows c * size .. (c + 1) * size - 1, a texel the sample's (dx, h, dz, 0)
// as Field::Samples holds it.
#pragma once

#include <cmath>
#include <cstdint>

namespace sea_gpu
{
inline constexpr int32_t kMaxCascades = 3;

// Vertex constant registers of the surface (c4..c8: free in every sea vertex port).
inline constexpr uint32_t kFirstRegister = 4;
inline constexpr uint32_t kRegisters = 5;
//   c4..c6  cascade 0..2 (cascade_register): x samples per metre, yz the sample coordinate of the mesh's origin
//           (wrapped to one period), w the cascade's first row in the texture, -1 for a cascade the field has not
//   c7      xy the mesh's origin minus the sea's vertex offset (x, z: the drawn positions are near the camera), z the
//           still water's level (Field::MeanLevel), w 0
//   c8      xy the camera minus the mesh's origin (x, z), z the sea's reach squared (beyond it the sea is flat at the
//           still level, as SEA::WaveXZ returns), w the source of the vertices: 1 the shader displaces the flat grid,
//           0 the CPU wrote them displaced (position and normal; SEA's path until the self-check passed)
inline constexpr float kSurfaceGpu = 1.0f;
inline constexpr float kSurfaceCpu = 0.0f;

// A cascade's register: the sample coordinate of the origin (x, z, metres) wrapped to one period of the cascade in
// double, so the shader's numbers stay small and land on the same samples as the CPU's world positions.
inline void cascade_register(double originX, double originZ, int32_t size, float patch, int32_t cascade, float out[4])
{
    const double perMetre = static_cast<double>(size) / patch;
    const auto wrap = [&](double v) {
        const double t = v * perMetre;
        return static_cast<float>(t - std::floor(t / size) * size);
    };
    out[0] = static_cast<float>(perMetre);
    out[1] = wrap(originX);
    out[2] = wrap(originZ);
    out[3] = static_cast<float>(cascade * size);
}

inline void absent_cascade(float out[4])
{
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = -1.0f;
}
} // namespace sea_gpu
