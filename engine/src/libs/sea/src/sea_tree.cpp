#include "sea_tree.hpp"

#include "math_inlines.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

int32_t SeaBlockTree::VisCode(const glm::vec3 &vP)
{
    int32_t vc = 0;

    // Max - so the accuracy is much higher, blocks should not disappear sometimes in the distance
    PLANE *p = &pFrustumPlanes[0];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x01;
    p = &pFrustumPlanes[1];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x02;
    p = &pFrustumPlanes[2];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x04;
    p = &pFrustumPlanes[3];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x08;

    return vc;
}

bool SeaBlockTree::isVisibleBBox(const glm::vec3 &vCenter, const glm::vec3 &v1, const glm::vec3 &v2)
{

    // if sphere not visible - return

    // check box visible
    int32_t vc = 0xFF;
    vc &= VisCode(glm::vec3(v1.x, v1.y, v1.z));
    if (vc == 0)
        return true;
    vc &= VisCode(glm::vec3(v1.x, v2.y, v1.z));
    vc &= VisCode(glm::vec3(v2.x, v1.y, v1.z));
    vc &= VisCode(glm::vec3(v2.x, v2.y, v1.z));
    if (vc == 0)
        return true;
    vc &= VisCode(glm::vec3(v1.x, v1.y, v2.z));
    vc &= VisCode(glm::vec3(v1.x, v2.y, v2.z));
    vc &= VisCode(glm::vec3(v2.x, v1.y, v2.z));
    vc &= VisCode(glm::vec3(v2.x, v2.y, v2.z));

    return vc == 0;
}

float SeaBlockTree::CalcLod(const float &x, const float &y, const float &z)
{
    return Sqr(x - vCamPos.x) + /*Sqr((y - vCamPos.y)) + */ Sqr(z - vCamPos.z);
}

void SeaBlockTree::CalculateLOD(const glm::vec3 &v1, const glm::vec3 &v2, int32_t &iMaxLOD, int32_t &iMinLOD)
{
    float fCur, fMax, fMin;

    fCur = CalcLod(v1.x, vSeaCenterPos.y, v1.z);
    fMax = fCur;
    fCur = CalcLod(v2.x, vSeaCenterPos.y, v1.z);
    if (fCur > fMax)
        fMax = fCur;
    fCur = CalcLod(v1.x, vSeaCenterPos.y, v2.z);
    if (fCur > fMax)
        fMax = fCur;
    fCur = CalcLod(v2.x, vSeaCenterPos.y, v2.z);
    if (fCur > fMax)
        fMax = fCur;

    if (vCamPos.x < v1.x)
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(v1.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(v1.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(v1.x, vSeaCenterPos.y, vCamPos.z);
    }
    else if (vCamPos.x > v2.x)
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(v2.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(v2.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(v2.x, vSeaCenterPos.y, vCamPos.z);
    }
    else
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, vCamPos.z);
    }

    // At least 1 under the logarithm: a block around the camera has fMin = 0, and log(0) = -inf has no int (undefined
    // behaviour; arm64 and x86 convert it differently). Below LOD 4 the result is clamped anyway.
    iMaxLOD = static_cast<int>(0.5f * logf(std::max(fLodScale * fMax, 1.0f)) / logf(2));
    if (iMaxLOD < 4)
        iMaxLOD = 4;
    iMinLOD = static_cast<int>(0.5f * logf(std::max(fLodScale * fMin, 1.0f)) / logf(2));
    if (iMinLOD < 4)
        iMinLOD = 4;
}

void SeaBlockTree::AddBlock(int32_t iTX, int32_t iTY, int32_t iSize, int32_t iLOD)
{
    aBlocks.emplace_back(SeaBlock{0, 0, 0, 0, iSize >> iLOD, iTX, iTY, iSize, iLOD, 0, 0, 0});
}

// The block tree is anchored to the world. It spans twice MaxDim grid cells and its origin follows the camera only
// in steps of a quarter of that, so every block of the levels below the top two sits on the same world grid before
// and after a step, with the same vertices and the same LOD (both depend on the block's world extent and the camera
// only); the top two levels are far beyond the sea's relief whenever they are leaves. The camera stays in the second
// quarter of each axis, at least MaxDim / 2 cells from the edges, as far as with the original placement. That one
// stepped by 256 grid cells: the partition of every larger block and the vertices of the coarse LODs (36 m and wider
// between vertices, beyond ~700 m) moved to other world points at each step, about every 18 m of camera travel, and
// the far band of the sea changed its shape and shading in one frame (tests/sea_tree_continuity).
uint32_t SeaBlockTree::TreeDim() const
{
    return dwMaxDim * 2;
}

void SeaBlockTree::PlaceTree()
{
    const float fStep = fGridStep * static_cast<float>(TreeDim() / 4);
    vSeaCenterPos =
        glm::vec3(fStep * (std::floor(vCamPos.x / fStep) - 2.0f), 0.0f, fStep * (std::floor(vCamPos.z / fStep) - 2.0f));
}

