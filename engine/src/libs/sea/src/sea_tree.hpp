// SEA's block tree: the square blocks of the sea's grid around the camera with their level of detail (PlaceTree,
// BuildTree), the farthest dropped when they do not fit the vertex buffer (FitBlocksToBuffer), and their grid points
// and triangles (PrepareIndicesForBlock). SEA sets the members and draws the blocks.
#pragma once

#include "plane.h"
#include "storm_glm.h"

#include <cstdint>
#include <vector>

#define NUM_VERTEXS 65500
#define NUM_INDICES 165000
// The sea is drawn with 16-bit indices. FitBlocksToBuffer keeps every block's vertex grid ((size0 + 1)^2 points, the
// pIndices entries) within NUM_VERTEXS, hence its 2 * size0^2 triangles within the index buffer.
static_assert(NUM_VERTEXS <= 65536, "sea vertices are indexed with uint16_t");
static_assert(2 * NUM_VERTEXS <= NUM_INDICES, "the triangles of NUM_VERTEXS grid points must fit the index buffer");

struct SeaBlockTree
{
    struct SeaBlock
    {
        int32_t iX1, iX2, iY1, iY2; // result rectangle(in units)
        int32_t iSize0;             //

        int32_t iTX, iTY;
        int32_t iSize;
        int32_t iLOD;
        int32_t iIStart;
        int32_t iIFirst, iILast;
    };

    uint32_t dwMaxDim, dwMinDim;
    float fMaxSeaHeight; // the blocks' boxes span y -fMaxSeaHeight / 2 .. fMaxSeaHeight / 2 (the sea's heights)
    float fGridStep;
    float fLodScale;

    glm::vec3 vCamPos, vSeaCenterPos;
    PLANE *pFrustumPlanes;
    std::vector<SeaBlock> aBlocks;

    // The grid points of the blocks (NUM_VERTEXS * 3 entries: the vertex of each, -1 for none yet) and the triangles
    // (NUM_INDICES * 3), written by PrepareIndicesForBlock from iIStart, iTStart; iVStart vertices so far.
    uint16_t *pTriangles;
    uint32_t *pIndices;
    int32_t iVStart, iTStart, iIStart;
    // frames whose farthest blocks did not fit the vertex buffer (FitBlocksToBuffer), reported at the end
    uint32_t dwFramesCut = 0, dwMostBlocksCut = 0;

    uint32_t TreeDim() const;
    void PlaceTree();
    void BuildTree(int32_t iTX, int32_t iTY, int32_t iLev);
    void FitBlocksToBuffer();
    void PrepareIndicesForBlock(uint32_t dwBlockIndex);

  private:
    void AddBlock(int32_t iTX, int32_t iTY, int32_t iSize, int32_t iLOD);
    bool isVisibleBBox(const glm::vec3 &vCenter, const glm::vec3 &v1, const glm::vec3 &v2);
    void CalculateLOD(const glm::vec3 &v1, const glm::vec3 &v2, int32_t &iMaxLOD, int32_t &iMinLOD);
    float CalcLod(const float &x, const float &y, const float &z);
    int32_t VisCode(const glm::vec3 &vP);
};
