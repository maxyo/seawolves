// Resource/INI/atmosphere.json: the haze band at the horizon, the height fog of land and sea and the textured clouds'
// look, per period.
#include "json_reader.hpp"

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

// Reads the period table `object` keyed by `names` (dawn, day, sunset, night order) into `periods`.
template <typename T, typename Read>
void ReadPeriods(Reader &reader, const Json &object, const std::string &path, const std::array<const char *, 4> &names,
                 std::array<T, kPeriods> &periods, Read read)
{
    reader.Require(object, path, {names[0], names[1], names[2], names[3]});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        for (int i = 0; i < kPeriods; ++i)
            if (key == names[size_t(i)])
                return read(value, child, periods[size_t(i)]), true;
        return false;
    });
}

void ReadHaze(Reader &reader, const Json &object, const std::string &path, HorizonHaze &haze)
{
    reader.Require(object, path,
                   {"enabled", "sky_color", "intensity", "sea_width_degrees", "sky_width_degrees", "edge_fade",
                    "sea_start", "sea_fade", "sample_elevation_degrees", "colors", "transition_hours"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        const std::pair<const char *, float *> numbers[] = {
            {"intensity", &haze.intensity},
            {"sea_width_degrees", &haze.sea_width_degrees},
            {"sky_width_degrees", &haze.sky_width_degrees},
            {"edge_fade", &haze.edge_fade},
            {"sea_start", &haze.sea_start},
            {"sea_fade", &haze.sea_fade},
            {"sample_elevation_degrees", &haze.sample_elevation_degrees}};
        for (const auto &[name, number] : numbers)
            if (key == name)
                return reader.Float(value, child, *number), true;
        if (key == "enabled" || key == "sky_color")
            return reader.Bool(value, child, key == "enabled" ? haze.enabled : haze.sky_color), true;
        if (key == "transition_hours")
            return reader.Transitions(value, child, haze.transition_hours), true;
        if (key == "colors")
        {
            ReadPeriods(reader, value, child, {"Dawn", "Day", "Sunset", "Night"}, haze.colors,
                        [&](const Json &v, const std::string &p, HazeColor &color) {
                            reader.Require(v, p, {"tint", "radiance", "brightness"});
                            reader.Object(v, p, [&](const std::string &k, const Json &x, const std::string &q) {
                                if (k == "tint")
                                    reader.Color(x, q, color.tint);
                                else if (k == "radiance")
                                    reader.Color(x, q, color.radiance);
                                else if (k == "brightness")
                                    reader.Float(x, q, color.brightness);
                                else
                                    return false;
                                return true;
                            });
                        });
            return true;
        }
        return false;
    });
    // The band's shape divides by these.
    if (!(haze.sky_width_degrees > 0.0f && haze.sea_width_degrees > 0.0f && haze.edge_fade > 0.0f))
        reader.Error(path, "sky_width_degrees, sea_width_degrees and edge_fade must be above 0");
}

void ReadFogPeriod(Reader &reader, const Json &object, const std::string &path, HeightFogPeriod &fog)
{
    reader.Require(object, path,
                   {"attenuation_distance", "base_height", "maximum_height", "start_distance", "max_distance",
                    "max_opacity", "color", "tint", "sky_color"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        const std::pair<const char *, float *> numbers[] = {{"attenuation_distance", &fog.attenuation_distance},
                                                            {"base_height", &fog.base_height},
                                                            {"maximum_height", &fog.maximum_height},
                                                            {"start_distance", &fog.start_distance},
                                                            {"max_distance", &fog.max_distance},
                                                            {"max_opacity", &fog.max_opacity},
                                                            {"mip_near", &fog.mip_near},
                                                            {"mip_far", &fog.mip_far},
                                                            {"mip_max", &fog.mip_max}};
        for (const auto &[name, number] : numbers)
            if (key == name)
                return reader.Float(value, child, *number), true;
        if (key == "color" || key == "tint")
            return reader.Color(value, child, key == "color" ? fog.color : fog.tint), true;
        if (key == "sky_color")
            return reader.Bool(value, child, fog.sky_color), true;
        return false;
    });
    // The density is the inverse of the attenuation distance; the layer's falloff the inverse of its thickness.
    if (!(fog.attenuation_distance > 0.0f && fog.maximum_height > fog.base_height))
        reader.Error(path, "attenuation_distance must be above 0 and maximum_height above base_height");
}

