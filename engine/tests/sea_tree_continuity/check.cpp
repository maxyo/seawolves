// SEA's block tree across its camera steps (src/libs/sea/src/sea_tree.cpp PlaceTree, BuildTree): for world points
// within the sea's relief and a camera path across a step of the tree's origin, with the game's sea set-ups, the
// lattice cell around each point (its vertex positions), its LOD and its height interpolated from a height function
// of the world position do not jump between two frames that move the camera by 3 cm.
#include "sea_tree.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <vector>

namespace
{
// A stand-in for the sea's heights: a function of the world position only, like SSE_WaveXZ.
double Height(double x, double z)
{
    return 1.5 * std::sin(0.031 * x + 0.7) * std::cos(0.023 * z) + 0.6 * std::sin(0.11 * x - 0.07 * z) +
           0.3 * std::sin(0.37 * z + 0.19 * x);
}

struct Sample
{
    bool found = false;
    int lod = 0;
    double x0 = 0.0, z0 = 0.0; // the lattice cell's corner (world)
    double height = 0.0;       // bilinear over the cell's four vertices, as the GPU interpolates the mesh
};

Sample Find(const SeaBlockTree &sea, double px, double pz)
{
    Sample sample;
    for (const SeaBlockTree::SeaBlock &b : sea.aBlocks)
    {
        // The block's extent and vertices as SSE_WaveXZBlock places them.
        const double step = static_cast<double>(sea.fGridStep) * (1 << b.iLOD);
        const double x1 = static_cast<double>(b.iTX) * b.iSize * sea.fGridStep + sea.vSeaCenterPos.x;
        const double z1 = static_cast<double>(b.iTY) * b.iSize * sea.fGridStep + sea.vSeaCenterPos.z;
        const double size = static_cast<double>(b.iSize >> b.iLOD) * step;
        if (px < x1 || px >= x1 + size || pz < z1 || pz >= z1 + size)
            continue;
        sample.found = true;
        sample.lod = b.iLOD;
        sample.x0 = x1 + std::floor((px - x1) / step) * step;
        sample.z0 = z1 + std::floor((pz - z1) / step) * step;
        const double fx = (px - sample.x0) / step, fz = (pz - sample.z0) / step;
        sample.height =
            (1 - fz) * ((1 - fx) * Height(sample.x0, sample.z0) + fx * Height(sample.x0 + step, sample.z0)) +
            fz * ((1 - fx) * Height(sample.x0, sample.z0 + step) + fx * Height(sample.x0 + step, sample.z0 + step));
        return sample;
    }
    return sample;
}

struct Setup
{
    const char *name;
    uint32_t max_dim;
    float grid_step, lod_scale, reach;
};

struct Result
{
    int steps = 0;         // frames whose tree origin moved
    int lattice_jumps = 0; // ... at which a point's cell moved to other world positions
    int lod_jumps = 0;     // ... or changed its LOD
    double max_height_jump = 0.0;
    bool covered = true;
};

Result Run(const Setup &setup, glm::dvec2 start)
{
    Result result;
    SeaBlockTree sea{};
    sea.dwMaxDim = setup.max_dim;
    sea.dwMinDim = 128;
    sea.fMaxSeaHeight = 20.0f;
    sea.fGridStep = setup.grid_step;
    sea.fLodScale = setup.lod_scale;
    PLANE everything[4] = {{0, 0, 0, -1}, {0, 0, 0, -1}, {0, 0, 0, -1}, {0, 0, 0, -1}}; // every block is in view
    sea.pFrustumPlanes = everything;
    // World points around the path, up to the relief's reach.
    std::vector<glm::dvec2> points;
    for (int i = 0; i < 400; ++i)
    {
        const double angle = 2.399963 * i, distance = 30.0 + (setup.reach - 50.0) * std::sqrt((i + 0.5) / 400.0);
        points.push_back({start.x + distance * std::cos(angle), start.y + distance * std::sin(angle)});
    }
    std::vector<Sample> previous;
    glm::vec3 previous_origin{0.0f};
    // A ship's camera: 3 cm per frame (about 2 knots at 30 fps) diagonally, 36 m in 1200 frames.
    for (int frame = 0; frame < 1200; ++frame)
    {
        const double t = frame * 0.03;
        sea.vCamPos =
            glm::vec3(static_cast<float>(start.x + t * 0.7071), 12.0f, static_cast<float>(start.y + t * 0.7071));
        sea.PlaceTree();
        sea.aBlocks.clear();
        sea.BuildTree(0, 0, 0);
        std::vector<Sample> samples;
        for (const glm::dvec2 &p : points)
        {
            samples.push_back(Find(sea, p.x, p.y));
            result.covered = result.covered && samples.back().found;
        }
        if (frame > 0 && sea.vSeaCenterPos != previous_origin)
        {
            ++result.steps;
            for (size_t i = 0; i < points.size(); ++i)
            {
                const Sample &a = previous[i], &b = samples[i];
                if (!a.found || !b.found)
                    continue;
                result.lod_jumps += a.lod != b.lod;
                // Float positions tens of kilometres from the origin round to millimetres: below 1 cm is the same
                // vertex.
                result.lattice_jumps += std::fabs(a.x0 - b.x0) > 0.01 || std::fabs(a.z0 - b.z0) > 0.01;
                result.max_height_jump = std::max(result.max_height_jump, std::fabs(a.height - b.height));
            }
        }
        previous = samples;
        previous_origin = sea.vSeaCenterPos;
    }
    return result;
}
} // namespace

TEST_CASE("a step of the tree's origin moves no vertex, LOD or height within the relief")
{
    // The engine's defaults (main menu), a location (WhrSea.c: MaxDim 65536*2, 3000 m) and the open sea
    // (MaxDim 65536*14, 20 km); grid steps of the sea detail settings.
    const Setup setups[] = {
        {"default", 65536 * 2, 0.07f, 0.5f, 1600.0f},
        {"location", 65536 * 2, 0.051f, 0.5f, 3000.0f},
        {"open sea", 65536 * 14, 0.09f, 0.5f, 20000.0f},
    };
    for (const Setup &setup : setups)
    {
        INFO(setup.name);
        // The path starts 10.6 m (diagonally 15 m) before a step of the origin on both axes.
        const double step = static_cast<double>(setup.grid_step) * (setup.max_dim * 2 / 4);
        const Result result = Run(setup, {3 * step - 10.6, -2 * step - 10.6});
        CHECK(result.covered);
        CHECK(result.steps >= 1);
        CHECK(result.lattice_jumps == 0);
        CHECK(result.lod_jumps == 0);
        CHECK(result.max_height_jump < 0.005);
    }
}
