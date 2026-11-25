// SEA's vertex budget (src/libs/sea/src/sea_tree.cpp: the block tree, the frustum test, FitBlocksToBuffer,
// PrepareIndicesForBlock). For the game's sea set-ups and cameras around the compass every frame writes at most
// NUM_VERTEXS vertices (16-bit indices), indexes only vertices it wrote and keeps the block under the camera; the
// blocks that do not fit are the farthest, and the frames that drop some are counted.
#include "sea_tree.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct Setup
{
    const char *name;
    uint32_t maxDim, minDim;
    float gridStep, lodScale, maxSeaHeight;
};

// WhrSea.c of TEHO and of another game on this engine with a denser sea: open sea and the sea of a location, at
// SeaDetails 1.0.
constexpr Setup kSetups[] = {
    {"TEHO open sea", 65536, 128, 0.07f, 0.5f, 200.0f},
    {"TEHO location", 65536, 64, 0.0375f, 1.0f, 1.01f},
    {"Dense-sea location", 65536 * 2, 128, 0.051f, 0.5f, 2.0f},
    {"Dense-sea open sea", 65536 * 14, 128, 0.051f, 0.5f, 20.0f},
    {"Dense-sea open sea, storm", 65536 * 14, 128, 0.051f, 0.5f, 200.0f},
};

struct Camera
{
    const char *name;
    float horizontalFov, aspect, height, pitch;
};

constexpr Camera kCameras[] = {
    {"ship camera", 1.285f, 9.0f / 20.0f, 12.0f, 0.15f},
    {"ship camera high", 1.285f, 9.0f / 20.0f, 40.0f, 0.35f},
    {"sea field of view at its maximum", 2.085f, 9.0f / 20.0f, 12.0f, 0.15f},
    {"free camera", 1.285f, 9.0f / 20.0f, 300.0f, 0.6f},
};

struct Sea : SeaBlockTree
{
    PLANE planes[4]{};
    std::vector<uint32_t> indices = std::vector<uint32_t>(NUM_VERTEXS * 3, 0xFFFFFFFFu);
    std::vector<uint16_t> triangles = std::vector<uint16_t>(NUM_INDICES * 3);

    // The four side planes of a perspective camera, inward normals, as Renderer::GetPlanes gives them to the sea.
    Sea(const Setup &setup, glm::vec3 position, float yaw, const Camera &camera) : SeaBlockTree{}
    {
        dwMaxDim = setup.maxDim;
        dwMinDim = setup.minDim;
        fGridStep = setup.gridStep;
        fLodScale = setup.lodScale;
        fMaxSeaHeight = setup.maxSeaHeight;
        vCamPos = position;
        pFrustumPlanes = planes;
        pIndices = indices.data();
        pTriangles = triangles.data();
        const float th = std::tan(camera.horizontalFov / 2.0f), tv = th * camera.aspect;
        const glm::vec3 forward{std::sin(yaw) * std::cos(camera.pitch), -std::sin(camera.pitch),
                                std::cos(yaw) * std::cos(camera.pitch)};
        const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
        glm::vec3 up = glm::cross(forward, right);
        if (up.y < 0.0f)
            up = -up;
        const auto plane = [&](float tangent, glm::vec3 side, int index) {
            const glm::vec3 n = forward * tangent + side;
            planes[index] = PLANE{n.x, n.y, n.z, glm::dot(n, position)};
        };
        plane(th, right, 0);
        plane(th, -right, 1);
        plane(tv, up, 2);
        plane(tv, -up, 3);
    }

    bool UnderCamera(const SeaBlock &b) const
    {
        const float size = fGridStep * static_cast<float>(b.iSize);
        const float x1 = vSeaCenterPos.x + static_cast<float>(b.iTX) * size;
        const float z1 = vSeaCenterPos.z + static_cast<float>(b.iTY) * size;
        return vCamPos.x >= x1 && vCamPos.x < x1 + size && vCamPos.z >= z1 && vCamPos.z < z1 + size;
    }

