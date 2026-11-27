#pragma once

#include "entity.h"
#include "script_libriary.h"

// GetFogDensity() and GetFogColor(): the fog of the current hour of the place's lighting presets.
class WEATHER_LIBRARY : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
