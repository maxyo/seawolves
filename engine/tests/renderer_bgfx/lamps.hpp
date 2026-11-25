// A location's still lamps as Lights hands them to the renderer (render::SetEffectsAggregateLamps): one group of one
// flame per lamp; none clears them.
#pragma once

#include "storm/fx/aggregate_light.hpp"
#include "storm/render/effects.hpp"

#include <span>

namespace storm::host_check
{
inline void SetLamps(std::span<const render::EffectsPointLight> lamps)
{
    fx::AggregateSnapshot snapshot;
    for (const render::EffectsPointLight &lamp : lamps)
    {
        const uint64_t id = snapshot.members.size() + 1;
        const glm::vec3 attenuation{lamp.att0, lamp.att1, lamp.att2};
        snapshot.groups.push_back({.id = id,
                                   .center = lamp.position,
                                   .first = static_cast<uint32_t>(snapshot.members.size()),
                                   .count = 1,
                                   .authored_range = lamp.range,
                                   .attenuation = attenuation});
        snapshot.members.push_back({.id = id,
                                    .position = lamp.position,
                                    .color = lamp.color,
                                    .steady = lamp.color,
                                    .authored_range = lamp.range,
                                    .attenuation = attenuation});
    }
    render::SetEffectsAggregateLamps(render::ShapeEffectsAggregates(std::move(snapshot)));
}

// One fixture of several flames (one group, its centre their mean), as Lights merges flames close together; they share
// the first flame's range and attenuation.
inline void SetFixture(std::span<const render::EffectsPointLight> flames)
{
    fx::AggregateSnapshot snapshot;
    glm::vec3 center{0.0f};
    for (const render::EffectsPointLight &flame : flames)
        center += flame.position / static_cast<float>(flames.size());
    const glm::vec3 attenuation{flames[0].att0, flames[0].att1, flames[0].att2};
    snapshot.groups.push_back({.id = 1,
                               .center = center,
                               .first = 0,
                               .count = static_cast<uint32_t>(flames.size()),
                               .authored_range = flames[0].range,
                               .attenuation = attenuation});
    for (const render::EffectsPointLight &flame : flames)
        snapshot.members.push_back({.id = snapshot.members.size() + 1,
                                    .position = flame.position,
                                    .color = flame.color,
                                    .steady = flame.color,
                                    .authored_range = flames[0].range,
                                    .attenuation = attenuation});
    render::SetEffectsAggregateLamps(render::ShapeEffectsAggregates(std::move(snapshot)));
}
} // namespace storm::host_check
