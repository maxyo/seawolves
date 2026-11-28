// Caribbean Legend's foliage files (README.md): the settings, a place's trees and a tree asset. Each reader checks its
// whole input and refuses it with the first problem named; what it returns is trusted from then on.
#pragma once

#include <storm_glm.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::zip
{
class Archive;
}

namespace storm::foliage
{

// Resource/INI/foliage.json: the keys this engine draws with.
struct Settings
{
    bool enabled = false;
    float draw_distance = 0, impostor_distance = 0, lod_scale = 0; // land, metres
    float island_draw_distance = 0, island_density = 0;            // islands seen from the sea; density 0..1
};
std::optional<Settings> ReadSettings(std::string_view json, std::string &error);

// <folder>.foliage: the trees of one location or island model folder, in the world frame.
struct Instance
{
    uint32_t asset = 0; // index into Place::assets
    uint32_t rank = 0;  // the id's rank (selection.hpp)
    glm::mat4 transform{};
};
struct Place
{
    std::vector<std::string> assets; // resources as written, lower case: "foliage/<name>.bmfoliage"
    std::vector<Instance> instances;
};
std::optional<Place> ReadPlace(std::string_view json, std::string &error);

// Models/Foliage/<name>.bmfoliage: a zip of a manifest, the meshes of every level, their per-vertex attributes and the
// DDS textures.
inline constexpr size_t kAttributeChannels = 18; // two floats each, in the order of kChannelNames
extern const std::array<std::string_view, kAttributeChannels> kChannelNames;

struct MeshVertex
{
    float position[3]; // metres, y up
    float normal[3];
    float uv[2];
    float occlusion;
    uint32_t color; // RGBA8
};
static_assert(sizeof(MeshVertex) == 40);

struct Material
{
    std::string name;
    bool two_sided = false;
    bool billboard = false; // the last level's atlas of views
    float alpha_cutoff = 0;
    std::string color, normal_gloss, subsurface_ao; // texture entries of the archive
};
struct Mesh
{
    uint32_t lod = 0, material = 0;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices; // triangle list
    std::vector<float> attributes; // kAttributeChannels * 2 per vertex
};
struct Texture
{
    std::string path, sha256;
    uint32_t bytes = 0;
};
struct Asset
{
    std::string name;
    std::vector<float> lod_screen_heights; // decreasing: the screen-height share at which each level gives way
    uint32_t lods = 0;                     // levels 0..lods-1; the last one is the billboard level
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Texture> textures;
};
std::optional<Asset> ReadAsset(const zip::Archive &archive, std::string &error);

} // namespace storm::foliage