void SeaBlockTree::BuildTree(int32_t iTX, int32_t iTY, int32_t iLev)
{
    int32_t iMaxLOD, iMinLOD;
    const int32_t iSize = static_cast<int32_t>(TreeDim() >> iLev);
    const float fGSize = fGridStep * iSize;

    const glm::vec3 v1 = vSeaCenterPos + glm::vec3(iTX * fGSize, -fMaxSeaHeight / 2.0f, iTY * fGSize);
    const glm::vec3 v2 = v1 + glm::vec3(fGSize, fMaxSeaHeight, fGSize);

    if (!isVisibleBBox(vSeaCenterPos, v1, v2))
        return;

    CalculateLOD(v1, v2, iMaxLOD, iMinLOD);

    if (iSize <= static_cast<int32_t>(dwMinDim) || iMaxLOD - iMinLOD <= 1)
    {
        AddBlock(iTX, iTY, iSize, iMinLOD);
        return;
    }

    iTX *= 2;
    iTY *= 2;
    iLev++;

    BuildTree(iTX, iTY, iLev);
    BuildTree(iTX + 1, iTY, iLev);
    BuildTree(iTX, iTY + 1, iLev);
    BuildTree(iTX + 1, iTY + 1, iLev);
}

// Orders the blocks far to near (coarse LOD first, PrepareIndicesForBlock stitches each block to the coarser ones
// before it) and drops the farthest ones whose vertices do not fit the buffer. A block of size0 cells a side takes
// (size0 + 1)^2 grid points (pIndices) and writes at most as many vertices (fewer when it shares an edge).
void SeaBlockTree::FitBlocksToBuffer()
{
    const auto distance2 = [this](const SeaBlock &block) {
        const float size = fGridStep * static_cast<float>(block.iSize);
        const float x1 = vSeaCenterPos.x + static_cast<float>(block.iTX) * size;
        const float z1 = vSeaCenterPos.z + static_cast<float>(block.iTY) * size;
        const float dx = std::clamp(vCamPos.x, x1, x1 + size) - vCamPos.x;
        const float dz = std::clamp(vCamPos.z, z1, z1 + size) - vCamPos.z;
        return dx * dx + dz * dz;
    };
    std::sort(aBlocks.begin(), aBlocks.end(), [&distance2](const SeaBlock &a, const SeaBlock &b) {
        return a.iLOD != b.iLOD ? a.iLOD > b.iLOD : distance2(a) > distance2(b);
    });

    int32_t points = 0;
    auto first = aBlocks.size(); // the farthest block that is kept
    while (first > 0)
    {
        const int32_t side = aBlocks[first - 1].iSize0 + 1;
        if (points + side * side > NUM_VERTEXS)
            break;
        points += side * side;
        --first;
    }
    if (first == 0)
        return;

    if (dwFramesCut++ == 0)
        spdlog::warn("sea: the {} visible blocks need more than the {} vertices of the sea's buffer; the farthest {} "
                     "are not drawn (the frames are counted when the sea goes)",
                     aBlocks.size(), NUM_VERTEXS, first);
    dwMostBlocksCut = std::max(dwMostBlocksCut, static_cast<uint32_t>(first));
    aBlocks.erase(aBlocks.begin(), aBlocks.begin() + static_cast<std::ptrdiff_t>(first));
}

