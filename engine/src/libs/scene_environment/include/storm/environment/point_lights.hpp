// The point light groups of the place being played: which are lit at its hour (scene_data::PointLightGroupLit).
#pragma once

#include "storm/scene_data/scene_data.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace storm::environment
{
using LitHoursByGroup = std::map<std::string, std::vector<scene_data::LitHours>, std::less<>>;

// Set by the weather every frame from the place's lighting preset (nullptr: no schedule, every group lit); the
// schedule outlives the call (the game's environment data).
void SetPointLightHours(const LitHoursByGroup *hours, float hour);
// Whether the group (a light type: "torchlight") is lit now, for what follows a group (an effect tied to it).
bool PointLightGroupLit(std::string_view group);
} // namespace storm::environment
