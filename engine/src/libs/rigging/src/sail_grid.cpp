#include "sail_grid.hpp"

void FillSailVertices(SAIL_TYPE type, const glm::vec3 (&hardPoints)[4], const SPHERE &boundSphere,
                      const SAILGEOMETRY &sgeo, SAILVERTEX *pv)
{
    uint16_t ix, iy, idx;
    glm::vec3 pcur, dV, ddV, dddV;
    glm::vec3 dnorm;
    glm::vec3 dStart;

    auto tmpCol = 1.f / static_cast<float>((SAIL_COL_MAX - 1));
    auto tmpRow = 1.f / static_cast<float>((SAIL_ROW_MAX - 1));

    auto pStart = hardPoints[0];
    auto dVStart = sgeo.cv.dVv;
    auto dVhStart = sgeo.cv.dVh;

    if (type == SAIL_TREANGLE)
    {
        dStart = (hardPoints[1] - pStart) * tmpRow;
        // define the center of divergence of normals
        dnorm = boundSphere.c - sgeo.cv.normL * boundSphere.r;
        // enumeration of sections along the sail and looping through them
        for (idx = ix = 0; ix < SAIL_ROW_MAX; ix++)
        {
            // set the coordinates of the starting point and their increments at each step
            pcur = pStart;
            dV = dVStart;

            // sail calculation along the section line
            // |||||||||||||||||||||||||||||||||||
            for (iy = 0; iy <= ix; iy++, idx++)
            {
                // Writing coordinates to the vertex buffer
                pv[idx].pos = pcur;
                pv[idx].norm = storm::math::normalizeOrZero(pcur - dnorm);
                // Calculation of the next
                pcur += dV;
            }
            pStart += dStart;
            dVhStart += sgeo.cv.ddVh;
            dVStart += dVhStart;
        }

        // set the sail attachment points
        pv[idx - SAIL_ROW_MAX].pos = hardPoints[1];
        pv[idx - 1].pos = hardPoints[2];
    }
    else
    {
        dStart = (hardPoints[1] - pStart) * tmpCol;
        // dnormL is the normal's row increment (zero for planar anchors), not a surface normal.
        // Keep the same smooth sphere-based normals as triangular and trapezoidal sails.
        dnorm = boundSphere.c - sgeo.cv.normL * boundSphere.r;
        // enumeration of sections along the sail and looping through them
        for (idx = ix = 0; ix < SAIL_COL_MAX; ix++)
        {
            // set the coordinates of the starting point and their increments at each step
            pcur = pStart;
            dV = dVStart;
            ddV = sgeo.cv.ddVv;
            dddV = sgeo.cv.dddVv;
            // sail calculation along the section line
            // |||||||||||||||||||||||||||||||||||
            for (iy = 0; iy < SAIL_ROW_MAX; iy++, idx++)
            {
                // Writing coordinates to the vertex buffer
                pv[idx].pos = pcur;
                pv[idx].norm = storm::math::normalizeOrZero(pcur - dnorm);
                // Calculation of the next
                ddV += dddV;
                dV += ddV;
                pcur += dV;
            }
            pStart += dStart;
            dVhStart += sgeo.cv.ddVh;
            dVStart += dVhStart;
        }

        // set the sail attachment points
        pv[idx - SAIL_ROW_MAX].pos = hardPoints[1];
        pv[SAIL_ROW_MAX - 1].pos = hardPoints[2];
        pv[idx - 1].pos = hardPoints[3];
    }
}
