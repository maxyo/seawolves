// WEATHER when the scene environment is evaluated from the game's data (scene_environment's README): the scripts give
// the place, the clock and the weather of the day, the data the light, sky and fog.
#include "weather.h"

#include "core.h"
#include "game_profile.hpp"
#include "shared/layers.h"
#include "storm/render/effects.hpp"
#include "v_data.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace
{
using storm::environment::State;

// What the scripts define under `name`; reported once when they do not.
std::optional<int32_t> Define(const char *name, const std::function<void(const std::string &)> &report)
{
    const std::optional<int32_t> value = core.GetScriptIntegerDefine(name);
    if (!value)
        report(std::string("the scripts define no ") + name);
    return value;
}

// A linear colour as the device's packed gamma-encoded 0xRRGGBB.
uint32_t Packed(const glm::vec3 &linear)
{
    const glm::vec3 g = glm::sqrt(glm::clamp(linear, 0.0f, 1.0f));
    const auto byte = [](float v) { return static_cast<uint32_t>(std::lround(v * 255.0f)); };
    return 0xFF000000u | byte(g.x) << 16 | byte(g.y) << 8 | byte(g.z);
}

// Hours 0..23 of `array` (a script array of references to string arrays) into `rows` from `first`.
bool ReadRows(VDATA *array, size_t first, size_t count, std::array<std::array<std::string, 16>, 25> &rows)
{
    if (!array || !array->IsArray())
        return false;
    for (size_t h = 0; h < count; ++h)
    {
        auto *element = reinterpret_cast<VDATA *>(array->GetArrayElement(static_cast<uint32_t>(h)));
        auto *row = element ? reinterpret_cast<VDATA *>(element->GetVarPointer()) : nullptr;
        if (!row || !row->IsArray())
            return false;
        const uint32_t size = std::min<uint32_t>(row->GetElementsNum(), 16);
        for (uint32_t i = 0; i < size; ++i)
        {
            const char *text = nullptr;
            rows[first + h][i] = row->Get(text, i) && text ? text : "";
        }
    }
    return true;
}
} // namespace

void WEATHER::ReportOnce(const std::string &problem)
{
    if (reported_.insert(problem).second)
        spdlog::error("weather: {}", problem);
}

uint64_t WEATHER::ProcessMessage(MESSAGE &message)
{
    const int32_t code = message.Long();
    if (storm::collidingIdMeaning(storm::gameProfile(), storm::CollidingIdDomain::Message, code) ==
            storm::CollidingIdMeaning::WhrUpdateState &&
        data_)
        ReadWeatherState();
    return 0;
}

bool WEATHER::ReadPipeline()
{
    const auto report = [this](const std::string &problem) { ReportOnce(problem); };
    auto *pipeline = static_cast<VDATA *>(core.GetScriptVariable("WeatherPipeline"));
    if (!pipeline)
    {
        ReportOnce("the scripts have no WeatherPipeline: every stage is processed by default");
        pipeline_ = {};
        return false;
    }
    const auto stage = [&](const char *name) -> std::optional<int32_t> {
        const std::optional<int32_t> index = Define(name, report);
        int32_t value = 0;
        if (!index || !pipeline->Get(value, static_cast<uint32_t>(*index)))
            return std::nullopt;
        return value;
    };
    const std::optional<int32_t> noop = Define("WHR_PROC_NOOP", report), inside = Define("WHR_PROC_INSIDE", report),
                                 sea = Define("WHR_PROC_SEA", report), fix = Define("WHR_PROC_FIX_HOUR", report);
    const auto proc = [&](const char *name) {
        const std::optional<int32_t> value = stage(name);
        using Proc = Pipeline::Proc;
        if (!value)
            return Proc::Default;
        return *value == noop     ? Proc::Noop
               : *value == inside ? Proc::Inside
               : *value == sea    ? Proc::Sea
               : *value == fix    ? Proc::FixHour
                                  : Proc::Default;
    };
    pipeline_.lum_pos = proc("WHR_SEC_LUM_POS");
    pipeline_.wind = proc("WHR_SEC_WIND");
    pipeline_.rain_render = proc("WHR_SEC_RAIN_RENDER");
    pipeline_.lighting = proc("WHR_SEC_LIGHTING");
    pipeline_.fixed_hour = stage("WHR_SEC_PARAM").value_or(0);
    return true;
}

