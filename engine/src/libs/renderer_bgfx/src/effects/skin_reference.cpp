#include "effects/skin_reference.hpp"

#include "storm/render/log.hpp"
#include "storm/render/skinned_vertex.hpp"
#include "storm_glm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

void SkinReference::reset(std::shared_ptr<const fx::TexturePixels> pixels)
{
    pixels_ = std::move(pixels);
    tone_.reset();
    read_ = false;
    cursor_ = 0;
    samples_.clear();
}

std::array<float, 8> SkinReference::tone(const DrawVertices &vertices, uint32_t top, std::string_view name)
{
    if (!pixels_)
        return fx::PackSkinTone({}); // unreadable reference: never turn unknown torso skin into generated relief
    if (!tone_ || !tone_->valid)
    {
        const bool first_read = !read_;
        read_ = true;
        // The texels under the vertices the head, neck and hand bones carry (the skin weight in the third byte of the
        // bone indices) that face forward in the bind pose (+z: the face, not the hair, a hat or the back of the
        // head), 3 x 3 texels each.
        const fx::TexturePixels &pixels = *pixels_;
        std::vector<uint8_t> samples;
        // A material can first be drawn by a torso-only range. Retry later ranges until a real reference arrives;
        // cap each draw's work and rotate across large ranges rather than freezing the first empty result forever.
        constexpr uint32_t kVertexBudget = 1024;
        const uint32_t count = std::min(vertices.count, kVertexBudget);
        if (vertices.data && vertices.stride == render::kSkinnedSourceStride && vertices.count)
            for (uint32_t j = 0; j < count; ++j)
            {
                const uint32_t i = (cursor_ + j) % vertices.count;
                const uint8_t *v = vertices.data + static_cast<size_t>(i) * vertices.stride;
                if (v[render::kSkinnedBonesOffset + render::kSkinWeightByte] < 230)
                    continue;
                glm::vec3 n;
                float uv[2];
                std::memcpy(&n, v + render::kSkinnedNormalOffset, sizeof(n));
                std::memcpy(uv, v + render::kSkinnedUvOffset, sizeof(uv));
                if (n.z <= 0.5f * glm::length(n))
                    continue;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const float u = uv[0] + static_cast<float>(dx) / static_cast<float>(pixels.width);
                        const float w = uv[1] + static_cast<float>(dy) / static_cast<float>(pixels.height);
                        const int x =
                            static_cast<int>((u - std::floor(u)) * static_cast<float>(pixels.width)) % pixels.width;
                        const int y =
                            static_cast<int>((w - std::floor(w)) * static_cast<float>(pixels.height)) % pixels.height;
                        const uint8_t *t = &pixels.srgb[(static_cast<size_t>(y) * pixels.width + x) * 3];
                        samples.insert(samples.end(), t, t + 3);
                    }
            }
        if (vertices.count)
            cursor_ = (cursor_ + count) % vertices.count;
        if (samples.empty() && !first_read)
            return fx::PackSkinTone({});
        // Small face/hand ranges can contribute fewer than the minimum reference texels. Accumulate only a bounded
        // tail; later actual skin must be able to replace an earlier unusable hat/cloth reference.
        constexpr size_t kSampleBytes = 4096 * 3;
        if (samples.size() > kSampleBytes)
            samples.erase(samples.begin(), samples.end() - kSampleBytes);
        const size_t overflow =
            samples_.size() + samples.size() > kSampleBytes ? samples_.size() + samples.size() - kSampleBytes : 0;
        samples_.erase(samples_.begin(), samples_.begin() + overflow);
        samples_.insert(samples_.end(), samples.begin(), samples.end());
        const fx::SkinTone tone = fx::MakeSkinTone(samples_);
        const bool detailed = render::DetailedDiagnostics();
        if (detailed && tone.valid)
            render::Log().info(
                "effects: skin colour of {}: {} texels, chromaticity {:.3f} {:.3f}, luminance {:.2f}..{:.2f}", name,
                tone.samples, tone.r, tone.g, tone.lum_lo, tone.lum_hi);
        else if (detailed && first_read)
            render::Log().info("effects: skin colour of {}: none ({} warm face texels{}): its torso relief is masked",
                               name, tone.samples, vertices.data ? "" : ", its vertices unreadable");
        tone_ = std::make_shared<const fx::SkinTone>(tone);
        if (tone.valid)
            std::vector<uint8_t>().swap(samples_);
    }
    if (!tone_ || !tone_->valid)
        return fx::PackSkinTone({});
    std::array<float, 8> packed = fx::PackSkinTone(*tone_);
    // The lod of the level about kSkinTestTexels across (the atlas's top level is level 0 of the sampler).
    packed[7] = std::max(0.0f, std::log2(static_cast<float>(top) / static_cast<float>(fx::kSkinTestTexels)));
    return packed;
}

} // namespace storm::effects
