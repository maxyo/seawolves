// A place's probes: one set of rays for everything its still geometry gives the light that does not depend on the
// hour, cached in one file per place. Design: fx/README.md, "Place probes and visibility".
#pragma once

#include "storm/fx/ao_volume.hpp"
#include "storm/fx/sun_fit.hpp"
#include "storm/fx/visibility.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::fx
{

// Bump on any change of the probes that changes their output, or of the file's layout.
inline constexpr uint32_t kPlaceBakeVersion = 2;
inline constexpr int kProbeRays = 32; // over the sphere (with the occlusion)
inline constexpr int kSkyRays = 24;   // over the upper hemisphere, cosine-weighted (without it)
// The largest side of the grid: a 3D texture of GL_MAX_3D_TEXTURE_SIZE, which every GLES 3.0 device has.
inline constexpr int kProbeMaxSide = 256;

struct ProbeParams
{
    glm::vec3 lo{1.0f, 1.0f, 1.0f}; // the part of the world that gets cells; empty (lo > hi): the geometry's box
    glm::vec3 hi{-1.0f, -1.0f, -1.0f};
    float cell = 0.5f; // metres; grown while the box needs more than max_cells or a side over kProbeMaxSide
    uint32_t max_cells = 1u << 19;
    float reach = 60.0f; // metres: no ray runs farther (nor beyond the bounds of the geometry)
    // The cells' rays turned against each other: noise in place of bands between coarse cells (outdoors). Not with
    // the occlusion lobes, whose fine cells must agree with their neighbours.
    bool turned = true;
    float near_sky = 0.0f;  // metres; > 0: PlaceProbes::near_sky
    float ao_radius = 0.0f; // metres; > 0: PlaceProbes::ao (occluders farther than this do not count)
    // Points on the floor where one walks (the location's walk patch): the occlusion is raised so that this floor
    // reads as an open one (kAoWalkLift); empty: as measured.
    std::vector<glm::vec3> walk;
    unsigned threads = 1;
};

struct PlaceProbes
{
    VisibilityVolume visibility;
    std::vector<uint8_t> near_sky; // per cell of the volume, 0..255 (empty: not asked for)
    AoVolume ao;                   // on the volume's grid (empty: not asked for)
    uint64_t rays = 0;             // traced (the log)
    double cpu_ms = 0.0;           // processor time of the rays over all threads (the log; not cached)
    float walk_lift = 1.0f;        // what the occlusion was raised by for the walked floor (the log; not cached)

    bool Empty() const
    {
        return visibility.Empty();
    }
};

// `positions`, `indices`: the geometry the cells are laid over (its box, limited by params.lo..hi); empty when there
// is nothing to lay them over.
PlaceProbes BakePlaceProbes(const Occluders &occluders, std::span<const glm::vec3> positions,
                            std::span<const uint32_t> indices, const ProbeParams &params);

// What the rays of a place depend on, hashed once per place.
struct PlaceGeometry
{
    std::span<const glm::vec3> positions;
    std::span<const uint32_t> indices;
    std::span<const std::array<float, 2>> uvs; // per position (they place the alpha masks)
    std::span<const Occluders::Cut> cut;       // per triangle (empty: nothing is cut)
};
uint64_t PlaceKey(const PlaceGeometry &geometry, const ProbeParams &params);

// The place's cache file in memory.
enum class PlaceSection : uint32_t
{
    Probes = 1,      // EncodeProbes
    LightmapSet = 2, // the town light's (src/effects/town_light.cpp), keyed by the set's lightmaps
};
class PlaceFile
{
  public:
    // The section of `kind` and `key`, or nothing.
    std::span<const uint8_t> Find(PlaceSection kind, uint64_t key) const;
    // Adds or replaces it; of the sections of `kind` the `keep` put last stay (the lightmap sets of a town's hours).
    void Put(PlaceSection kind, uint64_t key, std::vector<uint8_t> bytes, size_t keep = 1);
    std::vector<uint8_t> Encode(uint64_t place_key) const;
    // Refuses (nullopt, `error` says why) a file of another place, version or a broken one.
    static std::optional<PlaceFile> Decode(std::span<const uint8_t> bytes, uint64_t place_key, std::string &error);

  private:
    struct Section
    {
        PlaceSection kind;
        uint64_t key;
        std::vector<uint8_t> bytes;
    };
    std::vector<Section> sections_; // oldest first
};

std::vector<uint8_t> EncodeProbes(const PlaceProbes &probes);
std::optional<PlaceProbes> DecodeProbes(std::span<const uint8_t> bytes, std::string &error);

} // namespace storm::fx