void SeaBlockTree::PrepareIndicesForBlock(uint32_t dwBlockIndex)
{
    SeaBlock *pB = &aBlocks[dwBlockIndex];

    const float fStep = fGridStep * static_cast<float>(1 << pB->iLOD);
    int32_t x, y, size0 = pB->iSize >> pB->iLOD;
    // FitBlocksToBuffer keeps these within the buffers; past them the 16-bit indices and the buffers would overflow.
    if (iIStart + (size0 + 1) * (size0 + 1) > NUM_VERTEXS || iTStart + 2 * size0 * size0 > NUM_INDICES)
        throw std::runtime_error(fmt::format("sea: block {} ({} cells a side) does not fit the buffers: {} of {} grid "
                                             "points, {} of {} triangles taken",
                                             dwBlockIndex, size0, iIStart, NUM_VERTEXS, iTStart, NUM_INDICES));

    pB->iIStart = iIStart;

    const float x1 = static_cast<float>(pB->iTX * pB->iSize) * fGridStep;
    const float y1 = static_cast<float>(pB->iTY * pB->iSize) * fGridStep;
    const float x2 = x1 + static_cast<float>(size0) * fStep;
    const float y2 = y1 + static_cast<float>(size0) * fStep;

    pB->iX1 = fftoi(x1 / fGridStep);
    pB->iX2 = fftoi(x2 / fGridStep);
    pB->iY1 = fftoi(y1 / fGridStep);
    pB->iY2 = fftoi(y2 / fGridStep);

    // analysis
    int32_t i, j;
    for (i = 0; i < static_cast<int32_t>(dwBlockIndex); i++) // if (i == 6)
    {
        SeaBlock *pB2 = &aBlocks[i];

        // Test Up & Down
        const bool bTestedUp = pB->iY1 == pB2->iY2;
        const bool bTestedDown = pB->iY2 == pB2->iY1;

        if (bTestedUp || bTestedDown)
        {
            const int32_t iAddSrc = pB2->iIStart + ((bTestedUp) ? (pB2->iSize0 + 1) * pB2->iSize0 : 0);
            const int32_t iAddDst = pB->iIStart + ((bTestedUp) ? 0 : (pB->iSize0 + 1) * pB->iSize0);

            if ((pB->iX1 >= pB2->iX1 && pB->iX2 <= pB2->iX2) || (pB->iX1 <= pB2->iX1 && pB->iX2 >= pB2->iX2))
            {
                const int32_t iMinX = Max(pB->iX1, pB2->iX1);
                const int32_t iMaxX = Min(pB->iX2, pB2->iX2);

                const int32_t iStartDstX = pB->iSize0 * (iMinX - pB->iX1) / (pB->iX2 - pB->iX1);
                const int32_t iStartSrcX = pB2->iSize0 * (iMinX - pB2->iX1) / (pB2->iX2 - pB2->iX1);

                const int32_t iEndDstX = pB->iSize0 * (iMaxX - pB->iX1) / (pB->iX2 - pB->iX1);
                int32_t iEndSrcX = pB2->iSize0 * (iMaxX - pB2->iX1) / (pB2->iX2 - pB2->iX1);

                if (pB->iLOD == pB2->iLOD)
                    for (j = iStartDstX; j <= iEndDstX; j++)
                        pIndices[iAddDst + j] = pIndices[iAddSrc + iStartSrcX + (j - iStartDstX)];
                else
                    for (j = iStartDstX; j <= iEndDstX; j++)
                        pIndices[iAddDst + j] = pIndices[iAddSrc + iStartSrcX + (j - iStartDstX) / 2];

                continue;
            }
        }

        // Test Left & Right
        const bool bTestedLeft = pB->iX1 == pB2->iX2;
        const bool bTestedRight = pB->iX2 == pB2->iX1;
        if (bTestedLeft || bTestedRight)
        {
            const int32_t iAddSrc = pB2->iIStart + ((bTestedLeft) ? (pB2->iSize0) : 0);
            const int32_t iAddDst = pB->iIStart + ((bTestedLeft) ? 0 : (pB->iSize0));

            if ((pB->iY1 >= pB2->iY1 && pB->iY2 <= pB2->iY2) || (pB->iY1 <= pB2->iY1 && pB->iY2 >= pB2->iY2))
            {
                const int32_t iMinY = Max(pB->iY1, pB2->iY1);
                const int32_t iMaxY = Min(pB->iY2, pB2->iY2);

                const int32_t iStartDstY = pB->iSize0 * (iMinY - pB->iY1) / (pB->iY2 - pB->iY1);
                const int32_t iStartSrcY = pB2->iSize0 * (iMinY - pB2->iY1) / (pB2->iY2 - pB2->iY1);

                const int32_t iEndDstY = pB->iSize0 * (iMaxY - pB->iY1) / (pB->iY2 - pB->iY1);
                int32_t iEndSrcY = pB2->iSize0 * (iMaxY - pB2->iY1) / (pB2->iY2 - pB2->iY1);

                if (pB->iLOD == pB2->iLOD)
                    for (j = iStartDstY; j <= iEndDstY; j++)
                        pIndices[iAddDst + j * (pB->iSize0 + 1)] =
                            pIndices[iAddSrc + (iStartSrcY + j - iStartDstY) * (pB2->iSize0 + 1)];
                else
                    for (j = iStartDstY; j <= iEndDstY; j++)
                        pIndices[iAddDst + j * (pB->iSize0 + 1)] =
                            pIndices[iAddSrc + (iStartSrcY + (j - iStartDstY) / 2) * (pB2->iSize0 + 1)];
            }
        }
    }

    int32_t iIFirst = 1000000, iILast = -1000000;

    for (y = 0; y <= size0; y++)
        for (x = 0; x <= size0; x++)
        {
            if (pIndices[iIStart] != static_cast<uint32_t>(-1))
            {
                iIStart++;
                continue;
            }

            if (iVStart < iIFirst)
                iIFirst = iVStart;
            if (iVStart > iILast)
                iILast = iVStart;

            pIndices[iIStart++] = static_cast<uint32_t>(iVStart);
            iVStart++;
        }

    pB->iIFirst = iIFirst;
    pB->iILast = iILast;

    // setup triangles
    int32_t yy, dyy = size0 + 1;
    for (y = 0, yy = 0; y < size0; y++, yy += dyy)
        for (x = 0; x < size0; x++)
        {
            // first triangle
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy + 1)]);
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + 1)]);
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy)]);
            // second triangle
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy)]);
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy + 1)]);
            *pTriangles++ = static_cast<uint16_t>(pIndices[pB->iIStart + static_cast<uint32_t>(x + yy)]);

            iTStart += 2;
        }
}
