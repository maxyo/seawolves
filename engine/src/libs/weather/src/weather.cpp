#include "weather.h"
#include "weather_library.h"
#include "astronomy.h"
#include "core.h"
#include "lightning.h"
#include "rain.h"
#include "sky.h"
#include "sun_glow.h"
#include "math_inlines.h"
#include "storm/render/d3d_interop.hpp"
#include "storm/render/effects.hpp"

#include <algorithm>
#include <cmath>

CREATE_CLASS(WEATHER)

CREATE_CLASS(RAIN)

CREATE_CLASS(SUNGLOW)

CREATE_CLASS(LIGHTNING)

CREATE_CLASS(SKY)

CREATE_CLASS(Astronomy)

CREATE_SCRIPTLIBRIARY(WEATHER_LIBRARY)

WEATHER::WEATHER()
{
}

WEATHER::~WEATHER()
{
    CleanUP();
}

void WEATHER::SetDevice()
{
    pRS = static_cast<VRenderer *>(core.GetService("renderer"));
    Assert(pRS);

}

bool WEATHER::Init()
{
    SetDevice();
    SetBeginData();
    data_ = storm::weather::GameEnvironment();
    if (data_)
        sky_ = core.CreateEntity("Sky");
    // Here, behind the location's loading screen: the sky makes its own layers for a game without cloud textures.
    LoadClouds();
    return true;
}

void WEATHER::LoadClouds()
{
    if (!data_ || !data_->clouds)
    {
        storm::render::SetSkyClouds({});
        return;
    }
    const storm::scene_data::CloudPreset &preset = *data_->clouds;
    // A cube of the renderer by the file the library names; 0xffff: not loaded (reported).
    const auto cube = [this](const std::string &file) -> uint16_t {
        const std::optional<std::string> name = storm::scene_data::MaterialTextureName(file);
        const int32_t id = name ? pRS->TextureCreate(name->c_str()) : -1;
        const uint16_t handle =
            id >= 0 ? storm::render::BgfxTextureOf(pRS->GetTextureFromID(id), nullptr, nullptr) : 0xffff;
        if (id >= 0)
            cloud_textures_.push_back(id);
        if (handle == 0xffff)
            ReportOnce("the cloud texture " + file + " is not loaded: its layer is not drawn");
        return handle;
    };
    const auto layer = [&](const storm::scene_data::CloudLayerFiles &files, const char *which) {
        storm::fx::CloudLayer out;
        if (files.albedo_texture.empty())
            return out;
        // Their engine draws only layers whose colours carry their opacity.
        if (!files.premultiplied_alpha)
        {
            ReportOnce(std::string("the ") + which + " cloud layer's textures are not premultiplied: it is not drawn");
            return out;
        }
        out.positive = cube(files.albedo_texture);
        out.negative = cube(files.lighting_texture);
        out.radiance_scale = files.radiance_scale;
        out.ambient_scale = files.ambient_scale;
        out.has_ambient = files.has_ambient;
        return out;
    };
    storm::fx::CloudLayers layers;
    layers.base = layer(preset.base, "base");
    layers.upper = layer(preset.upper, "upper");
    layers.rain = layer(preset.rain, "rain");
    layers.ambient_strength = preset.ambient_strength;
    storm::render::SetSkyClouds(layers);
}

void WEATHER::SetBeginData()
{
    fFloats[whf_time_counter] = 0.f;
    fFloats[whf_time_speed] = 0.f;
    fUpdateFrequence = 0.f;
}

void WEATHER::Realize(uint32_t Delta_Time)
{
}

