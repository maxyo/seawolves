// Resource/INI/time_of_day.json: the light, sky and camera of the four periods of a day and the sun's and moon's times.
#include "json_reader.hpp"

namespace storm::scene_data
{
namespace
{
using detail::Json;
using detail::Reader;

void ReadPeriod(Reader &reader, const Json &object, const std::string &path, TimeOfDayPeriod &period)
{
    reader.Require(object, path,
                   {"light_color", "light_intensity", "sky_day_radiance", "sky_night_radiance", "sky_fill_color",
                    "sky_fill_intensity", "diffuse_sky_tint", "ground_color", "ground_intensity", "base_exposure",
                    "camera"});
    reader.Object(object, path, [&](const std::string &key, const Json &value, const std::string &child) {
        const std::pair<const char *, Rgb *> colors[] = {{"light_color", &period.light_color},
                                                         {"sky_day_radiance", &period.sky_day_radiance},
                                                         {"sky_night_radiance", &period.sky_night_radiance},
                                                         {"sky_fill_color", &period.sky_fill_color},
                                                         {"diffuse_sky_tint", &period.diffuse_sky_tint},
                                                         {"ground_color", &period.ground_color}};
        const std::pair<const char *, float *> numbers[] = {{"light_intensity", &period.light_intensity},
                                                            {"sky_fill_intensity", &period.sky_fill_intensity},
                                                            {"ground_intensity", &period.ground_intensity}};
        for (const auto &[name, color] : colors)
            if (key == name)
                return reader.Color(value, child, *color), true;
        for (const auto &[name, number] : numbers)
            if (key == name)
                return reader.Float(value, child, *number), true;
        if (key == "base_exposure")
        {
            // Periods blend their exposure in log2.
            if (reader.Float(value, child, period.base_exposure) && !(period.base_exposure > 0.0f))
                reader.Error(child, "must be above 0");
            return true;
        }
        if (key == "camera")
            return detail::ReadToneMapping(reader, value, child, period.camera), true;
        return false;
    });
}
} // namespace

TimeOfDay ParseTimeOfDay(std::string_view json, Report &report)
{
    Reader reader(report);
    TimeOfDay out;
    Json root;
    if (!reader.Parse(json, root))
        return out;
    reader.Require(root, "$", {"version", "enabled", "transition_hours", "celestial", "presets"});
    reader.Object(root, "$", [&](const std::string &key, const Json &value, const std::string &path) {
        if (key == "version")
            reader.Int(value, path, out.version);
        else if (key == "enabled")
            reader.Bool(value, path, out.enabled);
        else if (key == "source_preset")
            reader.String(value, path, out.source_preset);
        else if (key == "transition_hours")
            reader.Transitions(value, path, out.transition_hours);
        else if (key == "celestial")
        {
            CelestialTimes &c = out.celestial;
            const std::pair<const char *, float *> fields[] = {
                {"moon_overlap_hours", &c.moon_overlap_hours},
                {"handoff_hours", &c.handoff_hours},
                {"twilight_hours", &c.twilight_hours},
                {"shadow_morning_hour", &c.shadow_morning_hour},
                {"shadow_evening_hour", &c.shadow_evening_hour},
                {"shadow_morning_fade_minutes", &c.shadow_morning_fade_minutes},
                {"shadow_evening_fade_minutes", &c.shadow_evening_fade_minutes}};
            reader.Require(value, path,
                           {"moon_overlap_hours", "handoff_hours", "twilight_hours", "shadow_morning_hour",
                            "shadow_evening_hour", "shadow_morning_fade_minutes", "shadow_evening_fade_minutes"});
            reader.Object(value, path, [&](const std::string &k, const Json &v, const std::string &p) {
                for (const auto &[name, field] : fields)
                    if (k == name)
                        return reader.Float(v, p, *field), true;
                return false;
            });
            // The sun's and the moon's hand-off and the twilight divide by these.
            if (!(c.handoff_hours > 0.0f && c.twilight_hours > 0.0f))
                reader.Error(path, "handoff_hours and twilight_hours must be above 0");
        }
        else if (key == "presets")
        {
            static constexpr const char *kNames[kPeriods] = {"dawn", "day", "sunset", "night"};
            reader.Require(value, path, {kNames[0], kNames[1], kNames[2], kNames[3]});
            reader.Object(value, path, [&](const std::string &k, const Json &v, const std::string &p) {
                for (int i = 0; i < kPeriods; ++i)
                    if (k == kNames[i])
                        return ReadPeriod(reader, v, p, out.periods[size_t(i)]), true;
                return false;
            });
        }
        else
            return false;
        return true;
    });
    return out;
}

} // namespace storm::scene_data
