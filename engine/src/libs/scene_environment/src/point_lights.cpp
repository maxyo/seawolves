#include "storm/environment/point_lights.hpp"

namespace storm::environment
{
namespace
{
const LitHoursByGroup *g_hours = nullptr;
float g_hour = 0.0f;
} // namespace

void SetPointLightHours(const LitHoursByGroup *hours, float hour)
{
    g_hours = hours;
    g_hour = hour;
}

bool PointLightGroupLit(std::string_view group)
{
    return g_hours == nullptr || scene_data::PointLightGroupLit(*g_hours, group, g_hour);
}
} // namespace storm::environment
