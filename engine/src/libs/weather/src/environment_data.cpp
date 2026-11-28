#include "environment_data.hpp"

#include "core.h"
#include "string_compare.hpp"
#include "v_file_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <memory>

namespace storm::weather
{
namespace
{
std::optional<std::string> Read(const std::string &file)
{
    char *data = nullptr;
    uint32_t size = 0;
    if (!fio->LoadFile(file.c_str(), &data, &size))
        return std::nullopt;
    const std::unique_ptr<char[]> owned(data);
    return std::string(data, size);
}

// Logs what a file's parse reported; false when it had errors (the file is not used).
bool Reported(const std::string &file, const scene_data::Report &report)
{
    for (const std::string &error : report.errors)
        spdlog::error("environment: {}: {}", file, error);
    if (!report.unknown_keys.empty())
        spdlog::info("environment: {}: {} keys not used, the first {}", file, report.unknown_keys.size(),
                     report.unknown_keys.front());
    return report.ok();
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

void LoadPresets(EnvironmentData &data, const char *folder)
{
    for (const std::string &name : fio->_GetPathsOrFilenamesByMask(folder, "*.json", false))
    {
        const std::string file = std::string(folder) + "\\" + name;
        const std::optional<std::string> text = Read(file);
        if (!text)
        {
            spdlog::error("environment: {} cannot be read", file);
            continue;
        }
        scene_data::Report report;
        const scene_data::LightingPreset preset = scene_data::ParseLightingPreset(*text, report);
        if (!Reported(file, report))
            continue;
        std::optional<environment::Hours> hours = environment::CompleteHours(preset);
        if (!hours)
        {
            spdlog::error("environment: {} has no hours", file);
            continue;
        }
        if (preset.weather_type == "rain")
        {
            data.rain.emplace(Lower(name.substr(0, name.size() - 5)), std::move(*hours));
            continue;
        }
        PlacePreset &place = data.places.emplace_back();
        place.name = preset.preset_name;
        place.locations = preset.location_ids;
        place.hours = std::move(*hours);
        place.point_light_hours = preset.point_light_hours;
        if (preset.sun_height && preset.sun_azimuth)
        {
            place.noon_elevation_deg = *preset.sun_height;
            place.azimuth_deg = *preset.sun_azimuth;
        }
        else
            spdlog::warn("environment: {} gives no sun_height and sun_azimuth: the sun passes the zenith", file);
    }
}

std::unique_ptr<EnvironmentData> Load()
{
    constexpr const char *kTimeOfDay = "resource\\ini\\time_of_day.json";
    constexpr const char *kAtmosphere = "resource\\ini\\atmosphere.json";
    constexpr const char *kCamera = "resource\\ini\\aces.json";
    constexpr const char *kTextureLibrary = "resource\\texture_library.json";
    if (!fio->_FileOrDirectoryExists(kTimeOfDay))
        return nullptr;
    const auto started = std::chrono::steady_clock::now();
    auto data = std::make_unique<EnvironmentData>();
    const std::optional<std::string> tod = Read(kTimeOfDay);
    scene_data::Report report;
    data->time_of_day = scene_data::ParseTimeOfDay(tod.value_or(""), report);
    if (!tod || !Reported(kTimeOfDay, report))
    {
        // Without a valid time of day there is no sun, moon or sky to evaluate: the weather stays the scripts'.
        spdlog::error("environment: {} is not usable: the scene environment is not evaluated from data", kTimeOfDay);
        return nullptr;
    }
    if (const std::optional<std::string> text = Read(kAtmosphere))
    {
        scene_data::Report atmosphere_report;
        scene_data::Atmosphere atmosphere = scene_data::ParseAtmosphere(*text, atmosphere_report);
        if (Reported(kAtmosphere, atmosphere_report))
            data->atmosphere = std::move(atmosphere);
    }
    else
        spdlog::error("environment: {} cannot be read: no horizon haze, height fog or cloud look", kAtmosphere);
    if (const std::optional<std::string> text = Read(kCamera))
    {
        scene_data::Report camera_report;
        scene_data::ToneMapping camera = scene_data::ParseToneMapping(*text, camera_report);
        if (Reported(kCamera, camera_report))
            data->camera = camera;
    }
    else
        spdlog::error("environment: {} cannot be read: no camera grade where the time of day does not grade", kCamera);
    // The variant its engine draws by default (another is chosen only in its editor).
    if (const std::optional<std::string> text = Read(kTextureLibrary))
    {
        scene_data::Report library_report;
        scene_data::TextureLibrary library = scene_data::ParseTextureLibrary(*text, library_report);
        if (Reported(kTextureLibrary, library_report))
        {
            if (const auto found = library.clouds.find("default"); found != library.clouds.end())
                data->clouds = found->second;
            else
                spdlog::error("environment: {} has no default cloud preset: the clouds come from noise",
                              kTextureLibrary);
        }
    }
    else
        spdlog::info("environment: no {}: the clouds come from noise", kTextureLibrary);
    LoadPresets(*data, "resource\\lighting");
    LoadPresets(*data, "resource\\weather");
    spdlog::info(
        "environment: time of day, {} place presets, {} rain presets in {} ms", data->places.size(), data->rain.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    return data;
}
} // namespace

const PlacePreset *EnvironmentData::ForLocation(std::string_view location) const
{
    for (const PlacePreset &place : places)
        if (std::ranges::any_of(place.locations, [&](const std::string &id) { return iEquals(id, location); }))
            return &place;
    return nullptr;
}

const PlacePreset *EnvironmentData::Named(std::string_view name) const
{
    const auto found = std::ranges::find_if(places, [&](const PlacePreset &place) { return place.name == name; });
    return found != places.end() ? &*found : nullptr;
}

const environment::Hours *EnvironmentData::Rain(std::string_view name) const
{
    const auto found = rain.find(Lower(name));
    return found != rain.end() ? &found->second : nullptr;
}

const EnvironmentData *GameEnvironment()
{
    // The game's files do not change while it runs.
    static const std::unique_ptr<EnvironmentData> data = Load();
    return data.get();
}

} // namespace storm::weather
