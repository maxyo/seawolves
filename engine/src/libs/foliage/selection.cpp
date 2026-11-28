#include "selection.hpp"

#include <fnv_hash.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace storm::foliage
{

uint32_t Rank(std::string_view id)
{
    return static_cast<uint32_t>(hash::fnv1a(id) >> 32);
}

uint64_t RankLimit(float share)
{
    const double clamped = std::clamp(static_cast<double>(share), 0.0, 1.0);
    return static_cast<uint64_t>(std::llround(clamped * 4294967296.0));
}

std::span<const uint32_t> Cells::drawn(const Place &place, uint32_t cell, uint64_t limit) const
{
    const auto first = order.begin() + begin[cell], last = order.begin() + begin[cell + 1];
    const auto end = std::partition_point(first, last, [&](uint32_t i) { return place.instances[i].rank < limit; });
    return {order.data() + begin[cell], static_cast<size_t>(end - first)};
}

Cells BuildCells(const Place &place, float size)
{
    Cells cells;
    cells.size = size;
    if (place.instances.empty())
    {
        cells.begin = {0};
        return cells;
    }
    glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
    for (const Instance &instance : place.instances)
    {
        const glm::vec2 at(instance.transform[3].x, instance.transform[3].z);
        low = glm::min(low, at);
        high = glm::max(high, at);
    }
    cells.origin = low;
    cells.columns = static_cast<uint32_t>((high.x - low.x) / size) + 1;
    cells.rows = static_cast<uint32_t>((high.y - low.y) / size) + 1;
    const auto cell_of = [&](const Instance &instance) {
        const uint32_t column =
            std::min(cells.columns - 1, static_cast<uint32_t>((instance.transform[3].x - low.x) / size));
        const uint32_t row = std::min(cells.rows - 1, static_cast<uint32_t>((instance.transform[3].z - low.y) / size));
        return row * cells.columns + column;
    };
    std::vector<uint32_t> cell(place.instances.size());
    cells.begin.assign(size_t{cells.columns} * cells.rows + 1, 0);
    for (size_t i = 0; i < cell.size(); ++i)
    {
        cell[i] = cell_of(place.instances[i]);
        ++cells.begin[cell[i] + 1];
    }
    for (size_t c = 1; c < cells.begin.size(); ++c)
        cells.begin[c] += cells.begin[c - 1];
    cells.order.resize(place.instances.size());
    for (uint32_t i = 0; i < cells.order.size(); ++i)
        cells.order[i] = i;
    std::sort(cells.order.begin(), cells.order.end(), [&](uint32_t a, uint32_t b) {
        if (cell[a] != cell[b])
            return cell[a] < cell[b];
        if (place.instances[a].rank != place.instances[b].rank)
            return place.instances[a].rank < place.instances[b].rank;
        return a < b;
    });
    return cells;
}

} // namespace storm::foliage