void WEATHER::Execute(uint32_t Delta_Time)
{
    const float seconds = static_cast<float>(core.GetDeltaTime()) * 0.001f;
    flash_now_ = flash_.Advance(seconds, fFloats[whf_time_counter]);
    gust_now_ = gusts_.Advance(seconds, state_.rain > 0.0f);
    for (size_t i = 0; i < cloud_turn_.size(); ++i)
        cloud_turn_[i] =
            std::remainder(cloud_turn_[i] + glm::radians(state_.cloud_spin[i]) * seconds, glm::two_pi<float>());
    if (fFloats[whf_time_speed] != 0.f)
    {
        const auto fOldTimer = fFloats[whf_time_counter];
        fFloats[whf_time_counter] += core.GetDeltaTime() * fFloats[whf_time_speed];
        // change of day
        if (fFloats[whf_time_counter] > 24.f)
            fFloats[whf_time_counter] -= 24.f;

        if (static_cast<int32_t>(fFloats[whf_time_counter] * fUpdateFrequence) !=
            static_cast<int32_t>(fOldTimer * fUpdateFrequence))
        {
            core.Event("WeatherTimeUpdate", "f", fFloats[whf_time_counter]);
        }
        // updating date in scripts
    }
    if (data_)
    {
        UpdateEnvironment();
        RaiseHourEvents();
        PlaceSky();
    }
    else
    {
        PushLegacyEnvironment();
    }
}

void WEATHER::Flash(float seconds, storm::environment::LightningFlash::Source source)
{
    // One or two pulses, at random, as Caribbean Legend's engine flashes.
    flash_.Start(seconds, 1 + (std::rand() & 1), source);
}

// The scripts' attributes as the scene environment.
void WEATHER::PushLegacyEnvironment()
{
    storm::environment::LegacyInputs in;
    in.hour = fFloats[whf_time_counter];
    in.light_elevation = light_elevation_;
    in.light_azimuth = light_azimuth_;
    // The sun glow knows whether the hour's light is the moon, and the sizes of its sun and moon sprites at their
    // distance.
    if (const auto *glow = core.GetEntityPointerSafe(core.GetEntityId("sunglow")); glow && glow->AttributesPointer)
    {
        ATTRIBUTES *a = glow->AttributesPointer;
        in.moon = a->GetAttributeAsDword("Moon", 0) != 0;
        if (ATTRIBUTES *g = a->FindAClass(a, "Glow"))
            if (const float dist = g->GetAttributeAsFloat("Dist", 0.0f); dist > 0.0f)
            {
                in.sun_size = g->GetAttributeAsFloat("SunSize", 0.0f) / dist;
                in.moon_size = g->GetAttributeAsFloat("MoonSize", 0.0f) / dist;
            }
    }
    GetColor(whc_sun_color, &in.sun_color);
    GetColor(whc_sun_ambient, &in.ambient);
    GetColor(whc_fog_color, &in.fog_color);
    in.fog = iLongs[whi_fog_enable] != 0;
    in.fog_density = fFloats[whf_fog_density];
    if (const auto rain_id = core.GetEntityId("rain"))
        if (const auto *rain = static_cast<RAIN *>(core.GetEntityPointer(rain_id)))
            in.rain_drops = static_cast<float>(rain->NumDrops());
    if (const auto *sky = static_cast<const SKY *>(core.GetEntityPointerSafe(core.GetEntityId("sky"))))
        in.sky = sky->Cover(in.hour).value_or(storm::environment::LegacySky{});
    in.wind_angle = fFloats[whf_wind_angle];
    in.wind_speed = fFloats[whf_wind_speed];
    state_ = storm::environment::EvaluateLegacy(in);
    // The scripts make WEATHER anew for every place: the drying goes on from the last place outdoors.
    static storm::environment::LegacyWetness wetness;
    state_.wetness = wetness.Update(state_.rain, in.hour);
    ApplyEnvironment();
}

void WEATHER::CleanUP()
{
    storm::render::SetSkyClouds({});
    for (const int32_t texture : cloud_textures_)
        pRS->TextureRelease(texture);
    cloud_textures_.clear();
    if (sky_ != invalid_entity)
        core.EraseEntity(sky_);
    pRS->SetRenderState(D3DRS_FOGENABLE, false);
    pRS->LightEnable(0, false);
}


