#pragma once

#include "environment_data.hpp"
#include "storm/environment/legacy.hpp"
#include "rain.h"
#include "sky.h"
#include "weather_base.h"
#include "typedef.h"

#include <set>

class WEATHER : public WEATHER_BASE
{
  private:
    VRenderer *pRS = nullptr;

    // Zero until the scripts set them (Execute reads them every frame; not every script set gives every one)
    float fFloats[MAX_FLOAT_PARAMS]{};
    int32_t iLongs[MAX_LONG_PARAMS]{};
    uint32_t dwColors[MAX_COLOR_PARAMS]{};
    glm::vec3 vVectors[MAX_VECTORS_PARAMS]{};
    char *pStrings[MAX_STRINGS_PARAMS]{};

    // timer data
    float fUpdateFrequence;

    // Where the scripts' weather of the hour puts its light (radians).
    float light_elevation_ = 0.0f, light_azimuth_ = 0.0f;

    // The environment evaluated from the game's data (GameEnvironment(); null: the scripts' attributes set it).
    struct Pipeline
    {
        enum class Proc
        {
            Noop,
            Default,
            Inside,
            Sea,
            FixHour
        };
        Proc lum_pos = Proc::Default, wind = Proc::Default, rain_render = Proc::Default, lighting = Proc::Default;
        int fixed_hour = 0;
    };
    const storm::weather::EnvironmentData *data_ = nullptr;
    std::string location_;    // Weather.Location
    std::string built_place_; // the place the hour table was built for
    Pipeline pipeline_;
    std::optional<storm::environment::WeatherDay> day_;
    const storm::weather::PlacePreset *place_ = nullptr;
    std::optional<storm::environment::HourTable> table_;
    storm::environment::State state_;
    glm::vec2 cloud_drift_{0.0f}; // metres the wind has carried the clouds
    entid_t sky_ = invalid_entity; // the sky WEATHER created (environment from data); the scripts' own otherwise
    int event_hour_ = -1;
    bool lightning_ = false, tornado_ = false;
    std::set<std::string> reported_; // problems with the data, each logged once

    void SetBeginData();
    // The environment of the scripts' attributes (environment::EvaluateLegacy).
    void PushLegacyEnvironment();
    // state_ to the renderer, the device and those that ask WEATHER where the light stands.
    void ApplyEnvironment();
    // The fixed-function device's fog, ambient and sun (light 0); colours as the game's 0..1 gamma-encoded values.
    void SetDeviceStates(bool fog, uint32_t fog_color, float fog_density, uint32_t ambient, const glm::vec3 &to_sun,
                         const glm::vec3 &sun_color);
    void CleanUP();

    void ReportOnce(const std::string &problem);
    // MSG_WHR_UPDATE_STATE: the scripts' weather of the day and how this place processes it.
    void ReadWeatherState();
    bool ReadPipeline();
    std::string PlaceId() const;
    void BuildTable();
    // The frame's state to the renderer and the device; then, in play, the scripts' events of the hour.
    void UpdateEnvironment();
    void RaiseHourEvents();
    // The sky entity draws in the layers this weather realizes in, before everything else, and in the sea's reflection.
    void PlaceSky();

    // Weather_Base function
    int32_t GetInt(uint32_t dwCode) override;
    uint32_t GetColor(uint32_t, glm::vec3 *) override;
    uint32_t GetColor(uint32_t) override;
    float GetFloat(uint32_t) override;
    void GetVector(uint32_t, glm::vec3 *) override;

  public:
    WEATHER();
    ~WEATHER() override;

    void SetDevice();
    bool Init() override;
    void Execute(uint32_t Delta_Time);
    void Realize(uint32_t Delta_Time);
    uint32_t AttributeChanged(ATTRIBUTES *pAttribute) override;
    uint64_t ProcessMessage(MESSAGE &message) override;

    // The frame's scene environment.
    const storm::environment::State &Environment() const
    {
        return state_;
    }

    // The fog of the current hour of the place's lighting presets (density per 100 m, linear colour); nullopt when
    // the hour has none or the weather is not evaluated from data.
    std::optional<storm::scene_data::Fog> HourFog() const;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
        case Stage::execute:
            Execute(delta);
            break;
        case Stage::loading_prepare:
            // already configured weather; no clock or script update
            if (data_)
                UpdateEnvironment();
            else
                PushLegacyEnvironment();
            break;
        case Stage::realize:
            Realize(delta);
            break;
        }
    }
};
