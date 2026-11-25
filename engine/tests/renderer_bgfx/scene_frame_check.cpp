// The device's frames of a scene under preparation (Device on the Noop backend, the real reveal): the game picture's
// target as BeginScene, the post chain's handoff and the Present report it keeps a frame counted, a target replaced
// outside the chain or a new surface does not; a frame drawn hidden discards its draws but still offers them to the
// shadow casters, and runs no post chain; the device is released with the display-size picture parked.
#include <bits/stdc++.h> // every standard header before the access change below

#define private public // the device's frame state and its colour draw, the reveal's frame, the post chain's flag
#include "device.hpp"
#include "effects/effects.hpp"
#include "effects/scene_reveal.hpp"
#undef private
#include "storm/render/perf_stats.hpp"
#include "storm/render/pipelines.hpp"
#include "util.hpp"

using namespace storm;

namespace
{
void require(bool good, const char *what)
{
    std::printf("%s %s\n", good ? "PASS" : "FAIL", what);
    if (!good)
        std::exit(1);
}

struct Vertex
{
    float x, y, z;
};
constexpr Vertex kQuad[4] = {{-1, -1, 5}, {1, -1, 5}, {-1, 1, 5}, {1, 1, 5}};

// A target of the game picture's kind, as Device::create_scene_stores makes them.
shim::TexStorePtr target(shim::Device &device, uint32_t size, DWORD usage, D3DFORMAT format)
{
    shim::TexStorePtr store =
        shim::make_store(&device, shim::TexKind::Tex2D, size, size, 1, 1, usage, format, D3DPOOL_DEFAULT);
    store->backbuffer = true;
    return store;
}

// The perspective state of a 3D scene draw into the game picture.
void scene_state(shim::Device &device)
{
    D3DMATRIX identity{}, proj{};
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
    proj.m[0][0] = 1.0f, proj.m[1][1] = 1.0f, proj.m[2][2] = 1.0001f, proj.m[2][3] = 1.0f, proj.m[3][2] = -0.1f;
    device.SetTransform(D3DTS_WORLD, &identity);
    device.SetTransform(D3DTS_VIEW, &identity);
    device.SetTransform(D3DTS_PROJECTION, &proj);
    device.SetRenderState(D3DRS_LIGHTING, FALSE);
    device.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device.SetVertexShader(nullptr);
    device.SetPixelShader(nullptr);
    device.SetFVF(D3DFVF_XYZ);
}

// The colour draw of the scene's quad (Device::draw): true when the caster entry still gets it.
bool draw_scene(shim::Device &device)
{
    scene_state(device);
    shim::Device::DrawCall call;
    call.type = D3DPT_TRIANGLESTRIP;
    call.primitive_count = 2;
    call.up_vertices = reinterpret_cast<const uint8_t *>(kQuad);
    call.up_stride = sizeof(Vertex);
    call.up_vertex_count = 4;
    return device.draw(call, *device.fvf_layout(D3DFVF_XYZ, sizeof(Vertex)), shim::vertex_count_for(call.type, 2));
}

// After the Present: the frame counts for the reveal (its picture drawn into the target it began with, presented).
bool counted(const effects::SceneReveal &reveal)
{
    return reveal.frame_.picture && reveal.frame_.presented && !reveal.frame_.revoked;
}

void present(shim::Device &device)
{
    require(!FAILED(device.EndScene()), "EndScene");
    require(!FAILED(device.Present()), "Present");
}
} // namespace

int main()
{
    require(shim::Context::instance().init_headless(64, 64), "headless Noop start");
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = params.BackBufferHeight = 64;
    auto *device = new shim::Device(nullptr, params);
    shim::Device &d = *device;
    effects::BeginSceneResources("location/check", 1);
    const effects::SceneReveal &reveal = effects::CurrentSceneReveal();
    const effects::Host &host = effects::Host::instance();
    // The first frame presents on a new back buffer.
    d.BeginScene();
    draw_scene(d);
    present(d);

    // The post chain is not active on the Noop backend (its self-test reads no pixels back): the frame without it,
    // then its handoff of the display-size picture as scene_complete makes it.
    d.BeginScene();
    draw_scene(d);
    present(d);
    require(counted(reveal), "a frame without the post chain counts");
    d.post_color = target(d, 64, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8);
    const shim::TexStore *processed = d.post_color.get();
    d.BeginScene();
    draw_scene(d);
    d.scene_complete();
    d.use_processed_picture(false);
    require(d.back_color->store.get() == processed, "the processed picture is the game picture");
    present(d);
    require(counted(reveal), "the post chain's swap followed by the Present counts");

    // A target replaced outside the post chain, or a new surface, after an interface ended the scene: the presented
    // picture is not the one drawn.
    d.BeginScene();
    draw_scene(d);
    d.scene_complete();
    shim::TexStorePtr drawn_into =
        std::exchange(d.back_color->store, target(d, 64, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8));
    present(d);
    require(!counted(reveal), "a target replaced outside the post chain does not count");
    d.back_color->store = drawn_into; // its frame buffers are the device's
    d.BeginScene();
    draw_scene(d);
    d.scene_complete();
    ++shim::Context::instance().surface_.generation;
    present(d);
    require(!counted(reveal), "a frame across a new surface does not count");

    // A render scale: after a 3D frame the next one draws into the scaled stores and the chain upscales it onto the
    // parked display-size picture.
    d.scaled_color = target(d, 32, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8);
    d.scaled_depth = target(d, 32, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8);
    d.BeginScene();
    draw_scene(d);
    present(d);
    require(d.held_color != nullptr, "a frame after a 3D frame starts on the scaled picture");
    d.BeginScene();
    require(draw_scene(d), "a visible draw");
    d.scene_complete();
    require(host.post_submitted_ && !d.held_color, "the chain upscaled the scene onto the display-size picture");
    present(d);
    require(counted(reveal), "the upscaled frame counts");

    // Under a presented loading cover a frame that rediscovers what it needs is drawn hidden.
    render::ArmSceneCover(true);
    d.BeginScene();
    draw_scene(d);
    d.scene_complete();
    effects::SceneCoverDrawSubmitted(true, true);
    present(d);
    effects::CurrentSceneReveal().demand();
    d.BeginScene();
    require(reveal.frame_.hidden, "the frame after the cover is drawn hidden");
    const render::PerfCounters start = render::PerfTotals();
    require(draw_scene(d), "a hidden draw is still offered to the shadow casters");
    require(render::PerfTotals().Since(start).draws == 0, "a hidden draw is discarded");
    const uint16_t views = d.views_used;
    const render::PerfCounters drawn = render::PerfTotals();
    d.scene_complete();
    require(!host.post_submitted_ && d.views_used == views && d.scene_done && !d.held_color,
            "a hidden scene runs no post chain");
    require(render::PerfTotals().Since(drawn).views == 0, "a hidden scene is not copied up");
    present(d);
    require(!counted(reveal), "a hidden frame does not count");

    // The game's shutdown right after a 3D frame at a render scale: the device goes while the display-size picture is
    // parked and the scaled stores are drawn into.
    require(d.held_color && d.back_color->store == d.scaled_color, "the device goes with the picture parked");
    device->Release();
    return 0;
}
