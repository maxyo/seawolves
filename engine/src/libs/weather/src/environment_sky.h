#pragma once

#include "entity.h"
#include "script_libriary.h"

// The sky of the scene environment, drawn by the renderer (storm/render/effects.hpp DrawSky). WEATHER creates it and
// puts it into its layers when the environment comes from the game's data.
class EnvironmentSky : public Entity
{
  public:
    bool Init() override
    {
        return true;
    }
    uint64_t ProcessMessage(MESSAGE &message) override;
    void ProcessStage(Stage stage, uint32_t delta) override;
};

// GetFogDensity() and GetFogColor(): the fog of the current hour of the place's lighting presets.
class WEATHER_LIBRARY : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