    // From the camera to the nearest point of the block, across the water.
    double Distance(const SeaBlock &b) const
    {
        const double size = static_cast<double>(fGridStep) * b.iSize;
        const double x1 = vSeaCenterPos.x + b.iTX * size, z1 = vSeaCenterPos.z + b.iTY * size;
        const double dx = std::max({x1 - vCamPos.x, 0.0, vCamPos.x - (x1 + size)});
        const double dz = std::max({z1 - vCamPos.z, 0.0, vCamPos.z - (z1 + size)});
        return std::hypot(dx, dz);
    }
};

int32_t GridPoints(const std::vector<SeaBlockTree::SeaBlock> &blocks)
{
    int32_t points = 0;
    for (const auto &b : blocks)
        points += (b.iSize0 + 1) * (b.iSize0 + 1);
    return points;
}

bool Same(const SeaBlockTree::SeaBlock &a, const SeaBlockTree::SeaBlock &b)
{
    return a.iTX == b.iTX && a.iTY == b.iTY && a.iSize == b.iSize;
}
} // namespace

TEST_CASE("every frame fits the sea's buffers and keeps the nearest blocks")
{
    int cut = 0;
    for (const auto &setup : kSetups)
        for (const auto &camera : kCameras)
            for (int a = 0; a < 36; a++)
                for (int p = 0; p < 4; p++)
                {
                    const glm::vec3 position{1234.5f + p * 37.7f, camera.height, -777.3f + p * 91.1f};
                    Sea sea(setup, position, a * (6.2831853f / 36.0f), camera);
                    sea.PlaceTree();
                    sea.BuildTree(0, 0, 0);
                    const auto all = sea.aBlocks;
                    sea.FitBlocksToBuffer();
                    for (uint32_t i = 0; i < sea.aBlocks.size(); i++)
                        sea.PrepareIndicesForBlock(i);

                    INFO(setup.name << ", " << camera.name << ", heading " << a * 10 << ", position " << p);
                    REQUIRE(sea.iVStart <= NUM_VERTEXS);
                    REQUIRE(sea.iTStart <= NUM_INDICES);
                    REQUIRE(std::all_of(sea.triangles.begin(), sea.triangles.begin() + sea.iTStart * 3,
                                        [&](uint16_t index) { return index < sea.iVStart; }));
                    if (std::any_of(all.begin(), all.end(), [&](const auto &b) { return sea.UnderCamera(b); }))
                        REQUIRE(std::any_of(sea.aBlocks.begin(), sea.aBlocks.end(),
                                            [&](const auto &b) { return sea.UnderCamera(b); }));
                    if (sea.aBlocks.size() == all.size())
                    {
                        REQUIRE(sea.dwFramesCut == 0);
                        continue;
                    }
                    ++cut;
                    REQUIRE(sea.dwFramesCut == 1);
                    REQUIRE(sea.dwMostBlocksCut == all.size() - sea.aBlocks.size());
                    REQUIRE(GridPoints(sea.aBlocks) <= NUM_VERTEXS);
                    // The kept blocks are the nearest: no dropped block is finer than a kept one, nor nearer than a
                    // kept one of its LOD.
                    int droppedFinest = 1000, keptCoarsest = 0;
                    for (const auto &b : sea.aBlocks)
                        keptCoarsest = std::max(keptCoarsest, b.iLOD);
                    for (const auto &d : all)
                    {
                        if (std::any_of(sea.aBlocks.begin(), sea.aBlocks.end(),
                                        [&](const auto &k) { return Same(k, d); }))
                            continue;
                        droppedFinest = std::min(droppedFinest, d.iLOD);
                        for (const auto &k : sea.aBlocks)
                            if (k.iLOD == d.iLOD)
                                REQUIRE(sea.Distance(d) >= sea.Distance(k) - 1e-3);
                    }
                    REQUIRE(droppedFinest >= keptCoarsest);
                }
    // Some set-up exceeds the buffer, or the budget is not tested.
    CHECK(cut > 0);
}

TEST_CASE("a block that does not fit the buffers is refused")
{
    Sea sea(kSetups[0], {0, 12, 0}, 0, kCameras[0]);
    sea.aBlocks = {SeaBlockTree::SeaBlock{0, 0, 0, 0, 256, 0, 0, 256, 0, 0, 0, 0}}; // 257^2 grid points
    CHECK_THROWS_AS(sea.PrepareIndicesForBlock(0), std::runtime_error);
}
