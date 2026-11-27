// The scene environment evaluated from a game's data: the periods of the day, the hourly lighting presets, the
// scripts' weather of the day (README.md).
#pragma once

#include "storm/environment/state.hpp"
#include "storm/scene_data/scene_data.hpp"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace storm::environment
{

// Two neighbouring periods and the share of the second.
struct PeriodMix
{
    int from = scene_data::kNight, to = scene_data::kNight;
    float t = 0.0f;
};
// Before the first transition and after the last it is night; inside a transition the periods blend by smoothstep.
PeriodMix MixPeriods(const scene_data::TransitionHours &hours, float hour);

struct Celestial
{
    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f}, moon_dir{0.0f, -1.0f, 0.0f};
    float sun_weight = 1.0f;  // the sun's share of the direct light: it hands over to the moon at the day's ends
    float twilight = 1.0f;    // daylight in the sky
    float shadow_fade = 1.0f; // the shadows fade out while the light that casts them changes
    bool moon_shadows = false;
};
// The sun's day is 6..20; its noon elevation and the azimuth of its arc's plane in degrees.
Celestial EvaluateCelestial(const scene_data::CelestialTimes &times, float hour, float noon_elevation_deg,
                            float azimuth_deg);

// Caribbean Legend's engine's own transition hours, cloud periods and the layers' rotation speeds (upper, base, rain),
// for a game whose data gives none.
inline constexpr scene_data::TransitionHours kEngineTransitionHours{5.0f, 6.0f, 7.0f, 9.0f, 17.0f, 18.0f, 19.0f, 20.0f};
const std::array<scene_data::CloudPeriod, scene_data::kPeriods> &EngineCloudPeriods();
inline constexpr std::array<float, 3> kEngineCloudSpeeds{0.1f, 0.3f, 1.0f};
// Degrees per second a layer turns per unit of its rotation speed: its engine turns a layer by 0.06 degrees per unit
// of its clock, which runs 1/350 per millisecond.
inline constexpr float kCloudSpinPerSpeed = 0.06f * 1000.0f / 350.0f;
// The clouds' look at `hour`: the periods blended by the transition hours.
CloudLook EvaluateCloudLook(const std::array<scene_data::CloudPeriod, scene_data::kPeriods> &periods,
                            const scene_data::TransitionHours &hours, float hour);

// A preset's 24 hours with the absent ones filled in between the present neighbours on the 24-hour circle; nullopt
// when no hour is present.
using Hours = std::array<scene_data::LightingHour, 24>;
std::optional<Hours> CompleteHours(const scene_data::LightingPreset &preset);

// One hour of the scripts' weather of the day (WeatherState).
struct WeatherHour
{
    float cloud_density = 0.0f;
    float rain_intensity = 0.0f;
    bool rain = false, storm = false, tornado = false, rainbow = false, lightning = false;
    std::string preset;     // the rain preset of the hour; empty: none (the scripts write a number, 0, for none)
    float post_rain = 0.0f; // an hour without rain after one with: the previous hour's rain intensity
};

// Where in a WeatherState row each value is (the scripts' WS_* defines).
struct WeatherFields
{
    int cloud_density, preset, rain_intensity, rain, storm, tornado, rainbow, lightning;
};

// Hours 0..23 of the day and hour 24 (the next day's first), each a row of the scripts' strings, and the rain
// intensity of the hour before the day (fWhrPreRain). An entry that is not a number is reported by `error`.
struct WeatherDay
{
    std::array<WeatherHour, 25> hours;
    float pre_rain = 0.0f;
};
WeatherDay ParseWeatherDay(std::span<const std::array<std::string, 16>, 25> rows, const WeatherFields &fields,
                           float pre_rain, const std::function<void(const std::string &)> &error);

// The hour table the light is taken from: a place's preset with its rain presets laid over the hours that rain.
struct HourTable
{
    Hours hours;
    std::array<bool, 24> rain{}; // the hour's light comes from a rain preset
};
// `rain_preset` finds a rain preset by name (nullptr: none of that name, the caller reports it); at sea a preset
// "<name>_sea" is asked for. A rain preset replaces the sun, ambient, shadow and fog density of its hours, and none
// is used where `rain_allowed` is false (interiors, places without rain).
HourTable BuildHourTable(const Hours &base, const WeatherDay *weather, bool sea, bool rain_allowed,
                         const std::function<const Hours *(const std::string &)> &rain_preset);

// Everything a frame's state comes from. The other pointers may be null when the game has no such data.
struct Inputs
{
    const scene_data::TimeOfDay &time_of_day;
    const scene_data::Atmosphere *atmosphere = nullptr;
    const scene_data::ToneMapping *camera = nullptr; // the game's camera where the time of day does not grade
    const HourTable *table = nullptr;
    const WeatherDay *weather = nullptr;
    float hour = 12.0f;
    bool sky = true; // outdoors
    bool sea = false;
    float noon_elevation_deg = 90.0f, azimuth_deg = 0.0f; // the place's sun arc
};
State Evaluate(const Inputs &in);

// A lightning's flash, shaped as Caribbean Legend's engine shapes it: over its seconds one or two pulses, each rising
// over a tenth of its share and falling over the rest. It multiplies the sky's light and the fill by
// 1 + pulse * (peak - 1), the peak 40 at night. By day (6 to 19 o'clock) the flash message's peak is 0.5, as in
// Caribbean Legend; a bolt of the old lightning entity leaves sky and fill alone by day, its own sprite being the
// flash.
class LightningFlash
{
  public:
    enum class Source
    {
        Message, // the rain's flash message, sent by the game with its bolt effect
        Bolt,    // a bolt of the lightning entity
    };
    // A flash asked for while one runs is not started.
    void Start(float seconds, int pulses, Source source);
    // The multiplier after `seconds` more at `hour`; 1 without a flash.
    float Advance(float seconds, float hour);

  private:
    float time_ = 0.0f, duration_ = 0.0f;
    int pulses_ = 0;
    Source source_ = Source::Message;
};
// The flash on a frame's state: the sky's radiances, the fill the clouds take and the fill added apart from the sky
// (the fill the drawn sky gives follows its radiances).
void ApplyFlash(State &state, float multiplier);

} // namespace storm::environment
