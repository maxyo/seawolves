// The scene environment's evaluation (src/libs/scene_environment) on data written here; "[corpus]": every lighting
// and rain preset of the installed game in $STORM_GAME_DIR evaluated for every hour (not run without a game that has
// a time of day).
#include "storm/environment/evaluate.hpp"
#include "storm/environment/legacy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <catch2/catch.hpp>

using namespace storm;
using namespace storm::environment;

namespace
{
scene_data::TimeOfDay Day()
{
    scene_data::TimeOfDay tod;
    tod.version = 1;
    tod.enabled = true;
    tod.transition_hours = {5, 7, 8, 10, 17, 18, 19, 21};
    tod.celestial = {2.0f, 0.5f, 0.5f, 6.25f, 20.0f, 1.0f, 1.0f};
    const scene_data::Rgb white{1, 1, 1};
    for (auto &p : tod.periods)
    {
        p.light_color = white;
        p.sky_day_radiance = white;
        p.sky_night_radiance = {0.01f, 0.02f, 0.03f};
        p.sky_fill_color = white;
        p.sky_fill_intensity = 1.0f;
        p.diffuse_sky_tint = white;
        p.ground_color = white;
    }
    tod.periods[scene_data::kDawn].light_intensity = 1.0f;
    tod.periods[scene_data::kDay].light_intensity = 3.0f;
    tod.periods[scene_data::kSunset].light_intensity = 0.5f;
    tod.periods[scene_data::kNight].light_intensity = 0.1f;
    tod.periods[scene_data::kNight].light_color = {0.2f, 0.4f, 1.0f};
    tod.periods[scene_data::kDawn].base_exposure = 2.0f;
    tod.periods[scene_data::kDay].base_exposure = 2.0f;
    tod.periods[scene_data::kSunset].base_exposure = 1.0f;
    tod.periods[scene_data::kNight].base_exposure = 4.0f;
    return tod;
}

scene_data::LightingHour Hour(float sun, float exposure)
{
    scene_data::LightingHour h;
    h.sun_color = {1, 1, 1};
    h.sun_intensity = sun;
    h.ambient_color = {1, 1, 1};
    h.ambient_intensity = sun;
    h.exposure = exposure;
    return h;
}

Hours Flat(float sun)
{
    Hours hours;
    hours.fill(Hour(sun, 1.0f));
    return hours;
}

WeatherFields Fields()
{
    return {2, 3, 4, 5, 6, 7, 8, 9}; // the columns of the scripts' rows
}

// A day of the scripts' weather: `rain` hours rain at `intensity` with preset `preset`.
WeatherDay Weather(std::initializer_list<int> rain, float intensity, const std::string &preset, float pre_rain = 0)
{
    std::array<std::array<std::string, 16>, 25> rows;
    for (auto &row : rows)
    {
        row.fill("0");
        row[2] = "2.5"; // the cloud density
    }
    for (const int h : rain)
    {
        rows[size_t(h)][3] = preset;
        rows[size_t(h)][4] = std::to_string(intensity);
        rows[size_t(h)][5] = "1";
    }
    return ParseWeatherDay(rows, Fields(), pre_rain, [](const std::string &e) { FAIL(e); });
}

bool Finite(const glm::vec3 &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

TEST_CASE("periods of the day")
{
    const scene_data::TransitionHours t = {5, 7, 8, 10, 17, 18, 19, 21};
    const auto at = [&](float hour) { return MixPeriods(t, hour); };
    CHECK((at(3).from == scene_data::kNight && at(3).to == scene_data::kNight));
    CHECK((at(6).from == scene_data::kNight && at(6).to == scene_data::kDawn && at(6).t == Approx(0.5f)));
    CHECK((at(7.5f).from == scene_data::kDawn && at(7.5f).to == scene_data::kDawn));
    CHECK((at(9).from == scene_data::kDawn && at(9).to == scene_data::kDay && at(9).t == Approx(0.5f)));
    CHECK(at(9.5f).t == Approx(0.84375f)); // smoothstep of 0.75
    CHECK((at(12).from == scene_data::kDay && at(12).to == scene_data::kDay));
    CHECK((at(20).from == scene_data::kSunset && at(20).to == scene_data::kNight && at(20).t == Approx(0.5f)));
    CHECK((at(22).from == scene_data::kNight && at(22).to == scene_data::kNight));
    // Hours wrap: -2 is 22, 30 is 6.
    CHECK((at(-2).from == scene_data::kNight && at(-2).to == scene_data::kNight));
    CHECK((at(30).to == scene_data::kDawn && at(30).t == Approx(0.5f)));
}

TEST_CASE("sun and moon")
{
    const scene_data::CelestialTimes times{2.0f, 0.5f, 0.5f, 6.25f, 20.0f, 1.0f, 1.0f};
    const Celestial noon = EvaluateCelestial(times, 13.0f, 90.0f, 0.0f);
    CHECK(noon.sun_dir.y == Approx(1.0f));
    CHECK((noon.sun_weight == Approx(1.0f) && noon.twilight == Approx(1.0f) && !noon.moon_shadows));
    CHECK(noon.moon_dir.y < 0.0f);
    const Celestial tilted = EvaluateCelestial(times, 13.0f, 30.0f, 0.0f);
    CHECK(tilted.sun_dir.y == Approx(0.5f));
    CHECK(EvaluateCelestial(times, 6.0f, 90.0f, 0.0f).sun_dir.y == Approx(0.0f).margin(1e-6));
    CHECK(EvaluateCelestial(times, 9.5f, 90.0f, 0.0f).sun_dir.y == Approx(std::sin(glm::pi<float>() * 0.25f)));
    const Celestial night = EvaluateCelestial(times, 2.0f, 90.0f, 0.0f);
    CHECK((night.sun_dir.y < 0.0f && night.moon_dir.y > 0.0f && night.moon_shadows));
    CHECK((night.sun_weight == 0.0f && night.twilight == 0.0f));
    CHECK(EvaluateCelestial(times, 22.0f, 90.0f, 0.0f).moon_shadows); // from the evening shadow hour
    CHECK(!EvaluateCelestial(times, 19.0f, 90.0f, 0.0f).moon_shadows);
    // The sun hands over to the moon within handoff hours of sunset; the sky darkens within twilight hours of it.
    CHECK(EvaluateCelestial(times, 19.75f, 90.0f, 0.0f).sun_weight == Approx(0.5f));
    CHECK(EvaluateCelestial(times, 20.0f, 90.0f, 0.0f).twilight == Approx(0.5f));
    CHECK(EvaluateCelestial(times, 19.4f, 90.0f, 0.0f).twilight == Approx(1.0f));
    // The shadows fade out at the hour they pass to the moon and back over the fade's minutes.
    CHECK(EvaluateCelestial(times, 20.0f, 90.0f, 0.0f).shadow_fade == Approx(0.0f));
    CHECK(EvaluateCelestial(times, 20.5f, 90.0f, 0.0f).shadow_fade == Approx(1.0f));
    // The azimuth turns the arc about the vertical.
    const Celestial turned = EvaluateCelestial(times, 9.5f, 90.0f, 90.0f);
    const Celestial plain = EvaluateCelestial(times, 9.5f, 90.0f, 0.0f);
    CHECK((turned.sun_dir.y == Approx(plain.sun_dir.y) && std::fabs(turned.sun_dir.z) == Approx(plain.sun_dir.x)));
}

TEST_CASE("absent hours of a preset")
{
    scene_data::LightingPreset preset;
    CHECK(!CompleteHours(preset));
    preset.hours[6] = Hour(1.0f, 2.0f);
    preset.hours[6]->hdr_texture = "morning.hdr";
    preset.hours[18] = Hour(3.0f, 2.0f);
    preset.hours[18]->fog = scene_data::Fog{{0.5f, 0.5f, 0.5f}, 4.0f};
    const std::optional<Hours> hours = CompleteHours(preset);
    REQUIRE(hours);
    CHECK((*hours)[6].sun_intensity == 1.0f);
    CHECK((*hours)[12].sun_intensity == Approx(2.0f));
    CHECK((*hours)[9].sun_intensity == Approx(1.5f));
    CHECK((*hours)[0].sun_intensity == Approx(2.0f)); // half way from 18 to 6 across midnight
    CHECK((*hours)[21].sun_intensity == Approx(2.5f));
    CHECK((*hours)[12].exposure == 1.0f); // not interpolated
    CHECK((*hours)[12].hdr_texture == "morning.hdr");
    REQUIRE((*hours)[12].fog);
    CHECK(((*hours)[12].fog->density == Approx(2.0f) && (*hours)[12].fog->color[0] == 0.5f));

    scene_data::LightingPreset one;
    one.hours[3] = Hour(0.7f, 1.0f);
    const std::optional<Hours> same = CompleteHours(one);
    REQUIRE(same);
    CHECK(((*same)[0].sun_intensity == 0.7f && (*same)[23].sun_intensity == 0.7f));
}

TEST_CASE("the scripts' weather of the day")
{
    const WeatherDay day = Weather({3, 4}, 0.8f, "rain_2", 0.4f);
    CHECK((day.hours[3].rain && day.hours[3].preset == "rain_2" && day.hours[3].rain_intensity == Approx(0.8f)));
    CHECK((!day.hours[2].rain && day.hours[2].preset.empty() && day.hours[2].cloud_density == 2.5f));
    CHECK(day.hours[5].post_rain == Approx(0.8f));
    CHECK(day.hours[4].post_rain == 0.0f);
    CHECK(day.hours[0].post_rain == Approx(0.4f));
    std::array<std::array<std::string, 16>, 25> rows;
    for (auto &row : rows)
        row.fill("0");
    rows[7][2] = "dense";
    int errors = 0;
    ParseWeatherDay(rows, Fields(), 0.0f, [&](const std::string &) { ++errors; });
    CHECK(errors == 1);
}

TEST_CASE("rain presets over the place's hours")
{
    Hours base = Flat(1.0f);
    base[5].fog = scene_data::Fog{{0.1f, 0.2f, 0.3f}, 1.0f};
    Hours rain = Flat(0.2f);
    for (auto &h : rain)
    {
        h.shadow_ao = 0.3f;
        h.fog = scene_data::Fog{{0.9f, 0.9f, 0.9f}, 7.0f};
        h.exposure = 5.0f;
    }
    const WeatherDay day = Weather({5, 6}, 1.0f, "rain_2");
    std::vector<std::string> asked;
    const auto find = [&](const std::string &name) -> const Hours * {
        asked.push_back(name);
        return name == "rain_2_sea" ? &rain : nullptr;
    };
    const HourTable sea = BuildHourTable(base, &day, true, true, find);
    CHECK(asked == std::vector<std::string>{"rain_2_sea", "rain_2_sea"});
    CHECK((sea.rain[5] && sea.rain[6] && !sea.rain[4]));
    CHECK((sea.hours[5].sun_intensity == 0.2f && sea.hours[5].shadow_ao == 0.3f && sea.hours[5].exposure == 1.0f));
    CHECK((sea.hours[5].fog->density == 7.0f && sea.hours[5].fog->color[0] == 0.1f));
    CHECK(sea.hours[6].fog->color[0] == 0.9f);
    CHECK(sea.hours[4].sun_intensity == 1.0f);
    const HourTable land = BuildHourTable(base, &day, false, true, find); // no "rain_2" preset
    CHECK((!land.rain[5] && land.hours[5].sun_intensity == 1.0f));
    const HourTable inside = BuildHourTable(base, &day, true, false, find);
    CHECK((!inside.rain[5] && inside.hours[5].sun_intensity == 1.0f));
}

TEST_CASE("a frame's state")
{
    const scene_data::TimeOfDay tod = Day();
    HourTable table{Flat(0.5f), {}};
    Inputs in{tod};
    in.table = &table;
    in.noon_elevation_deg = 90.0f;

    in.hour = 13.0f;
    const State noon = Evaluate(in);
    CHECK((noon.light_is_sun && noon.light_dir.y == Approx(1.0f)));
    CHECK(noon.light_color.x == Approx(3.0f)); // the day period, not the hour table
    CHECK(noon.exposure == Approx(2.0f));
    CHECK(noon.shadow_strength == Approx(1.0f));
    // The sky's own: the period's radiances as given, the sun's light on its disc, Caribbean Legend's disc sizes.
    CHECK((noon.sky_day == glm::vec3(1.0f) && noon.sky_night == glm::vec3(0.01f, 0.02f, 0.03f)));
    CHECK((noon.sun_light.x == Approx(3.0f) && noon.moon_light == glm::vec3(0.0f)));
    CHECK((noon.sun_size == 0.12f && noon.moon_size == 0.075f));
    // Exposure blends in log2: half way from sunset's 1 to the night's 4 is 2.
    in.hour = 20.0f;
    CHECK(Evaluate(in).exposure == Approx(2.0f));
    in.hour = 2.0f;
    const State night = Evaluate(in);
    CHECK((!night.light_is_sun && night.light_dir.y > 0.0f));
    CHECK(night.light_color.z == Approx(0.1f)); // the night period's moon
    CHECK(night.light_color.x == Approx(0.02f));
    CHECK((night.moon_light.z == Approx(0.1f) && night.sun_light == glm::vec3(0.0f)));
    CHECK(night.exposure == Approx(4.0f));
    // Outdoors the periods' fill multiplies the drawn sky's light; nothing is added apart from it.
    CHECK((noon.sky_fill == glm::vec3(1.0f) && noon.ambient == glm::vec3(0.0f) && noon.fill_intensity == 1.0f));
    CHECK(noon.ground_fill == glm::vec3(0.0f)); // the periods' ground intensity is 0
    // The drawn sky's mean above, tinted, times the fill; the ground's by its luminance.
    State lit = noon;
    lit.sky_tint = {1.0f, 0.5f, 1.0f};
    lit.ground_fill = glm::vec3(0.5f);
    ResolveSkyFill(lit, glm::vec3(0.2f));
    CHECK((lit.ambient.x == Approx(0.2f) && lit.ambient.y == Approx(0.1f)));
    CHECK(lit.ground.x == Approx(0.5f * glm::dot(glm::vec3(0.2f, 0.1f, 0.2f), glm::vec3(0.2126f, 0.7152f, 0.0722f))));

    // The period's camera: its exposure_ev on top of the base exposure, its grade kept for the post chain.
    scene_data::TimeOfDay graded = tod;
    graded.periods[scene_data::kDay].camera.exposure_ev = 1.0f;
    graded.periods[scene_data::kDay].camera.saturation = 1.2f;
    Inputs camera{graded};
    camera.table = &table;
    camera.hour = 13.0f;
    const State bright = Evaluate(camera);
    CHECK(bright.exposure == Approx(4.0f));
    CHECK((bright.grade && bright.grade->saturation == Approx(1.2f)));

    // While an hour rains its preset keeps the light.
    table.rain[13] = table.rain[14] = true;
    in.hour = 13.0f;
    CHECK(Evaluate(in).light_color.x == Approx(0.5f));
    // The clouds and the discs take the rain's light too.
    CHECK(Evaluate(in).sun_light.x == Approx(0.5f));
    in.hour = 12.5f; // half way into the rain
    CHECK(Evaluate(in).light_color.x == Approx(1.75f));
    CHECK(Evaluate(in).sun_light.x == Approx(1.75f));
    table.rain[1] = table.rain[2] = true;
    in.hour = 2.0f; // the moon's light at night
    CHECK(Evaluate(in).moon_light.z == Approx(0.5f));
    table.rain = {};

    // Indoors only the hour table lights.
    in.sky = false;
    table.hours[13].exposure = 3.0f;
    in.hour = 13.0f;
    const State inside = Evaluate(in);
    CHECK((inside.light_color.x == Approx(0.5f) && inside.exposure == Approx(3.0f)));
    CHECK(inside.haze == 0.0f);
    CHECK((!inside.grade && inside.sky_fill == glm::vec3(0.0f)));
    // Indoors the game's own camera grades, its exposure_ev on the hour's.
    scene_data::ToneMapping aces;
    aces.exposure_ev = -1.0f;
    aces.contrast = 0.7f;
    in.camera = &aces;
    const State cellar = Evaluate(in);
    CHECK((cellar.exposure == Approx(1.5f) && cellar.grade && cellar.grade->contrast == Approx(0.7f)));
    in.camera = nullptr;
}

TEST_CASE("the clouds' look of the hour")
{
    // Caribbean Legend's engine's own periods by its own transition hours (5, 6, 7, 9, 17, 18, 19, 20).
    const auto &periods = EngineCloudPeriods();
    const CloudLook noon = EvaluateCloudLook(periods, kEngineTransitionHours, 12.0f);
    CHECK((noon.intensity == Approx(2.09f) && noon.opacity == Approx(0.28f) && noon.twilight == 0.0f));
    const CloudLook dusk = EvaluateCloudLook(periods, kEngineTransitionHours, 18.5f);
    CHECK((dusk.opacity == Approx(0.95f) && dusk.twilight == 1.0f));
    // Half way from the sunset to the night (smoothstep of 0.5): the twilight's share halves, the opacity rises to 1.
    const CloudLook late = EvaluateCloudLook(periods, kEngineTransitionHours, 19.5f);
    CHECK((late.twilight == Approx(0.5f) && late.opacity == Approx(0.975f) && late.ambient_intensity == Approx(0.54f)));
    const CloudLook night = EvaluateCloudLook(periods, kEngineTransitionHours, 2.0f);
    CHECK((night.intensity == Approx(0.35f) && night.twilight == 0.0f));
    // A game's periods come from its data, the layers' speeds in degrees per second.
    scene_data::TimeOfDay tod = Day();
    scene_data::Atmosphere atmosphere;
    scene_data::TexturedClouds &clouds = atmosphere.textured_clouds.emplace();
    clouds.enabled = true;
    clouds.periods = periods;
    clouds.periods[scene_data::kDay].opacity = 0.5f;
    clouds.rotation_speeds = {0.1f, 0.3f, 1.0f};
    Inputs in{tod};
    in.atmosphere = &atmosphere;
    in.hour = 13.0f;
    const State s = Evaluate(in);
    CHECK(s.cloud_look.opacity == Approx(0.5f));
    CHECK(s.cloud_spin[2] == Approx(0.06f * 1000.0f / 350.0f));
}

TEST_CASE("fog of the hour and of the height")
{
    const scene_data::TimeOfDay tod = Day();
    scene_data::Atmosphere atmosphere;
    atmosphere.height_fog = true;
    scene_data::HeightFogProfile land;
    land.enabled = true;
    land.transition_hours = {5, 7, 8, 10, 17, 18, 19, 21};
    for (auto &p : land.periods)
    {
        p.attenuation_distance = 1000.0f;
        p.base_height = 0.0f;
        p.maximum_height = 40.0f;
        p.max_opacity = 0.9f;
        p.start_distance = 10.0f;
        p.max_distance = 4000.0f;
        p.color = {0.2f, 0.4f, 0.6f};
        p.tint = {1, 1, 1};
    }
    atmosphere.land = land;
    HourTable table{Flat(1.0f), {}};
    table.hours[12].fog = scene_data::Fog{{1.0f, 0.0f, 0.0f}, 3.0f};
    Inputs in{tod};
    in.table = &table;
    in.atmosphere = &atmosphere;
    in.hour = 12.0f;
    const State foggy = Evaluate(in);
    CHECK(foggy.fog_density == Approx(0.03f)); // per 100 m in the preset
    CHECK(foggy.fog_color.x == Approx(1.0f));
    in.hour = 15.0f;
    const State clear = Evaluate(in);
    CHECK(clear.fog_density == Approx(0.001f));
    CHECK((clear.fog_color.z == Approx(0.6f) && clear.fog_layer == Approx(40.0f) && clear.fog_max == Approx(0.9f)));
    CHECK((clear.fog_start == Approx(10.0f) && clear.fog_end == Approx(4000.0f)));
    in.hour = 12.5f; // half the hour's fog, half the height fog
    CHECK(Evaluate(in).fog_density == Approx(0.0155f));
    in.sea = true; // no sea profile
    in.hour = 15.0f;
    CHECK(Evaluate(in).fog_density == 0.0f);

    // The haze band's shape: its height, edge and the elevation its sky colour is taken at.
    scene_data::HorizonHaze haze;
    haze.enabled = haze.sky_color = true;
    haze.intensity = 0.8f;
    haze.sky_width_degrees = 30.0f;
    haze.edge_fade = 0.5f;
    haze.sample_elevation_degrees = 2.0f;
    haze.transition_hours = {5, 7, 8, 10, 17, 18, 19, 21};
    for (auto &c : haze.colors)
        c = {{0.5f, 1.0f, 1.0f}, {}, 2.0f};
    atmosphere.horizon_haze = haze;
    const State hazy = Evaluate(in);
    CHECK((hazy.haze == Approx(0.8f) && hazy.haze_height == Approx(0.5f) && hazy.haze_edge == 0.5f &&
           hazy.haze_sample == Approx(glm::radians(2.0f)) && hazy.haze_from_sky && hazy.haze_color.x == Approx(1.0f)));
}

TEST_CASE("rain clouds and wetness")
{
    const scene_data::TimeOfDay tod = Day();
    Inputs in{tod};
    const WeatherDay day = Weather({10, 11}, 1.0f, "rain_1");
    in.weather = &day;
    in.hour = 9.5f; // the rain comes at 10
    CHECK(Evaluate(in).rain_clouds == Approx(0.5f));
    in.hour = 10.5f;
    CHECK(Evaluate(in).rain_clouds == 1.0f);
    in.hour = 12.25f; // it stopped at 12
    const State after = Evaluate(in);
    CHECK(after.rain_clouds == Approx(1.0f - 0.15625f)); // smoothstep of 0.25
    CHECK(after.wetness == Approx(1.0f - 0.25f / 3.0f));
    in.hour = 14.0f;
    CHECK(Evaluate(in).rain_clouds == 0.0f);
    in.hour = 8.0f;
    CHECK(Evaluate(in).rain_clouds == 0.0f);
    CHECK(Evaluate(in).clouds == Approx(0.5f)); // a fifth of the density
}

TEST_CASE("wetness over the hours")
{
    // A rain of 0.6 at 10 and 11; the scripts mark 9, the hour before, rainy without an intensity.
    const scene_data::TimeOfDay tod = Day();
    Inputs in{tod};
    WeatherDay day = Weather({10, 11}, 0.6f, "rain_1");
    day.hours[9].rain = true;
    in.weather = &day;
    const auto wet = [&](float hour) {
        in.hour = hour;
        return Evaluate(in).wetness;
    };
    CHECK(wet(8.5f) == 0.0f);
    CHECK(wet(9.5f) == Approx(0.3f)); // rising with the rain into the rainy hour
    CHECK(wet(10.2f) == Approx(0.6f));
    CHECK(wet(11.9f) == Approx(0.6f));
    // It stopped at 12: a third per hour from 0.6, dry at 13.8, not cut short at 12.6.
    CHECK(wet(12.0f) == Approx(0.6f));
    CHECK(wet(13.0f) == Approx(0.6f - 1.0f / 3.0f));
    CHECK(wet(13.7f) == Approx(0.6f - 1.7f / 3.0f));
    CHECK(wet(13.9f) == 0.0f);
    CHECK(wet(20.0f) == 0.0f);

    // A game without hours: the rain thins out, the place stays as wet as it made it, then dries by the game's hours.
    LegacyWetness legacy;
    CHECK(legacy.Update(0.8f, 22.0f) == Approx(0.8f));
    CHECK(legacy.Update(0.1f, 22.1f) == Approx(0.8f - 0.1f / 3.0f));
    CHECK(legacy.Update(0.0f, 23.0f) == Approx(0.8f - 1.0f / 3.0f));
    CHECK(legacy.Update(0.0f, 0.2f) == Approx(0.8f - 2.2f / 3.0f)); // across midnight
    CHECK(legacy.Update(0.0f, 2.5f) == 0.0f);
    CHECK(legacy.Update(0.5f, 2.5f) == Approx(0.5f));
}

TEST_CASE("a lightning's flash")
{
    // A tenth into its pulse it is half lit: half way to 0.5 by day, to 40 at night; over, nothing.
    using Source = LightningFlash::Source;
    LightningFlash day;
    day.Start(0.25f, 1, Source::Message);
    CHECK(day.Advance(0.0125f, 12.0f) == Approx(0.75f));
    day.Start(1.0f, 1, Source::Message); // a flash runs: no new one
    CHECK(day.Advance(0.25f, 12.0f) == 1.0f);
    // A bolt of the lightning entity leaves the day alone and lights the night.
    LightningFlash bolt;
    bolt.Start(0.25f, 1, Source::Bolt);
    CHECK(bolt.Advance(0.0125f, 12.0f) == 1.0f);
    CHECK(bolt.Advance(0.0f, 22.0f) == Approx(20.5f));
    LightningFlash night;
    night.Start(0.25f, 2, Source::Message);
    CHECK(night.Advance(0.0f, 22.0f) == 1.0f);
    CHECK(night.Advance(0.1250f + 0.00625f, 22.0f) == Approx(20.5f)); // the second pulse
    State lit;
    lit.sky_day = glm::vec3(2.0f);
    lit.fill_intensity = 1.0f;
    ApplyFlash(lit, 0.5f);
    CHECK((lit.sky_day == glm::vec3(1.0f) && lit.fill_intensity == 0.5f && lit.sky_fill == glm::vec3(0.0f)));
}

TEST_CASE("every preset of an installed game for every hour", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    const fs::path ini = root ? fs::path(root) / "Resource" / "INI" : fs::path();
    if (!root || !*root || !fs::exists(ini / "time_of_day.json"))
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game with Resource/INI/time_of_day.json");
        std::exit(77);
    }
    const auto read = [](const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };
    scene_data::Report report;
    const scene_data::TimeOfDay tod = scene_data::ParseTimeOfDay(read(ini / "time_of_day.json"), report);
    const scene_data::Atmosphere atmosphere = scene_data::ParseAtmosphere(read(ini / "atmosphere.json"), report);
    REQUIRE(report.ok());
    std::map<std::string, Hours> rain;
    std::vector<std::pair<std::string, Hours>> places;
    for (const char *folder : {"Lighting", "weather"})
        for (const auto &entry : fs::directory_iterator(fs::path(root) / "Resource" / folder))
        {
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!entry.is_regular_file() || extension != ".json")
                continue;
            scene_data::Report r;
            const scene_data::LightingPreset preset = scene_data::ParseLightingPreset(read(entry.path()), r);
            REQUIRE(r.ok());
            const std::optional<Hours> hours = CompleteHours(preset);
            INFO(entry.path().string());
            REQUIRE(hours);
            (preset.weather_type == "rain" ? rain[entry.path().stem().string()] : places.emplace_back().second) =
                *hours;
        }
    int states = 0;
    for (const auto &[name, hours] : places)
        for (const char *weather : {"0", "rain_1", "rain_4"})
        {
            const WeatherDay day = Weather({0, 5, 12, 23}, 1.0f, weather, 1.0f);
            for (const bool sea : {false, true})
            {
                const HourTable table = BuildHourTable(hours, &day, sea, true, [&](const std::string &n) {
                    return rain.contains(n) ? &rain.at(n) : nullptr;
                });
                for (const bool sky : {false, true})
                    for (float hour = 0.0f; hour < 24.0f; hour += 0.25f)
                    {
                        Inputs in{tod};
                        in.atmosphere = &atmosphere;
                        in.table = &table;
                        in.weather = &day;
                        in.sea = sea;
                        in.sky = sky;
                        in.hour = hour;
                        const State s = Evaluate(in);
                        INFO("hour " << hour << " sea " << sea << " sky " << sky);
                        REQUIRE((Finite(s.light_color) && Finite(s.ambient) && Finite(s.fog_color) &&
                                 Finite(s.light_dir) && Finite(s.haze_color) && Finite(s.sky_day)));
                        REQUIRE((std::isfinite(s.fog_density) && s.fog_density >= 0.0f && s.exposure > 0.0f &&
                                 std::isfinite(s.exposure)));
                        ++states;
                    }
            }
        }
    std::printf("%zu place presets, %zu rain presets, %d states\n", places.size(), rain.size(), states);
}

