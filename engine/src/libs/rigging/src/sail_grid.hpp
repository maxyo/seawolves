// The vertex grid of a sail: its types and the grid built from its anchor points and shape (SAILONE::FillVertex).
#pragma once

#include "storm_glm.h"

#include <cstdint>

#define SAIL_COL_MAX 13
#define SAIL_ROW_MAX 17

struct SAILVERTEX
{
    glm::vec3 pos, norm;
    float tu1, tv1;
    float tu2, tv2;
    float tu3, tv3;
};

struct SPHERE
{
    glm::vec3 c;
    glm::vec3 rc; // center in world coordinates
    float r;
};

struct SAILGEOMETRY
{
    union {
        struct
        {
            float x, y, z;
        } v[9];

        struct
        {
            glm::vec3 dVv, ddVv, dddVv; // Vectors for vertical flex of sail
            glm::vec3 dVh, ddVh;        // Vectors for horizontal flaex of sail
            glm::vec3 normL, dnormL;    // Vectors for leftside sail normals
            glm::vec3 normR, dnormR;    // Vectors for rightside sail normals
            glm::vec3 dopV;             // Vector for rope tie;
        } cv;
    };

    SAILGEOMETRY() : cv{}
    {
    }
};

enum SAIL_TYPE
{
    SAIL_TREANGLE,
    SAIL_SQUARE,
    SAIL_TRAPECIDAL,
    SAIL_GERBED,
    SAIL_ROLLING,
    SAIL_SPECIAL
};

// The positions and the smooth normals (diverging from a point behind the sail) of a sail's grid.
void FillSailVertices(SAIL_TYPE type, const glm::vec3 (&hardPoints)[4], const SPHERE &boundSphere,
                      const SAILGEOMETRY &sgeo, SAILVERTEX *pv);
