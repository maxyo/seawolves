#include "sky.h"

#include "attributes.h"
#include "message.h"
#include "shared/messages.h"
#include "storm/render/effects.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

uint32_t SKY::AttributeChanged(ATTRIBUTES *attribute)
{
    if (!(*attribute == "dir"))
        return 0;
    // One folder for the whole day, or folders d0..d<n-1> for n equal parts of it.
    const uint32_t count = attribute->GetAttributesNum();
    folders_.assign(count, {});
    for (uint32_t n = 0; n < count; ++n)
    {
        const char *name = attribute->GetAttributeName(n);
        const size_t part = count == 1 ? 0 : std::strtoul(name && name[0] == 'd' ? name + 1 : "", nullptr, 10);
        const char *folder = attribute->GetAttribute(n);
        const std::optional<storm::environment::LegacySky> sky =
            storm::environment::LegacySkyFolder(folder ? folder : "");
        if (!sky || part >= count)
        {
            if (reported_.insert(folder ? folder : "").second)
                spdlog::error("sky: no clouds known for the sky folder '{}' ({}): a clear sky there",
                              folder ? folder : "", name ? name : "");
            continue;
        }
        folders_[part] = *sky;
    }
    return 0;
}

std::optional<storm::environment::LegacySky> SKY::Cover(float hour) const
{
    if (folders_.empty())
        return std::nullopt;
    const float f = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f) / 24.0f * static_cast<float>(folders_.size());
    const size_t a = std::min(static_cast<size_t>(f), folders_.size() - 1), b = (a + 1) % folders_.size();
    const float t = f - static_cast<float>(a);
    return storm::environment::LegacySky{folders_[a].clouds + t * (folders_[b].clouds - folders_[a].clouds),
                                         folders_[a].rain_clouds +
                                             t * (folders_[b].rain_clouds - folders_[a].rain_clouds)};
}

uint64_t SKY::ProcessMessage(MESSAGE &message)
{
    if (message.Long() == MSG_SEA_REFLECTION_DRAW)
        storm::render::DrawSky();
    return 0;
}

void SKY::ProcessStage(Stage stage, uint32_t)
{
    if (stage == Stage::realize || stage == Stage::loading_realize)
        storm::render::DrawSky();
}