void ReadProfile(Reader &reader, const Json &object, const std::string &path, HeightFogProfile &profile)
{
    reader.Require(object, path, {"enabled", "periods", "transition_hours"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "enabled")
            reader.Bool(value, child, profile.enabled);
        else if (key == "transition_hours")
            reader.Transitions(value, child, profile.transition_hours);
        else if (key == "periods")
            ReadPeriods(
                reader, value, child, {"Morning", "Day", "Evening", "Night"}, profile.periods,
                [&](const Json &v, const std::string &p, HeightFogPeriod &fog) { ReadFogPeriod(reader, v, p, fog); });
        else
            return false;
        return true;
    });
}

void ReadCloudPeriod(Reader &reader, const Json &object, const std::string &path, CloudPeriod &period)
{
    const std::pair<const char *, float *> numbers[] = {
        {"intensity", &period.intensity},
        {"opacity", &period.opacity},
        {"edge_lighting_strength", &period.edge_lighting_strength},
        {"reflection_strength", &period.reflection_strength},
        {"moon_intensity", &period.moon_intensity},
        {"sun_transmission_strength", &period.sun_transmission_strength},
        {"moon_transmission_strength", &period.moon_transmission_strength},
        {"sun_transmission_radius", &period.sun_transmission_radius},
        {"moon_transmission_radius", &period.moon_transmission_radius},
        {"sun_intensity", &period.sun_intensity},
        {"ambient_intensity", &period.ambient_intensity},
        {"sky_tint", &period.sky_tint},
        {"lighting_contrast", &period.lighting_contrast},
        {"twilight_sun_multiplier", &period.twilight_sun_multiplier},
        {"twilight_base_lighting", &period.twilight_base_lighting},
        {"twilight_warmth", &period.twilight_warmth},
        {"twilight_edge_color", &period.twilight_edge_color},
        {"twilight_opposite_strength", &period.twilight_opposite_strength},
        {"twilight_transition_width", &period.twilight_transition_width},
        {"twilight_ambient_intensity", &period.twilight_ambient_intensity}};
    reader.Require(object, path,
                   {"intensity",
                    "opacity",
                    "edge_lighting_strength",
                    "reflection_strength",
                    "moon_intensity",
                    "sun_transmission_strength",
                    "moon_transmission_strength",
                    "sun_transmission_radius",
                    "moon_transmission_radius",
                    "sun_intensity",
                    "ambient_intensity",
                    "sky_tint",
                    "lighting_contrast",
                    "twilight_sun_multiplier",
                    "twilight_base_lighting",
                    "twilight_warmth",
                    "twilight_edge_color",
                    "twilight_opposite_strength",
                    "twilight_transition_width",
                    "twilight_ambient_intensity",
                    "twilight_tint"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        for (const auto &[name, number] : numbers)
            if (key == name)
                return reader.Float(value, child, *number), true;
        if (key == "twilight_tint")
            return reader.Color(value, child, period.twilight_tint), true;
        return false;
    });
}

void ReadClouds(Reader &reader, const Json &object, const std::string &path, TexturedClouds &clouds)
{
    reader.Require(object, path, {"enabled", "rotation_speeds", "periods"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        if (key == "version")
        {
            int version = 0;
            if (reader.Int(value, child, version) && version != 1)
                reader.Error(child, "only version 1 is known");
        }
        else if (key == "enabled")
            reader.Bool(value, child, clouds.enabled);
        else if (key == "rotation_speeds")
            reader.Floats(value, child, clouds.rotation_speeds);
        else if (key == "periods")
            ReadPeriods(reader, value, child, {"dawn", "day", "sunset", "night"}, clouds.periods,
                        [&](const Json &v, const std::string &p, CloudPeriod &period) {
                            ReadCloudPeriod(reader, v, p, period);
                        });
        else
            return false;
        return true;
    });
}
} // namespace

Atmosphere ParseAtmosphere(std::string_view json, Report &report)
{
    Reader reader(report);
    Atmosphere out;
    Json root;
    if (!reader.Parse(json, root))
        return out;
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        if (key == "version")
            reader.Int(value, path, out.version);
        else if (key == "horizon_haze")
            ReadHaze(reader, value, path, out.horizon_haze.emplace());
        else if (key == "textured_clouds")
            ReadClouds(reader, value, path, out.textured_clouds.emplace());
        else if (key == "height_fog")
            reader.Object(value, path, [&](const std::string &k, const Json &v, const std::string &p) {
                if (k == "enabled")
                    reader.Bool(v, p, out.height_fog);
                else if (k == "profiles")
                    reader.Object(v, p, [&](const std::string &name, const Json &x, const std::string &q) {
                        if (name != "Land" && name != "Sea")
                            return false;
                        ReadProfile(reader, x, q, (name == "Land" ? out.land : out.sea).emplace());
                        return true;
                    });
                else
                    return false;
                return true;
            });
        else
            return false;
        return true;
    });
    return out;
}

} // namespace storm::scene_data
