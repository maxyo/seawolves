#include "weather_library.h"

#include "core.h"
#include "s_import_func.h"
#include "v_s_stack.h"
#include "weather.h"

#include <algorithm>
#include <cmath>

namespace
{
std::optional<storm::scene_data::Fog> HourFog()
{
    const auto id = core.GetEntityId("weather");
    const auto *weather = id ? static_cast<WEATHER *>(core.GetEntityPointer(id)) : nullptr;
    return weather ? weather->HourFog() : std::nullopt;
}

uint32_t GetFogDensity(VS_STACK *stack)
{
    auto *result = static_cast<VDATA *>(stack->Push());
    if (!result)
        return IFUNCRESULT_FAILED;
    const std::optional<storm::scene_data::Fog> fog = HourFog();
    result->Set(fog ? fog->density : 0.0f);
    return IFUNCRESULT_OK;
}

// 0xRRGGBB of the fog's 0..1 channels.
uint32_t GetFogColor(VS_STACK *stack)
{
    auto *result = static_cast<VDATA *>(stack->Push());
    if (!result)
        return IFUNCRESULT_FAILED;
    int32_t packed = 0;
    if (const std::optional<storm::scene_data::Fog> fog = HourFog())
        for (const float channel : fog->color)
            packed = packed << 8 | std::clamp(static_cast<int32_t>(std::lround(channel * 255.0f)), 0, 255);
    result->Set(packed);
    return IFUNCRESULT_OK;
}
} // namespace

bool WEATHER_LIBRARY::Init()
{
    IFUNCINFO info;
    info.nArguments = 0;
    info.pFuncName = "GetFogDensity";
    info.pReturnValueName = "float";
    info.pFuncAddress = GetFogDensity;
    core.SetScriptFunction(&info);
    info.pFuncName = "GetFogColor";
    info.pReturnValueName = "int";
    info.pFuncAddress = GetFogColor;
    core.SetScriptFunction(&info);
    return true;
}
