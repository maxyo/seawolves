// What the coast foam's vertex shader (shaders/ports/vs_port_coastfoam.sc) reads, without engine dependencies so
// tests/renderer_bgfx/coast_foam_vertex_check draws it as CoastFoam does.
#pragma once

#include "sea_surface_gpu.hpp"

#include <cstdint>

namespace foam_gpu
{

constexpr int32_t kFirstStrand = 9;
constexpr int32_t kFoamsPerDraw = 64; // c9.. of vs_port_coastfoam.sc (PORT_VSC_COUNT 73)
constexpr int32_t kRegisters = kFirstStrand + kFoamsPerDraw;
constexpr int32_t kRows = 8; // rows of a strand

#pragma pack(push, 1)
struct Vertex
{
    float x0, z0, x1, z1; // the work part's ends
    float tu;             // its texture u
    uint8_t row;          // 0..7
    uint8_t edge;         // the strand's alpha at its ends: 0..4 quarters
    uint8_t foamLow, foamHigh;
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 24);

// The alpha of column x of iLen at the strand's ends (the CPU path's fAlpha1), in quarters.
inline uint8_t edge_quarters(int32_t x, int32_t iLen)
{
    int32_t quarters = 4;
    if (x <= 4)
        quarters = x;
    if (x >= iLen - 5)
        quarters = (iLen - 1) - x;
    return static_cast<uint8_t>(quarters < 0 ? 0 : quarters > 4 ? 4 : quarters);
}

// c4..c6: the sea's cascades (sea_gpu::cascade_register) for world positions: the origin at x = z = 0.
inline void cascade_registers(int32_t size, int32_t cascades, const float *patch, float out[3][4])
{
    for (int32_t c = 0; c < sea_gpu::kMaxCascades; c++)
        if (c < cascades)
            sea_gpu::cascade_register(0.0, 0.0, size, patch[c], c, out[c]);
        else
            sea_gpu::absent_cascade(out[c]);
}

} // namespace foam_gpu
