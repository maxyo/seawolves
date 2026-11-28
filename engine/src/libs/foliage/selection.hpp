// Which trees a density draws (README.md, "Selection"): every tree has a rank from its id, and the trees of each cell
// are kept in rank order, so a density is a prefix of every cell — the same trees each time, more density only adds.
#pragma once

#include "foliage_files.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace storm::foliage
{

// The top 32 bits of the id's 64-bit FNV-1a.
uint32_t Rank(std::string_view id);
// Trees whose rank is below this are drawn at `share` (clamped to 0..1); 1 draws all.
uint64_t RankLimit(float share);

// The place's trees on a square grid in the xz plane.
struct Cells
{
    float size = 0;
    glm::vec2 origin{}; // the grid's lowest x, z
    uint32_t columns = 0, rows = 0;
    std::vector<uint32_t> begin; // columns * rows + 1 offsets into `order`; cell = row * columns + column
    std::vector<uint32_t> order; // instance indices, each cell's by rank, then by index

    // The instances of `cell` drawn at `limit` (RankLimit).
    std::span<const uint32_t> drawn(const Place &place, uint32_t cell, uint64_t limit) const;
};
// `size` metres per cell (> 0).
Cells BuildCells(const Place &place, float size);

} // namespace storm::foliage
