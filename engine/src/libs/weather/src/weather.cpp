#include "weather.h"
#include "environment_sky.h"
#include "astronomy.h"
#include "core.h"
#include "lightning.h"
#include "rain.h"
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

CREATE_CLASS(EnvironmentSky)

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
        sky_ = core.CreateEntity("EnvironmentSky");
    return true;
}

void WEATHER::SetBeginData()
{
    fFloats[whf_time_counter] = 0.f;
    fFloats[whf_time_speed] = 0.f;
    fUpdateFrequence = 0.f;

    // sun moving data
    bSunPresent = false;
    fSunHeight = 3.f / 4.f * PId2;
    fSunBegAngle = 0.f;
    fSunBegTime = 5.f;
    fSunEndAngle = PI;
    fSunEndTime = 23.f;
    // moon moving data
    bMoonPresent = false;
    fMoonHeight = 5.f / 4.f * PId2;
    fMoonBegAngle = PId2;
    fMoonBegTime = 23.5f;
    fMoonEndAngle = -PId2;
    fMoonEndTime = 4.5f;
}

void WEATHER::Realize(uint32_t Delta_Time)
{
}

void WEATHER::Execute(uint32_t Delta_Time)
{
    if (fFloats[whf_time_speed] != 0.f)
    {
        const auto fOldTimer = fFloats[whf_time_counter];
        fFloats[whf_time_counter] += core.GetDeltaTime() * fFloats[whf_time_speed];
        // change of day
        if (fFloats[whf_time_counter] > 24.f)
            fFloats[whf_time_counter] -= 24.f;

        if (!data_)
            UpdateSunMoonPos();

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
        PushLegacyEnvironment();
}

// The scripts' attributes as the scene environment: their colours are gamma encoded, their sun or moon is the light.
void WEATHER::PushLegacyEnvironment()
{
    storm::environment::State out;
    out.hour = fFloats[whf_time_counter];
    glm::vec3 light = vVectors[whv_sun_pos];
    if (const float length = glm::length(light); length > 0.0f)
        light /= length;
    out.light_dir = light;
    out.light_is_sun = bSunPresent && !bMoonPresent;
    // The sun from the time of day, also below the horizon, for the sky's day / night state.
    float height, azimuth;
    glm::vec3 sun;
    SunAt(fFloats[whf_time_counter], height, azimuth, sun);
    out.sun_dir = glm::normalize(sun);
    glm::vec3 color;
    GetColor(whc_sun_color, &color);
    out.light_color = color * color;
    GetColor(whc_sun_ambient, &color);
    out.ambient = color * color;
    GetColor(whc_fog_color, &color);
    out.fog_color = color * color;
    out.fog_density = fFloats[whf_fog_density];
    // Rain and overcast from the RAIN entity's drop count: these scripts give WEATHER no rain or lightning state.
    if (const auto rain_id = core.GetEntityId("rain"))
        if (const auto *rain = static_cast<RAIN *>(core.GetEntityPointer(rain_id)))
        {
            // A full rain at the game's day-rain drop count (3500..5000; a light evening rain 1500, storms 25000+).
            out.rain = std::min(static_cast<float>(rain->NumDrops()) / 5000.0f, 1.0f);
            out.overcast = out.rain;
        }
    out.wind_dir = {std::cos(fFloats[whf_wind_angle]), std::sin(fFloats[whf_wind_angle])};
    out.wind_speed = fFloats[whf_wind_speed];
    storm::render::SetSceneEnvironment(out);
}

bool WEATHER::SunAt(float hour, float &height_angle, float &azimuth_angle, glm::vec3 &pos) const
{
    const auto fK = (hour - fSunBegTime) / (fSunEndTime - fSunBegTime);
    if (fK < 0.f || fK > 1.f)
    {
        height_angle = -PId2;
        azimuth_angle = 0.f;
        pos = glm::vec3(0, -5000.f, 0);
        return false;
    }
    height_angle = (fSunHeight + 0.2f) * sinf(fK * PI) - 0.2f;
    azimuth_angle = fSunBegAngle + fK * (fSunEndAngle - fSunBegAngle);
    pos = glm::vec3(5000.0f, 0.0f, 0.0f);
    RotateAroundY(pos.x, pos.y, cosf(-height_angle), sinf(-height_angle));
    RotateAroundY(pos.x, pos.z, cosf(azimuth_angle), sinf(azimuth_angle));
    return true;
}

void WEATHER::UpdateSunMoonPos()
{
    // sun
    bSunPresent = SunAt(fFloats[whf_time_counter], fFloats[whf_sun_height_angle], fFloats[whf_sun_azimuth_angle],
                        vVectors[whv_sun_pos]);
    float fK;

    // moon
    if (fMoonBegTime < fMoonEndTime)
        fK = (fFloats[whf_time_counter] - fMoonBegTime) / (fMoonEndTime - fMoonBegTime);
    else
    {
        fK = (fFloats[whf_time_counter] - fMoonBegTime);
        if (fK < 0.f)
            fK += 24.f;
        fK /= 24.f + fMoonEndTime - fMoonBegTime;
    }
    if (fK < 0.f || fK > 1.f)
    {
        bMoonPresent = false;
    }
    else
    {
        bMoonPresent = true;
        fFloats[whf_sun_height_angle] = (fK < 0.5f) ? (fMoonHeight * fK * 2.f) : (fMoonHeight * (1.f - fK) * 2.f);
        fFloats[whf_sun_azimuth_angle] = fMoonBegAngle + fK * (fMoonEndAngle - fMoonBegAngle);

        glm::vec3 vMoon;
        vMoon.x = 5000.0f;
        vMoon.y = 0.0f;
        RotateAroundY(vMoon.x, vMoon.y, cosf(-fFloats[whf_sun_height_angle]), sinf(-fFloats[whf_sun_height_angle]));
        vMoon.z = 0.0f;
        RotateAroundY(vMoon.x, vMoon.z, cosf(fFloats[whf_sun_azimuth_angle]), sinf(fFloats[whf_sun_azimuth_angle]));
        vVectors[whv_sun_pos] = vMoon;
    }
}

void WEATHER::CleanUP()
{
    if (sky_ != invalid_entity)
        core.EraseEntity(sky_);
    pRS->SetRenderState(D3DRS_FOGENABLE, false);
    pRS->LightEnable(0, false);
}


void WEATHER::SetCommonStates()
{
    // setup sun light
    const auto fSunHeightAngle = GetFloat(whf_sun_height_angle);
    const auto fSunAzimuthAngle = GetFloat(whf_sun_azimuth_angle);
    glm::vec3 vSun, vSunColor;

    if (fFloats[whf_time_speed] == 0.f)
    {
        vSun.x = 5000.0f;
        vSun.y = 0.0f;
        RotateAroundY(vSun.x, vSun.y, cosf(-fSunHeightAngle), sinf(-fSunHeightAngle));

        vSun.z = 0.0f;
        RotateAroundY(vSun.x, vSun.z, cosf(fSunAzimuthAngle), sinf(fSunAzimuthAngle));

        vVectors[whv_sun_pos] = vSun;
    }
    else
    {
        UpdateSunMoonPos();
        vSun = vVectors[whv_sun_pos];
    }

    GetColor(whc_sun_color, &vSunColor);
    SetDeviceStates(GetInt(whi_fog_enable) != 0, GetColor(whc_fog_color), GetFloat(whf_fog_density),
                    GetColor(whc_sun_ambient), storm::math::normalizeOrZero(vSun), vSunColor);
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
            fSunHeight = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegAngle")
        {
            fSunBegAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegTime")
        {
            fSunBegTime = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndAngle")
        {
            fSunEndAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndTime")
        {
            fSunEndTime = pAttribute->GetAttributeAsFloat();
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
                fFloats[whf_sun_height_angle] = pAttribute->GetAttributeAsFloat();
                return 0;
            }
            if (*pAttribute == "AzimuthAngle")
            {
                fFloats[whf_sun_azimuth_angle] = pAttribute->GetAttributeAsFloat();
                return 0;
            }
        }
    }
    if (*pParent == "moon")
    {
        if (*pAttribute == "Height")
        {
            fMoonHeight = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegAngle")
        {
            fMoonBegAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BegTime")
        {
            fMoonBegTime = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndAngle")
        {
            fMoonEndAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "EndTime")
        {
            fMoonEndTime = pAttribute->GetAttributeAsFloat();
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
            SetCommonStates();
    }
    return 0;
}
