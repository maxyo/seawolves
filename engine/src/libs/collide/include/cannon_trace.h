#pragma once

#include "object.h"

class CANNON_TRACE_BASE : public COLLISION_OBJECT
{
  public:
    ~CANNON_TRACE_BASE() override = default;

    // A ball of character `iBallOwner` fired from a cannon of type `iCannonType` (-1: no ball, no cannon) flies from
    // src to dst; the fraction of the way to the hit, > 1 for none.
    virtual float Cannon_Trace(int32_t iBallOwner, int32_t iCannonType, const glm::vec3 &src, const glm::vec3 &dst) = 0;
};
