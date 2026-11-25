// A character's torso colour test reads the stage-0 albedo the draw binds (src/shim/draw.cpp SetSkinToneSampling):
// at about 64 texels across of the store's own texture or of its ×2, never at a level the texture lacks; the other
// stages' textures leave it.
#include "device.hpp"
#include "effects/upscale.hpp"
#include "storm/fx/skin_tone.hpp"

#include <array>
#include <cstdio>
#include <memory>

namespace
{
bool expect(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

std::array<float, 8> sampled(const storm::shim::TexStore &store, bool upscaled, uint32_t stage = 0)
{
    std::array<float, 8> tone = {0.47f, 0.32f, 0.2f, 0.7f, 1, 0, 1, 0}; // a valid reference
    storm::shim::SetSkinToneSampling(tone, stage, store, upscaled);
    return tone;
}
} // namespace

int main()
{
    storm::shim::TexStore store;
    store.width = 512;
    store.height = 256;
    store.levels = 10;
    store.upscale = std::make_shared<storm::effects::UpscaleEntry>();
    store.upscale->width = 512;
    store.upscale->height = 256;
    bool ok = expect(sampled(store, false)[7] == 3.0f, "a 512 texel albedo: the test at lod 3 (64 texels)");
    ok &= expect(sampled(store, true)[7] == 4.0f, "its ×2: lod 4, the same 64 texels");
    ok &= expect(sampled(store, false, 1)[7] == 0.0f,
                 "stage 1's texture (a detail or height layer) leaves the reference's sampling");
    store.levels = 1;
    ok &= expect(sampled(store, false)[0] < 0.0f, "one mip level, none of 64 texels: the torso stays masked");
    ok &= expect(sampled(store, true)[0] > 0.0f && sampled(store, true)[7] == 4.0f,
                 "its ×2 has every level: the test runs");
    store.full_chain = true;
    ok &= expect(sampled(store, false)[0] > 0.0f && sampled(store, false)[7] == 3.0f,
                 "the rebuilt chain of a one-level texture: the test runs");
    std::array<float, 8> unknown = storm::fx::PackSkinTone({});
    storm::shim::SetSkinToneSampling(unknown, 0, store, true);
    ok &= expect(unknown[0] < 0.0f, "an unknown reference stays masked");
    std::array<float, 8> disabled{};
    storm::shim::SetSkinToneSampling(disabled, 0, store, true);
    ok &= expect(disabled[0] == 0.0f && disabled[7] == 0.0f, "a disabled test stays disabled");
    return ok ? 0 : 1;
}
