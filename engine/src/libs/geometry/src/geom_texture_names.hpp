// The textures a model file (.gm, rdf.h) loads: the rule GEOM loads them by, and their names read from the file's
// bytes without building it, which a model loaded in the background (geom_deferred.cpp) reads ahead.
#pragma once

#include "../../util/include/istring.hpp"
#include "rdf.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace storm::geometry
{
// The base name of a texture name: after its last folder, without its extensions ("Props\\Box.tga.tx" -> "box").
inline std::string TextureBaseName(std::string_view name)
{
    if (const size_t slash = name.find_last_of("\\/"); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    return AsciiLower(std::string(name.substr(0, name.find('.'))));
}

// Which texture slot of a model each slot loads (-1: none). A slot no material uses loads nothing: models keep names
// nothing draws (empty ones, citiz_50.gm slots 3 and 5; an exporter's leftovers, Fonar_Street2_01.gm
// "Fonar_Street2.dds" beside "Props\\Fonar_Street2.dds"). A name without a folder loads the model's slot of the same
// base name with a folder, where it has one: an exporter that wrote the folder into some slots of a model left
// others bare (BagRope_01.gm: "SN_Bagrope.tga" and "Props\\SN_Bagrope.dds"; the bare name is in no folder the model's
// textures are looked for in).
inline std::vector<int32_t> TextureSlotsToLoad(const std::vector<std::string_view> &names,
                                               const std::vector<bool> &referenced)
{
    const auto foldered = [](std::string_view name) { return name.find_first_of("\\/") != std::string_view::npos; };
    std::vector<int32_t> load(names.size(), -1);
    for (size_t t = 0; t < names.size(); ++t)
    {
        if (!referenced[t] || names[t].empty())
            continue;
        load[t] = int32_t(t);
        if (foldered(names[t]))
            continue;
        const std::string base = TextureBaseName(names[t]);
        for (size_t other = 0; other < names.size(); ++other)
            if (foldered(names[other]) && TextureBaseName(names[other]) == base)
            {
                load[t] = int32_t(other);
                break;
            }
    }
    return load;
}

// The texture names a model file loads (TextureSlotsToLoad), from its bytes: RDF_HEAD, the names, their indices, the
// textures' name offsets, the materials. Empty when the file is too short for what its header says (the build
// reports it).
inline std::vector<std::string> TextureNames(const std::vector<char> &file)
{
    std::vector<std::string> result;
    RDF_HEAD head;
    if (file.size() < sizeof(head))
        return result;
    std::memcpy(&head, file.data(), sizeof(head));
    if (!RdfVersionSupported(head.version) || head.name_size <= 0 || head.names < 0 || head.ntextures <= 0 ||
        head.nmaterials < 0)
        return result;
    const size_t names = sizeof(head);
    const size_t offsets = names + size_t(head.name_size) + size_t(head.names) * sizeof(int32_t);
    const size_t materials = offsets + size_t(head.ntextures) * sizeof(int32_t);
    if (file.size() < materials + size_t(head.nmaterials) * sizeof(RDF_MATERIAL))
        return result;
    const char *strings = file.data() + names;
    std::vector<std::string_view> slots(size_t(head.ntextures));
    for (int32_t t = 0; t < head.ntextures; t++)
    {
        int32_t offset;
        std::memcpy(&offset, file.data() + offsets + size_t(t) * sizeof(int32_t), sizeof(offset));
        if (offset < 0 || offset >= head.name_size)
            continue;
        const size_t length = strnlen(strings + offset, size_t(head.name_size - offset));
        if (length < size_t(head.name_size - offset))
            slots[size_t(t)] = std::string_view(strings + offset, length);
    }
    std::vector<bool> referenced(slots.size(), false);
    for (int32_t m = 0; m < head.nmaterials; m++)
    {
        RDF_MATERIAL material;
        std::memcpy(&material, file.data() + materials + size_t(m) * sizeof(material), sizeof(material));
        for (int stage = 0; stage < 4; stage++)
            if (material.texture_type[stage] != TEXTURE_NONE && material.texture[stage] >= 0 &&
                material.texture[stage] < head.ntextures)
                referenced[size_t(material.texture[stage])] = true;
    }
    for (const int32_t slot : TextureSlotsToLoad(slots, referenced))
        if (slot >= 0)
            result.emplace_back(slots[size_t(slot)]);
    return result;
}
// A game without wind marks: whether a texture is a plant's by its name (palm, tree, fern, leaf, leaves, listik,
// flora, flower, sugarcane; any case), not a backdrop's ("back...") nor under water. Its count on «Каждому своё»'s
// location models is in renderer_bgfx/README.md, "Wind".
inline bool VegetationTexture(std::string name)
{
    name = AsciiLower(std::move(name));
    static constexpr std::array<const char *, 9> kWords = {"palm",   "tree",  "fern",   "leaf",     "leaves",
                                                           "listik", "flora", "flower", "sugarcane"};
    if (name.starts_with("back") || name.find("underwater") != std::string::npos)
        return false;
    for (const char *word : kWords)
        if (name.find(word) != std::string::npos)
            return true;
    return false;
}
} // namespace storm::geometry
