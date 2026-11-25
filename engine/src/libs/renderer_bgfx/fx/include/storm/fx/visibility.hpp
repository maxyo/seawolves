// How open to the sky every point of a location is: one volume per place, part of its probes (fx/place_probes.hpp),
// sampled per pixel. Design: fx/README.md, "Place probes and visibility".
#pragma once

#include <storm_glm.h>

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

struct VisibilityVolume
{
    std::array<int, 3> size{0, 0, 0};   // cells along x, y, z
    glm::vec3 origin{0.0f, 0.0f, 0.0f}; // world corner of cell (0, 0, 0)
    float cell = 0.0f;
    std::vector<uint8_t> sky_open; // 2 per cell, x fastest, then y, then z

    bool Empty() const
    {
        return sky_open.empty();
    }
    size_t Cells() const
    {
        return static_cast<size_t>(size[0]) * size[1] * size[2];
    }
    // Trilinear between cell centres, clamped to the grid, as the GPU samples it: {sky, open}; {1, 1} when empty.
    std::array<float, 2> Sample(const glm::vec3 &world) const;
};

// `values` (one byte per cell of `volume`) read like Sample, 0..1.
float SampleCells(const VisibilityVolume &volume, const std::vector<uint8_t> &values, const glm::vec3 &world);

} // namespace storm::fx
