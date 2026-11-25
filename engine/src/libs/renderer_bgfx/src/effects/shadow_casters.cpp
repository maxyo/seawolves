#include "effects/shadow_casters.hpp"

#include "shim/device.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{

bool outside(const glm::vec4 &plane, const glm::vec3 &p, float radius)
{
    return glm::dot(glm::vec3(plane), p) + plane.w < -radius;
}

} // namespace

void ShadowVolume::set_clip_bounds(const std::array<float, 16> &m, bool homogeneous_depth, bool depth)
{
    // clip = p * m (row vectors): component c of the clip position is the dot product of (p, 1) with column c.
    const auto column = [&](int c) { return glm::vec4{m[c], m[4 + c], m[8 + c], m[12 + c]}; };
    const glm::vec4 x = column(0), y = column(1), z = column(2), w = column(3);
    // -w <= x <= w, -w <= y <= w, near (0 or -w) <= z <= w.
    const std::array<glm::vec4, 6> clip{w + x, w - x, w + y, w - y, homogeneous_depth ? w + z : z, w - z};
    plane_count = 0;
    for (size_t i = 0; i < (depth ? clip.size() : 4); ++i)
    {
        const glm::vec4 &p = clip[i];
        const float length = glm::length(glm::vec3(p));
        if (length <= 1e-12f)
            continue; // not a plane of this projection (a constant row)
        planes[plane_count++] = p / length;
    }
}

// The caster's sphere centre in the volume's space, or false when the sphere lies outside the volume or its shadow
// outside the camera's view.
bool ShadowCasters::meets(const ShadowVolume &volume, const ShadowCaster &caster, glm::vec3 &in_volume) const
{
    in_volume = caster.center + (caster.origin - volume.origin);
    const float radius = caster.radius;
    if (radius < 0.0f)
        return true; // no bounds: the volume takes it
    if (volume.sphere.w > 0.0f)
    {
        const glm::vec3 d = in_volume - glm::vec3(volume.sphere);
        const float reach = volume.sphere.w + radius;
        if (glm::dot(d, d) > reach * reach)
            return false;
    }
    for (uint8_t i = 0; i < volume.plane_count; ++i)
        if (outside(volume.planes[i], in_volume, radius))
            return false;
    if (volume.light.w < 0.0f || camera_.plane_count == 0)
        return true;
    // The shadow: the hull of the caster's sphere and of the sphere where its shadow ends, in the camera's space. A
    // camera plane that has both spheres outside has the whole shadow outside.
    const glm::vec3 near_end = in_volume + (volume.origin - camera_.origin);
    glm::vec3 far_end;
    float far_radius = radius;
    if (volume.light.w > 0.5f)
    {
        const glm::vec3 to = in_volume - glm::vec3(volume.light);
        const float distance = glm::length(to);
        if (distance <= radius)
            return true; // the light is inside the caster: its shadow goes everywhere
        const float scale = std::max(1.0f, volume.sphere.w / distance);
        far_radius = radius * scale;
        far_end = near_end + to * (scale - 1.0f);
    }
    else
        far_end = near_end - glm::vec3(volume.light) * volume.shadow_length;
    for (uint8_t i = 0; i < camera_.plane_count; ++i)
        if (outside(camera_.planes[i], near_end, radius) && outside(camera_.planes[i], far_end, far_radius))
            return false;
    return true;
}

void ShadowCasters::set_camera(const std::array<float, 16> &view_proj, const glm::vec3 &origin)
{
    camera_.set_clip_bounds(view_proj, false, false);
    camera_.origin = origin;
}

ShadowCasters::Id ShadowCasters::publish(const ShadowVolume &volume)
{
    if (volume.layer == ShadowLayer::Static && volume.still == 0)
        shim::trap("a static shadow volume without the owner of its still geometry takes nothing");
    const bool sphere = volume.sphere[3] > 0.0f;
    if (volumes_.empty())
        boxed_ = sphere;
    boxed_ = boxed_ && sphere;
    if (boxed_)
        for (int k = 0; k < 3; ++k)
        {
            const double c = static_cast<double>(volume.sphere[k]) + volume.origin[k];
            const double lo = c - volume.sphere[3], hi = c + volume.sphere[3];
            lo_[k] = volumes_.empty() ? lo : std::min(lo_[k], lo);
            hi_[k] = volumes_.empty() ? hi : std::max(hi_[k], hi);
        }
    volumes_.push_back(volume);
    stats_.emplace_back();
    return static_cast<Id>(volumes_.size() - 1);
}

bool ShadowCasters::accepts(const ShadowVolume &volume, const ShadowCaster &caster) const
{
    if (volume.layer == ShadowLayer::Static)
        return caster.still != 0 && caster.still == volume.still;
    if (!caster.lit || (volume.still != 0 && caster.still == volume.still))
        return false;
    return volume.pass != Casters::Pass::Sun || caster.sun;
}

