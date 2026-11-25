// The grid a sail is built with (src/libs/rigging/src/sail_grid.cpp FillSailVertices): on a planar sail of every
// kind, every vertex has a finite unit normal on the side the sail faces (its plane normal), and the corners are
// its anchor points.
#include "sail_grid.hpp"

#include <catch2/catch.hpp>

#include <array>
#include <cmath>

TEST_CASE("planar sails are lit from the side they face")
{
    for (const SAIL_TYPE kind : {SAIL_TREANGLE, SAIL_SQUARE, SAIL_TRAPECIDAL})
    {
        INFO("sail kind " << kind);
        const glm::vec3 anchors[4] = {{-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, 1, 0}};
        SPHERE bound{};
        bound.r = std::sqrt(2.0f);
        SAILGEOMETRY geometry;
        geometry.cv.normL = {0, 0, 1};
        geometry.cv.dVv = kind == SAIL_TREANGLE ? glm::vec3{-2.0f / 16, 2.0f / 16, 0} : glm::vec3{0, 2.0f / 16, 0};
        geometry.cv.ddVv = {0, 0, 0.001f}; // a bulge: the grid does not end at the anchors by itself
        std::array<SAILVERTEX, SAIL_COL_MAX * SAIL_ROW_MAX> grid{};
        FillSailVertices(kind, anchors, bound, geometry, grid.data());
        const int count = kind == SAIL_TREANGLE ? SAIL_ROW_MAX * (SAIL_ROW_MAX + 1) / 2 : SAIL_COL_MAX * SAIL_ROW_MAX;
        int unlit = 0, unnormal = 0;
        for (int i = 0; i < count; ++i)
        {
            const glm::vec3 n = grid[i].norm;
            unlit += !(n.z > 0.0f);
            unnormal += !(std::abs(glm::length(n) - 1.0f) < 1e-5f);
        }
        CHECK(unlit == 0);
        CHECK(unnormal == 0);
        CHECK(grid[0].pos == anchors[0]);
        if (kind == SAIL_TREANGLE)
        {
            CHECK(grid[count - SAIL_ROW_MAX].pos == anchors[1]);
            CHECK(grid[count - 1].pos == anchors[2]);
        }
        else
        {
            CHECK(grid[count - SAIL_ROW_MAX].pos == anchors[1]);
            CHECK(grid[SAIL_ROW_MAX - 1].pos == anchors[2]);
            CHECK(grid[count - 1].pos == anchors[3]);
        }
    }
}
