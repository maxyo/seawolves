#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

namespace storm::render
{
// A vertex as the caller fills it: position and a packed ARGB colour (D3DCOLOR).
struct ColorVertex
{
    float x, y, z;
    uint32_t argb;
};
static_assert(sizeof(ColorVertex) == 16);
struct ColorDraw
{
    std::span<const float, 16> world;
    std::string_view material = "SkyFog";
    uint16_t view = 0;
};
// A coloured triangle mesh drawn through VRenderer (legacy_colored_mesh.hpp). Owns its buffers: destroy before
// the device. update() copies its input before returning.
class ColoredMesh
{
  public:
    virtual ~ColoredMesh() = default;
    // Nonempty indices replace topology; empty indices preserve it. First update
    // must provide triangle-list indices.
    virtual void update(std::span<const ColorVertex>, std::span<const uint16_t> indices = {}) = 0;
    virtual void submit(const ColorDraw &) = 0;
};
inline void validateColorMesh(std::span<const ColorVertex> vertices, std::span<const uint16_t> indices)
{
    if (vertices.empty() || vertices.size() > 65536 || indices.size() > UINT32_MAX || indices.size() % 3)
        throw std::invalid_argument("Invalid colored triangle mesh size");
    for (auto index : indices)
        if (index >= vertices.size())
            throw std::invalid_argument("Colored mesh index out of bounds");
}
} // namespace storm::render
