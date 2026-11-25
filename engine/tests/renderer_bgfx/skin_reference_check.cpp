// A character atlas's skin reference (src/effects/skin_reference.hpp): a torso-only first draw keeps the torso masked
// and a later face draw makes the reference; partial draws add up; large ranges are walked in turns; a grey atlas
// never unmasks the torso; a new source starts again.
#include "effects/skin_reference.hpp"
#include "storm/render/skinned_vertex.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace storm;

namespace
{
bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

std::shared_ptr<const fx::TexturePixels> pixels(uint8_t r = 180, uint8_t g = 120, uint8_t b = 80)
{
    auto p = std::make_shared<fx::TexturePixels>();
    p->width = p->height = 4;
    for (int i = 0; i < 16; ++i)
        p->srgb.insert(p->srgb.end(), {r, g, b});
    return p;
}

void set_face(std::vector<uint8_t> &v, uint32_t i, bool face)
{
    v[i * render::kSkinnedSourceStride + render::kSkinnedBonesOffset + render::kSkinWeightByte] = face ? 255 : 0;
}

// `n` forward-facing vertices at the atlas's centre, of the face (head bones) or the torso.
std::vector<uint8_t> vertices(uint32_t n, bool face)
{
    std::vector<uint8_t> v(n * render::kSkinnedSourceStride);
    for (uint32_t i = 0; i < n; ++i)
    {
        uint8_t *p = &v[i * render::kSkinnedSourceStride];
        const float normal[3] = {0, 0, 1}, uv[2] = {0.5f, 0.5f};
        std::memcpy(p + render::kSkinnedNormalOffset, normal, sizeof(normal));
        std::memcpy(p + render::kSkinnedUvOffset, uv, sizeof(uv));
        set_face(v, i, face);
    }
    return v;
}

effects::DrawVertices draw(const std::vector<uint8_t> &v)
{
    return {v.data(), render::kSkinnedSourceStride, static_cast<uint32_t>(v.size() / render::kSkinnedSourceStride)};
}

float tone(effects::SkinReference &reference, const std::vector<uint8_t> &v)
{
    return reference.tone(draw(v), 256, "characters/test")[0];
}
} // namespace

int main()
{
    effects::SkinReference atlas;
    atlas.reset(pixels());
    auto torso = vertices(3, false);
    bool ok = expect(tone(atlas, torso) < 0.0f, "a torso-only first draw: unknown skin");
    for (uint32_t i = 0; i < 3; ++i)
        set_face(torso, i, true);
    const float recovered = tone(atlas, torso);
    ok &= expect(recovered > 0.0f, "a later face draw makes the reference");
    ok &= expect(!atlas.needs_vertices(), "a valid reference is final: no draw is read again");
    effects::SkinReference partial;
    partial.reset(pixels());
    const auto one = vertices(1, true);
    ok &= expect(tone(partial, one) < 0.0f && tone(partial, one) < 0.0f, "too few texels: still unknown");
    ok &= expect(tone(partial, one) > 0.0f, "partial face draws add up to a reference");

    effects::SkinReference large;
    large.reset(pixels());
    auto many = vertices(1100, false);
    for (uint32_t i = 1097; i < 1100; ++i)
        set_face(many, i, true);
    ok &= expect(tone(large, many) < 0.0f, "a large range: the first walk reads a bounded part");
    ok &= expect(tone(large, many) > 0.0f, "the next walk goes on where it stopped and reaches the face");

    effects::SkinReference grey;
    grey.reset(pixels(120, 120, 120));
    const auto faces = vertices(1024, true);
    for (int i = 0; i < 4; ++i)
        ok &= expect(tone(grey, faces) < 0.0f, "a grey face never unmasks the torso");

    const auto face = vertices(3, true);
    atlas.reset(pixels(120, 90, 60));
    const float changed = tone(atlas, face);
    ok &= expect(changed > 0.0f && std::abs(changed - recovered) > 0.01f, "a new source: its own face colour");
    atlas.reset(nullptr);
    ok &= expect(tone(atlas, face) < 0.0f, "an atlas without pixels: unknown skin");
    atlas.reset(pixels());
    ok &= expect(tone(atlas, face) > 0.0f, "a readable source again: a reference");
    return ok ? 0 : 1;
}