void WEATHER::ReadWeatherState()
{
    const auto report = [this](const std::string &problem) { ReportOnce(problem); };
    ReadPipeline();
    std::array<std::array<std::string, 16>, 25> rows;
    const bool read = ReadRows(static_cast<VDATA *>(core.GetScriptVariable("WeatherState")), 0, 24, rows) &&
                      ReadRows(static_cast<VDATA *>(core.GetScriptVariable("WeatherStateNext")), 24, 1, rows);
    const auto field = [&](const char *name) { return Define(name, report).value_or(-1); };
    const storm::environment::WeatherFields fields{field("WS_CLOUD_DENSITY"),  field("WS_PRESET_NAME"),
                                                   field("WS_RAIN_INTENSITY"), field("WS_RAIN_ENABLE"),
                                                   field("WS_STORM_ENABLE"),   field("WS_TORNADO_ENABLE"),
                                                   field("WS_RAINBOW_ENABLE"), field("WS_LIGHTNING_ENABLE")};
    const auto in_row = [](int index) { return index >= 0 && index < 16; };
    if (!read || !in_row(fields.cloud_density) || !in_row(fields.preset) || !in_row(fields.rain_intensity) ||
        !in_row(fields.rain) || !in_row(fields.storm) || !in_row(fields.tornado) || !in_row(fields.rainbow) ||
        !in_row(fields.lightning))
    {
        ReportOnce("the scripts' WeatherState / WeatherStateNext cannot be read: no weather of the day");
        day_.reset();
    }
    else
    {
        float pre_rain = 0.0f;
        if (auto *value = static_cast<VDATA *>(core.GetScriptVariable("fWhrPreRain")))
            value->Get(pre_rain);
        day_ = storm::environment::ParseWeatherDay(rows, fields, pre_rain, report);
    }
    BuildTable();
}

std::string WEATHER::PlaceId() const
{
    // The loaded location's id, else the place the scripts gave (the main menu's location is not the hero's).
    if (const auto id = core.GetEntityId("location"))
        if (const auto *location = core.GetEntityPointerSafe(id); location && location->AttributesPointer)
            if (const char *name = location->AttributesPointer->GetAttribute("id"); name && *name)
                return name;
    return location_;
}

void WEATHER::BuildTable()
{
    using Proc = Pipeline::Proc;
    table_.reset();
    place_ = nullptr;
    built_place_ = PlaceId();
    if (pipeline_.lighting == Proc::Noop)
        return;
    const bool sea = pipeline_.lighting == Proc::Sea;
    // At sea the sea's preset, else the one that lists the place.
    place_ = sea ? data_->Named("Sea") : data_->ForLocation(built_place_);
    if (!place_)
    {
        ReportOnce(sea ? std::string("no lighting preset is named Sea")
                       : "no lighting preset lists location '" + built_place_ + "': its light is the time of day's");
        return;
    }
    const bool rain = pipeline_.rain_render != Proc::Noop && pipeline_.rain_render != Proc::Inside;
    table_ = storm::environment::BuildHourTable(place_->hours, day_ ? &*day_ : nullptr, sea, rain,
                                                [this](const std::string &name) {
                                                    const storm::environment::Hours *hours = data_->Rain(name);
                                                    if (!hours)
                                                        ReportOnce("no rain preset " + name);
                                                    return hours;
                                                });
}

std::optional<storm::scene_data::Fog> WEATHER::HourFog() const
{
    if (!table_)
        return std::nullopt;
    const int hour = static_cast<int>(fFloats[whf_time_counter]) % 24;
    return table_->hours[static_cast<size_t>(std::max(hour, 0))].fog;
}