void WEATHER::SetDeviceStates(bool fog, uint32_t fog_color, float fog_density, uint32_t ambient,
                              const glm::vec3 &to_sun, const glm::vec3 &sun_color)
{
    pRS->SetRenderState(D3DRS_FOGENABLE, fog);
    pRS->SetRenderState(D3DRS_FOGCOLOR, fog_color);
    pRS->SetRenderState(D3DRS_FOGDENSITY, *reinterpret_cast<const uint32_t *>(&fog_density));
    pRS->SetRenderState(D3DRS_AMBIENT, ambient);

    D3DLIGHT9 sun{};
    sun.Type = D3DLIGHT_DIRECTIONAL;
    sun.Diffuse = {sun_color.x, sun_color.y, sun_color.z, 1.0f};
    sun.Specular = sun.Diffuse;
    sun.Direction = {-to_sun.x, -to_sun.y, -to_sun.z};
    sun.Range = 100000;
    sun.Attenuation0 = 1;
    sun.Theta = 1;
    sun.Phi = 1;
    pRS->SetLight(0, &sun);
    pRS->LightEnable(0, true);

    pRS->SetRenderState(D3DRS_LIGHTING, false);
}

int32_t WEATHER::GetInt(uint32_t dwCode)
{
    return iLongs[dwCode];
}

uint32_t WEATHER::GetColor(uint32_t dwCode, glm::vec3 *vOut)
{
    vOut->x = static_cast<float>((dwColors[dwCode] >> 0x10) & 0xFF) / 255.0f;
    vOut->y = static_cast<float>((dwColors[dwCode] >> 0x8) & 0xFF) / 255.0f;
    vOut->z = static_cast<float>((dwColors[dwCode]) & 0xFF) / 255.0f;
    return dwColors[dwCode];
}

uint32_t WEATHER::GetColor(uint32_t dwCode)
{
    return dwColors[dwCode];
}

float WEATHER::GetFloat(uint32_t dwCode)
{
    return fFloats[dwCode];
}

void WEATHER::GetVector(uint32_t dwCode, glm::vec3 *vOut)
{
    *vOut = vVectors[dwCode];
}

uint32_t WEATHER::AttributeChanged(ATTRIBUTES *pAttribute)
{
    auto *const pParent = pAttribute->GetParent(); // if (*pAttribute == "Hour")
    if (*pParent == "fog")
    {
        if (*pAttribute == "Enable")
        {
            iLongs[whi_fog_enable] = pAttribute->GetAttributeAsDword();
            return 0;
        }
        if (*pAttribute == "Start")
        {
            fFloats[whf_fog_start] = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Density")
        {
            fFloats[whf_fog_density] = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Color")
        {
            dwColors[whc_fog_color] = pAttribute->GetAttributeAsDword();
            return 0;
        }
    }
    if (*pParent == "wind")
    {
        if (*pAttribute == "Angle")
        {
            fFloats[whf_wind_angle] = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Speed")
        {
            fFloats[whf_wind_speed] = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        return 0;
    }
    if (*pParent == "sun")
    {
        if (*pAttribute == "Color")
        {
            dwColors[whc_sun_color] = pAttribute->GetAttributeAsDword();
            return 0;
        }
        if (*pAttribute == "Ambient")
        {
            dwColors[whc_sun_ambient] = pAttribute->GetAttributeAsDword();
            return 0;
        }
        if (*pAttribute == "HeightAngle")
        {
            light_elevation_ = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "AzimuthAngle")
        {
            light_azimuth_ = pAttribute->GetAttributeAsFloat();
            return 0;
        }
    }
    if (*pParent == "time")
    {
        if (*pAttribute == "time")
        {
            fFloats[whf_time_counter] = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "speed")
        {
            fFloats[whf_time_speed] = pAttribute->GetAttributeAsFloat();
            if (fFloats[whf_time_speed] != 0.f)
                fFloats[whf_time_speed] = 0.001f / fFloats[whf_time_speed];
            return 0;
        }
        if (*pAttribute == "updatefrequence")
        {
            fUpdateFrequence = pAttribute->GetAttributeAsFloat();
            return 0;
        }
    }
    if (*pAttribute == "Location")
    {
        location_ = to_string(pAttribute->GetThisAttr());
        return 0;
    }
    if (*pAttribute == "isDone")
    {
        if (data_)
            UpdateEnvironment();
        else
            PushLegacyEnvironment();
    }
    return 0;
}
