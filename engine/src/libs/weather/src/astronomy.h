#pragma once

#include "entity.h"

// The scripts of «Каждому своё» create an Astronomy entity and give it a star catalogue and planets; the sky draws
// its own stars, one sky for every game. The entity only lets their CreateEntity succeed.
class Astronomy : public Entity
{
  public:
    bool Init() override
    {
        return true;
    }
    void ProcessStage(Stage, uint32_t) override
    {
    }
};