void WEATHER::UpdateEnvironment()
{
    using Proc = Pipeline::Proc;
    if (pipeline_.lighting != Proc::Sea && PlaceId() != built_place_)
        BuildTable();
    const float clock = fFloats[whf_time_counter];
    storm::environment::Inputs in{data_->time_of_day};
    in.atmosphere = data_->atmosphere ? &*data_->atmosphere : nullptr;
    in.camera = data_->camera ? &*data_->camera : nullptr;
    in.table = table_ ? &*table_ : nullptr;
    in.weather = day_ ? &*day_ : nullptr;
    in.hour = pipeline_.lum_pos == Proc::FixHour ? static_cast<float>(pipeline_.fixed_hour) : clock;
    in.sky = pipeline_.wind != Proc::Inside && pipeline_.rain_render != Proc::Inside;
    in.sea = pipeline_.lighting == Proc::Sea;
    if (place_)
    {
        in.noon_elevation_deg = place_->noon_elevation_deg;
        in.azimuth_deg = place_->azimuth_deg;
    }
    state_ = storm::environment::Evaluate(in);
    state_.wind_dir = {std::sin(fFloats[whf_wind_angle]), std::cos(fFloats[whf_wind_angle])};
    state_.wind_speed = fFloats[whf_wind_speed];
    ApplyEnvironment();
}

void WEATHER::ApplyEnvironment()
{
    state_.cloud_turn = cloud_turn_;
    storm::environment::ApplyFlash(state_, flash_now_);
    state_.wind_gust = gust_now_;
    storm::render::SetSceneEnvironment(state_);
    // The sun glow and the others that ask WEATHER where the light stands.
    vVectors[whv_sun_pos] = state_.light_dir * 5000.0f;
    fFloats[whf_sun_height_angle] = std::asin(std::clamp(state_.light_dir.y, -1.0f, 1.0f));
    fFloats[whf_sun_azimuth_angle] = std::atan2(-state_.light_dir.z, state_.light_dir.x);

    // The fixed-function device takes the light through the exposure, its colours stopping at 1.
    const glm::vec3 fog = state_.fog_from_sky ? storm::render::SkyHorizon() * state_.fog_color
                                              : storm::environment::DisplayLight(state_.fog_color, state_.exposure);
    // A fog that thins with height reaches the device at the eye's density (the renderer's aerial fog lays it out).
    float density = state_.fog_density;
    if (state_.fog_layer > 0.0f)
    {
        glm::vec3 eye, angles;
        float fov;
        pRS->GetCamera(eye, angles, fov);
        density *= std::exp(-std::max(eye.y - state_.fog_base, 0.0f) / state_.fog_layer);
    }
    SetDeviceStates(density > 0.0f, Packed(fog), density,
                    Packed(storm::environment::DisplayLight(storm::render::SceneAmbient(), state_.exposure)),
                    state_.light_dir, glm::sqrt(storm::environment::DisplayLight(state_.light_color, state_.exposure)));
}

void WEATHER::RaiseHourEvents()
{
    // The scripts start and stop lightning and tornadoes when the hour's weather changes.
    if (const int hour = static_cast<int>(state_.hour); hour != event_hour_ && day_)
    {
        event_hour_ = hour;
        if (state_.lightning != lightning_)
            core.Event("Whr_Lightning", "l", static_cast<int32_t>(state_.lightning));
        if (state_.tornado != tornado_)
            core.Event("Whr_Tornado", "l", static_cast<int32_t>(state_.tornado));
        lightning_ = state_.lightning;
        tornado_ = state_.tornado;
    }
}

void WEATHER::PlaceSky()
{
    // Before the scene's own draws, where the scripts of other games place theirs; the sea's reflection draws it first.
    constexpr priority_t kSkyRealize = 3, kSkyReflection = 1;
    if (sky_ == invalid_entity)
        return;
    const auto contains = [](layer_index_t layer, entid_t id) {
        const auto &ids = core.GetEntityIds(layer);
        return std::ranges::find(ids, id) != ids.end();
    };
    for (const layer_index_t layer : {layer_index_t{REALIZE}, layer_index_t{SEA_REALIZE}})
    {
        const bool weather = contains(layer, GetId()), sky = contains(layer, sky_);
        if (weather && !sky)
            core.AddToLayer(layer, sky_, kSkyRealize);
        else if (!weather && sky)
            core.RemoveFromLayer(layer, sky_);
    }
    if (!contains(layer_index_t{SEA_REFLECTION}, sky_))
        core.AddToLayer(layer_index_t{SEA_REFLECTION}, sky_, kSkyReflection);
}