TEST_CASE("the scripts' attribute weather")
{
    LegacyInputs in;
    in.sun_color = {0.5f, 0.6f, 0.8f};
    in.ambient = {0.2f, 0.2f, 0.4f};
    in.fog_color = {0.5f, 0.5f, 0.5f};
    in.fog = true;
    in.fog_density = 0.002f;

    // The hour's light where the weather puts it: elevation 0.25, azimuth pi/2 (x 0, z -cos).
    in.light_elevation = 0.25f;
    in.light_azimuth = glm::pi<float>() / 2.0f;
    const State evening = EvaluateLegacy(in);
    CHECK(evening.sun_dir.y == Approx(std::sin(0.25f)));
    CHECK(evening.sun_dir.z == Approx(-std::cos(0.25f)));
    CHECK(evening.sun_dir.x == Approx(0.0f).margin(1e-5));
    CHECK((evening.light_is_sun && evening.light_dir == evening.sun_dir && evening.moon_dir.y < 0.0f));
    CHECK(evening.light_color.g == Approx(0.36f)); // gamma 2 to linear
    CHECK(evening.ambient.b == Approx(0.16f));
    CHECK(evening.fog_density == 0.002f);
    CHECK(evening.twilight == 1.0f);
    CHECK((evening.sun_light.x == Approx(evening.light_color.x / 0.74f) && evening.moon_light == glm::vec3(0.0f)));

    in.light_elevation = -1.0f; // a weather whose light is under the horizon: night, nothing casts
    CHECK((!EvaluateLegacy(in).light_is_sun && EvaluateLegacy(in).twilight == 0.0f));

    in.light_elevation = 0.3f; // a night's weather: its light is the moon, of the sprite's size; the sun is down
    in.moon = true;
    in.sun_size = 0.05f;
    in.moon_size = 0.03f;
    const State night = EvaluateLegacy(in);
    CHECK(night.moon_dir.y == Approx(std::sin(0.3f)));
    CHECK((!night.light_is_sun && night.light_dir == night.moon_dir && night.moon_size == 0.03f &&
           night.sun_size == 0.05f && night.moon_light.z == Approx(night.light_color.z / 0.74f) &&
           night.sun_light == glm::vec3(0.0f)));
    CHECK(night.sun_dir.y < 0.0f);
    CHECK(night.twilight == 0.0f);
    in.moon = false;
    in.light_elevation = 0.25f;

    // Clouds: the folders' cover; the rain's drops raise the rain clouds; a storm's folder is overcast.
    in.sky = *LegacySkyFolder("weather\\skies\\16\\");
    in.rain_drops = 2500.0f;
    const State showers = EvaluateLegacy(in);
    CHECK((showers.clouds == Approx(0.65f) && showers.rain == Approx(0.5f) && showers.rain_clouds == Approx(0.5f)));
    in.sky = *LegacySkyFolder("WEATHER/SKIES/Storm02");
    const State storm = EvaluateLegacy(in);
    CHECK((storm.rain_clouds == 1.0f && storm.overcast == 1.0f && storm.shadow_strength == 0.0f));
    CHECK(showers.shadow_strength == Approx(0.5f));
    CHECK(LegacySkyFolder("weather\\skies\\12\\")->clouds < LegacySkyFolder("weather\\skies\\18\\")->clouds);
    CHECK(!LegacySkyFolder("weather\\skies\\Twilight1\\"));

    // The haze band is Caribbean Legend's, in the sky's colour, with or without the scripts' fog.
    CHECK((evening.haze == 1.0f && evening.haze_from_sky && evening.haze_color == glm::vec3(1.0f) &&
           evening.haze_height == Approx(0.150398f) && evening.haze_sample == Approx(0.0247837f)));
    in.fog = false;
    CHECK((EvaluateLegacy(in).fog_density == 0.0f && EvaluateLegacy(in).haze == 1.0f));
}

TEST_CASE("the wind's gusts come only while it rains")
{
    WindGusts gusts(7);
    for (int i = 0; i < 600; ++i)
        CHECK(gusts.Advance(0.1f, false) == 1.0f);
    float peak = 1.0f, last = 1.0f;
    for (int i = 0; i < 1000; ++i) // 100 s of rain: one gust at least
        peak = std::max(peak, last = gusts.Advance(0.1f, true));
    CHECK((peak >= 1.3f && peak <= 1.5f));
    // The rain stops: the gust under way ends, then the gain stays 1.
    for (int i = 0; i < 400; ++i)
        last = gusts.Advance(0.1f, false);
    CHECK(last == 1.0f);
    // A legacy weather's angle: the direction every engine part reads, (sin a, cos a).
    LegacyInputs in;
    in.wind_angle = glm::half_pi<float>();
    CHECK(EvaluateLegacy(in).wind_dir.x == Approx(1.0f));
}