bool ShadowCasters::reach(const glm::vec3 &center, float radius, const glm::vec3 &origin, uint64_t still) const
{
    ShadowCaster caster;
    caster.center = center;
    caster.radius = radius;
    caster.origin = origin;
    caster.still = still;
    caster.lit = caster.sun = true; // not known before the draw's states are set: the draw itself is tested again
    return takes(caster);
}

bool ShadowCasters::takes(const ShadowCaster &caster) const
{
    if (volumes_.empty())
        return false;
    if (boxed_ && caster.radius >= 0.0f)
        for (int k = 0; k < 3; ++k)
        {
            const double c = static_cast<double>(caster.center[k]) + caster.origin[k];
            if (c + caster.radius < lo_[k] || c - caster.radius > hi_[k])
                return false;
        }
    glm::vec3 in_volume;
    for (const ShadowVolume &volume : volumes_)
        if (accepts(volume, caster) && meets(volume, caster, in_volume))
            return true;
    return false;
}

uint32_t ShadowCasters::submit(shim::Device &device, Casters &programs, const ShadowCaster &caster)
{
    uint32_t submits = 0;
    glm::vec3 in_volume;
    for (size_t id = 0; id < volumes_.size(); ++id)
    {
        const ShadowVolume &volume = volumes_[id];
        if (!accepts(volume, caster) || !meets(volume, caster, in_volume))
            continue;
        // The program sets its cut uniforms; a failed one is logged by the device.
        Casters::Pass pass = volume.pass;
        if (volume.holder.w > 0.0f && (caster.radius < 0.0f || glm::distance(in_volume, glm::vec3(volume.holder)) <
                                                                   volume.holder.w + caster.radius))
            pass = Casters::Pass::LampHolder;
        const bgfx::ProgramHandle program = programs.program(device, pass, caster.skinned, caster.material);
        if (!bgfx::isValid(program))
            continue;
        float world[16], model[16];
        memcpy(world, caster.world, sizeof(world));
        for (int k = 0; k < 3; ++k)
            world[12 + k] += caster.origin[k] - volume.origin[k];
        bx::mtxMul(model, world, volume.transform.data());
        if (volume.scissor[2] != 0)
            bgfx::setScissor(volume.scissor[0], volume.scissor[1], volume.scissor[2], volume.scissor[3]);
        else
            bgfx::setScissor(UINT16_MAX);
        if (bgfx::isValid(volume.uniform))
            bgfx::setUniform(volume.uniform, volume.uniform_value.data());
        bgfx::setStencil(BGFX_STENCIL_NONE);
        bgfx::setTransform(model);
        bgfx::setState(volume.state | caster.primitive);
        // The caster's buffers, cut textures and bone palette stay for the next volume.
        bgfx::submit(volume.view, program, 0, BGFX_DISCARD_NONE);
        ShadowVolumeStats &stats = stats_[id];
        ++stats.draws;
        stats.triangles += caster.triangles;
        stats.skinned += caster.skinned ? 1 : 0;
        stats.offscreen += caster.offscreen ? 1 : 0;
        if (caster.radius >= 0.0f)
            for (int k = 0; k < 3; ++k)
            {
                stats.lo[k] = std::min(stats.lo[k], in_volume[k] - caster.radius);
                stats.hi[k] = std::max(stats.hi[k], in_volume[k] + caster.radius);
            }
        ++submits;
    }
    bgfx::discard(BGFX_DISCARD_ALL);
    if (submits != 0)
    {
        ++frame_.casters;
        frame_.offscreen += caster.offscreen ? 1 : 0;
        frame_.skinned += caster.skinned ? 1 : 0;
        if (caster.radius < 0.0f)
        {
            ++frame_.unbounded;
            if (render::Once(unbounded_reported_))
                render::Log().info(
                    "effects: shadow casters: a {} draw without bounds (no model node around it, or no vertex "
                    "positions to read) is drawn into every shadow volume; counted as casters_unbounded",
                    caster.skinned ? "skinned" : "lit");
        }
    }
    return submits;
}

void ShadowCasters::end_frame()
{
    last_ = std::move(frame_);
    frame_ = {};
    for (size_t id = 0; id < volumes_.size(); ++id)
    {
        const bgfx::ViewId view = volumes_[id].view;
        auto it =
            std::find_if(last_.views.begin(), last_.views.end(), [&](const ViewCount &v) { return v.view == view; });
        if (it == last_.views.end())
            it = last_.views.insert(last_.views.end(), ViewCount{view, 0, 0});
        it->draws += stats_[id].draws;
        it->triangles += stats_[id].triangles;
    }
    volumes_.clear();
    stats_.clear();
    boxed_ = false;
    camera_.plane_count = 0;
}

} // namespace storm::effects
