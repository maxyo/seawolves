// The game's environment data (scene_environment's README): the time of day, the atmosphere and the lighting and rain
// presets, read once per process.
#pragma once

#include "storm/environment/point_lights.hpp"
#include "storm/environment/evaluate.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace storm::weather
{

struct PlacePreset
{
    std::string name; // presetName
    std::vector<std::string> locations;
    environment::Hours hours;
    float noon_elevation_deg = 90.0f, azimuth_deg = 0.0f; // the sun's arc
    environment::LitHoursByGroup point_light_hours;
};

struct EnvironmentData
{
    scene_data::TimeOfDay time_of_day;
    std::optional<scene_data::Atmosphere> atmosphere;
    std::optional<scene_data::ToneMapping> camera; // aces.json: the camera where the time of day does not grade
    std::optional<scene_data::CloudPreset> clouds; // texture_library.json's default cloud preset
    std::vector<PlacePreset> places;
    std::map<std::string, environment::Hours, std::less<>> rain; // by file name without .json, lower case

    // The place preset that lists `location` (any case); nullptr: none.
    const PlacePreset *ForLocation(std::string_view location) const;
    // The place preset of this presetName; nullptr: none.
    const PlacePreset *Named(std::string_view name) const;
    const environment::Hours *Rain(std::string_view name) const;
};

// The game's data; nullptr when it has no resource\ini\time_of_day.json: then its scripts set the weather through
// the WEATHER entity's attributes.
const EnvironmentData *GameEnvironment();

} // namespace storm::weather
