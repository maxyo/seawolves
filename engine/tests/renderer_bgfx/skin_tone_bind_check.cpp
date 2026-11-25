// A draw that binds two stages (Device::bind_textures on the Noop backend): the torso colour test samples stage 0's
// albedo at its 64-texel level; the stage-1 texture (a detail or height layer) leaves that sampling.
#include <bits/stdc++.h> // every standard header before the access change below

#define private public // Device::bind_textures and its draw call are the device's own
#include "device.hpp"
#undef private
#include "effects/effects.hpp"

using namespace storm;

namespace
{
void require(bool good, const char *what)
{
    std::printf("%s %s\n", good ? "PASS" : "FAIL", what);
    if (!good)
        std::exit(1);
}

shim::Texture2D *make_texture(shim::Device &device, UINT width, UINT height, UINT levels)
{
    shim::Texture2D *made = nullptr;
    require(!FAILED(device.CreateTexture(width, height, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &made, nullptr)),
            "CreateTexture");
    return made;
}
} // namespace

int main()
{
    require(shim::Context::instance().init_headless(64, 64), "headless Noop start");
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = params.BackBufferHeight = 64;
    auto *device = new shim::Device(nullptr, params);
    shim::Texture2D *albedo = make_texture(*device, 512, 256, 10), *layer = make_texture(*device, 64, 64, 1);
    device->SetTexture(0, albedo);
    device->SetTexture(1, layer);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
    require(!FAILED(device->BeginScene()), "BeginScene");

    const shim::Device::DrawCall call{};
    const shim::LayoutInfo layout{};
    effects::FfDraw draw;
    draw.skin_tone = {0.47f, 0.32f, 0.2f, 0.7f, 1, 0, 1, 0}; // a character's valid reference
    require(device->bind_textures(call, layout, 0, device->ensure_view(), false, effects::Host::instance().lighting(),
                                  draw),
            "both stages bound");
    require(draw.skin_tone[0] > 0.0f, "the torso test runs on the stage-0 albedo");
    require(draw.skin_tone[7] == 3.0f, "at the albedo's 64-texel level (lod 3 of 512), not the detail layer's");

    device->SetTexture(0, nullptr);
    device->SetTexture(1, nullptr);
    albedo->Release();
    layer->Release();
    device->Release();
    return 0;
}
