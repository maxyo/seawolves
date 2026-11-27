#include "weather.h"
#include "weather_library.h"
#include "astronomy.h"
#include "core.h"
#include "lightning.h"
#include "rain.h"
#include "sky.h"
#include "sun_glow.h"
#include "math_inlines.h"
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
    return true;
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
    cloud_drift_ += glm::vec2(std::cos(fFloats[whf_wind_angle]), std::sin(fFloats[whf_wind_angle])) *
                    fFloats[whf_wind_speed] * static_cast<float>(core.GetDeltaTime()) * 0.001f;
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
        // The scripts colour a band of fog over the horizon on a sphere of this radius, fading out at their fog's
        // height (WEATHER_CALC_FOG_COLOR: its alpha at a point). Asked here, never while a script runs: the event
        // would re-enter the script machine.
        constexpr float kFogBandRadius = 2000.0f, kProbe = 100.0f;
        if (VDATA *band = core.Event(WEATHER_CALC_FOG_COLOR, "fff", kFogBandRadius, kProbe, 0.0f))
        {
            const float alpha = static_cast<float>(static_cast<uint32_t>(band->GetInt()) >> 24) / 255.0f;
            fog_band_ = alpha < 1.0f ? std::min(kProbe / (1.0f - alpha) / kFogBandRadius, 1.0f) : 1.0f;
        }
        PushLegacyEnvironment();
    }
}

// The scripts' attributes as the scene environment.
void WEATHER::PushLegacyEnvironment()
{
    storm::environment::LegacyInputs in;
    in.hour = fFloats[whf_time_counter];
    in.sun = sun_;
    in.moon = moon_;
    in.fixed_light = fFloats[whf_time_speed] == 0.f;
    in.light_elevation = fixed_elevation_;
    in.light_azimuth = fixed_azimuth_;
    GetColor(whc_sun_color, &in.sun_color);
    GetColor(whc_sun_ambient, &in.ambient);
    GetColor(whc_fog_color, &in.fog_color);
    in.fog = iLongs[whi_fog_enable] != 0;
    in.fog_density = fFloats[whf_fog_density];
    in.haze_height = fog_band_;
    if (const auto rain_id = core.GetEntityId("rain"))
        if (const auto *rain = static_cast<RAIN *>(core.GetEntityPointer(rain_id)))
            in.rain_drops = static_cast<float>(rain->NumDrops());
    if (const auto *sky = static_cast<const SKY *>(core.GetEntityPointerSafe(core.GetEntityId("sky"))))
        in.sky = sky->Cover(in.hour).value_or(storm::environment::LegacySky{});
    in.wind_angle = fFloats[whf_wind_angle];
    in.wind_speed = fFloats[whf_wind_speed];
    state_ = storm::environment::EvaluateLegacy(in);
    ApplyEnvironment();
}

void WEATHER::CleanUP()
{
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
        if (*pAttribute == "Height")
        {
            sun_.height = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegAngle")
        {
            sun_.beg_angle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegTime")
        {
            sun_.beg_time = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndAngle")
        {
            sun_.end_angle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndTime")
        {
            sun_.end_time = pAttribute->GetAttributeAsFloat();
            return 0;
        }
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
        if (fFloats[whf_time_speed] == 0.f)
        {
            if (*pAttribute == "HeightAngle")
            {
                fixed_elevation_ = pAttribute->GetAttributeAsFloat();
                return 0;
            }
            if (*pAttribute == "AzimuthAngle")
            {
                fixed_azimuth_ = pAttribute->GetAttributeAsFloat();
                return 0;
            }
        }
    }
    if (*pParent == "moon")
    {
        if (*pAttribute == "Height")
        {
            moon_.height = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegAngle")
        {
            moon_.beg_angle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegTime")
        {
            moon_.beg_time = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndAngle")
        {
            moon_.end_angle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndTime")
        {
            moon_.end_time = pAttribute->GetAttributeAsFloat();
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
