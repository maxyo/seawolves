// Mesh LOD chains: a model's objects <name>_lod1.._lodN are coarser copies of its object <name>, one of them drawn at a
// time by the size the object takes on screen (storm::scene_data::MeshLodProfile, the profile named by the model's
// material file, else "Default"). Levels switch at once: the profile's cross-fade is not drawn.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::geometry
{

struct MeshLods
{
    // Per object: its chain (-1: in none) and its level in it (0: the object the others stand in for).
    std::vector<int32_t> chain;
    std::vector<uint8_t> level;
    // Per chain: the object whose sphere measures it (the first of level 0) and the levels it has, ascending.
    std::vector<int32_t> measured;
    std::vector<std::vector<uint8_t>> levels;
};

// The chains of a model with these object names. A name <base>_lod<N> (N >= 1, any letter case) joins the chain of
// the object named <base>; without such an object it is an ordinary object (some models name objects so).
inline MeshLods FindMeshLods(std::span<const std::string_view> names)
{
    const auto lower = [](std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return out;
    };
    MeshLods lods;
    lods.chain.assign(names.size(), -1);
    lods.level.assign(names.size(), 0);
    std::unordered_map<std::string, size_t> base; // lower-case name -> first object of that name
    for (size_t o = 0; o < names.size(); ++o)
        base.try_emplace(lower(names[o]), o);
    std::unordered_map<size_t, int32_t> chains; // first base object -> chain
    for (size_t o = 0; o < names.size(); ++o)
    {
        const std::string name = lower(names[o]);
        const size_t mark = name.rfind("_lod");
        if (mark == std::string::npos || mark + 4 == name.size() ||
            !std::all_of(name.begin() + long(mark + 4), name.end(), [](unsigned char c) { return std::isdigit(c); }))
            continue;
        const int number = std::stoi(name.substr(mark + 4));
        const auto found = base.find(name.substr(0, mark));
        if (number < 1 || number > 255 || found == base.end())
            continue;
        const auto [entry, fresh] = chains.try_emplace(found->second, int32_t(lods.measured.size()));
        if (fresh)
        {
            lods.measured.push_back(int32_t(found->second));
            lods.levels.push_back({0});
        }
        lods.chain[o] = entry->second;
        lods.level[o] = uint8_t(number);
        lods.levels[size_t(entry->second)].push_back(uint8_t(number));
    }
    // Every object named as a chain's base is its level 0 (a base may be split into several objects).
    for (size_t o = 0; o < names.size(); ++o)
        if (lods.chain[o] < 0)
            if (const auto found = chains.find(base.at(lower(names[o]))); found != chains.end())
                lods.chain[o] = found->second;
    for (auto &levels : lods.levels)
    {
        std::ranges::sort(levels);
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    }
    return lods;
}

// The level for an object that takes `screen` of the screen's height, `thresholds` falling (level i + 1 below
// thresholds[i]). `current`: the level chosen last (-1: none yet); a change needs `hysteresis` past the threshold,
// so an object at a threshold does not switch back and forth.
inline int MeshLodLevel(float screen, int current, std::span<const float> thresholds, float hysteresis)
{
    const int count = int(thresholds.size());
    int level = 0;
    while (level < count && screen < thresholds[size_t(level)])
        ++level;
    if (current >= 0 && level > current)
    {
        level = current;
        while (level < count && screen < thresholds[size_t(level)] * (1.0f - hysteresis))
            ++level;
    }
    else if (current >= 0 && level < current)
    {
        level = current;
        while (level > 0 && screen >= thresholds[size_t(level - 1)] * (1.0f + hysteresis))
            --level;
    }
    return level;
}

// The level of the chain drawn for `level`: a level the chain lacks (`levels`, ascending, 0 first) is drawn with the
// finer one before it.
inline int MeshLodDrawn(int level, std::span<const uint8_t> levels)
{
    int drawn = 0;
    for (const uint8_t l : levels)
        if (l <= level)
            drawn = l;
    return drawn;
}

} // namespace storm::geometry
