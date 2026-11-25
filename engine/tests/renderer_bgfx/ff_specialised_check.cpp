// The specialised fixed-function programs (shaders/ff_programs.cmake) through the shim on a real GPU: for every
// specialised program, a draw of its stage setup shows the same pixels as its general program (fx_ff_specialised=0),
// exactly, and the draw is counted as specialised only with the option on (render::PerfCounters::specialised_draws).
// Also with an alpha test that discards (the programs without the opaque twin's early depth) and while a texture fades
// in (the one stage uniform they read). A setup that differs from a listed one only in its alpha row, its result
// argument or by a stage more takes the general program; a session asked for the general programs draws them from its
// first frame. Colours stay away from 0 and 1 at every step, so a wrong constant shows. The interior kind: a baked
// floor of each setup that has one, in an interior with its AO volume and a lamp, at «Высокая», relief off: host GPUs
// have no EAC RG11 textures, so the settings switch the normal maps off (fx::Support::normal_map) and the kind's relief
// programs cannot be reached.
#include "device.hpp"
#include "check_backend.hpp"
#include "effects/effects.hpp"
#include "effects/lighting.hpp"
#include "lamps.hpp"
#include "shim/perf.hpp"
#include "shim/texture_fades.hpp"
#include "texture_fade.hpp"

#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <utility>
#include <optional>
#include <set>
#include <limits>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kSize = 48;

// The texture fades' clock (shim::TextureFades::set_clock): a fade's share is the same in every frame drawn at one
// time, so the general and the specialised program are compared on equal shares.
uint64_t g_now = 1000;
uint64_t fade_clock()
{
    return g_now;
}

void check(HRESULT result, const char *what)
{
    INFO(what);
    REQUIRE(SUCCEEDED(result));
}

// The D3DTA value of an argument as the table encodes it (selector + 16 complement + 32 alpha replicate).
DWORD argument(uint8_t encoded)
{
    return (encoded & 15u) | ((encoded >> 4 & 1u) ? D3DTA_COMPLEMENT : 0u) |
           ((encoded >> 5 & 1u) ? D3DTA_ALPHAREPLICATE : 0u);
}

struct Draw
{
    uint32_t pixels[kSize * kSize];
    uint64_t specialised;
    uint64_t uniform_bytes; // of the last frame: every view sends the shim's uniforms again
};

class Harness
{
  public:
    // `start`: the effects options asked for before the device exists, as the game's settings are.
    explicit Harness(const std::vector<render::EffectsOption> &start)
    {
        render::ConfigureEffects(0, start);
        shim::texture_fades().set_clock(fade_clock);
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
        host_check::UseBackendFromEnvironment();
        window_ = SDL_CreateWindow("ff_specialised_check", 0, 0, kSize, kSize, 0);
        REQUIRE(window_);
        REQUIRE(shim::Context::instance().init(window_, kSize, kSize, false));
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        device_ = new Device(window_, params);
        check(device_->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target_,
                                          nullptr),
              "CreateRenderTarget");
        check(
            device_->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
            "CreateOffscreenPlainSurface");
        check(device_->GetRenderTarget(0, &back_), "GetRenderTarget");
        // Every texel and channel different and mid-range (a gradient: 0x30..0xB0).
        check(device_->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture_, nullptr),
              "CreateTexture");
        D3DLOCKED_RECT rect;
        check(texture_->LockRect(0, &rect, nullptr, 0), "LockRect(texture)");
        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) =
                    (0x60u + 8 * x) << 24 | (0x30u + 16 * x) << 16 | (0x50u + 12 * y) << 8 | (0x90u - 8 * (x + y));
        texture_->UnlockRect(0);
        // The later stages' texture: another gradient, so a stage that combines two textures shows its operation.
        later_ = make_texture(0x58704838);
    }
    ~Harness()
    {
        texture_->Release();
        later_->Release();
        target_->Release();
        readback_->Release();
        back_->Release();
        device_->Release(); // shuts the context down with the device
        shim::Context::instance().shutdown();
        shim::texture_fades().set_clock(nullptr);
        SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    // Draws `stages` over the target with the option `specialised` and reads the picture back.
    void draw(const std::vector<shim::FfStageCodes> &stages, bool specialised, Draw &out)
    {
        render::ConfigureEffects(0, {{"fx_ff_specialised", specialised ? 1 : 0}});
        // The switch draws with the previous settings until it is prepared, a few frames; a switch that never comes
        // is the check's TIMEOUT. Then two frames with the new ones, the first may build the program.
        for (int after = 0; after < 2;)
        {
            after += render::CurrentEffects().pending ? 0 : 1;
            frame(stages, out);
        }
    }

    // The stages' texture (null: the gradient) and the alpha test (0: off, else D3DCMP_GREATER than it) of the draws.
    shim::Texture2D *bound = nullptr;
    DWORD alpha_ref = 0;

    // A texture of one colour per texel row band, for a swap: mid-range in every channel.
    shim::Texture2D *make_texture(uint32_t argb)
    {
        shim::Texture2D *made = nullptr;
        check(device_->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &made, nullptr), "CreateTexture");
        D3DLOCKED_RECT rect;
        check(made->LockRect(0, &rect, nullptr, 0), "LockRect(texture)");
        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) =
                    argb + 0x04040404u * (x + y);
        made->UnlockRect(0);
        return made;
    }

    // The stages' operations and textures (the bound ones: `bound`, or a gradient per stage 0 and the later stages),
    // stage `stages.size()` disabled.
    void set_stages(const std::vector<shim::FfStageCodes> &stages)
    {
        Device &d = *device_;
        for (DWORD i = 0; i < stages.size(); ++i)
        {
            const shim::FfStageCodes &s = stages[i];
            d.SetTextureStageState(i, D3DTSS_COLOROP, s.colour_op);
            d.SetTextureStageState(i, D3DTSS_COLORARG1, argument(s.colour_arg1));
            d.SetTextureStageState(i, D3DTSS_COLORARG2, argument(s.colour_arg2));
            d.SetTextureStageState(i, D3DTSS_COLORARG0, argument(s.colour_arg0));
            d.SetTextureStageState(i, D3DTSS_ALPHAOP, s.alpha_op);
            d.SetTextureStageState(i, D3DTSS_ALPHAARG1, argument(s.alpha_arg1));
            d.SetTextureStageState(i, D3DTSS_ALPHAARG2, argument(s.alpha_arg2));
            d.SetTextureStageState(i, D3DTSS_ALPHAARG0, argument(s.alpha_arg0));
            d.SetTextureStageState(i, D3DTSS_RESULTARG, s.temp ? D3DTA_TEMP : D3DTA_CURRENT);
            d.SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, 0);
            d.SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            d.SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            d.SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            d.SetTexture(i, s.bound ? (bound ? bound : i == 0 ? texture_ : later_) : nullptr);
        }
        d.SetTextureStageState(static_cast<DWORD>(stages.size()), D3DTSS_COLOROP, D3DTOP_DISABLE);
    }

    // One frame of `stages` with the settings in force.
    void frame(const std::vector<shim::FfStageCodes> &stages, Draw &out)
    {
        const render::PerfCounters frame_start = shim::perf();
        Device &d = *device_;
        check(d.BeginScene(), "BeginScene");
        check(d.SetRenderTarget(0, target_), "SetRenderTarget");
        check(d.SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        check(d.Clear(0, nullptr, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        d.SetTransform(D3DTS_WORLD, &identity);
        d.SetTransform(D3DTS_VIEW, &identity);
        d.SetTransform(D3DTS_PROJECTION, &identity);
        d.SetRenderState(D3DRS_ZENABLE, FALSE);
        d.SetRenderState(D3DRS_LIGHTING, FALSE);
        d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d.SetRenderState(D3DRS_ALPHATESTENABLE, alpha_ref ? TRUE : FALSE);
        d.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        d.SetRenderState(D3DRS_ALPHAREF, alpha_ref);
        d.SetRenderState(D3DRS_TEXTUREFACTOR, 0x70584838);
        set_stages(stages);
        struct QuadVertex
        {
            float x, y, z;
            uint32_t diffuse, specular;
            float u, v;
        };
        // Vertex colours mid-range and different per corner.
        const QuadVertex quad[] = {
            {-1, -1, .5f, 0x9060A070, 0x20302818, 0, 1}, {-1, 1, .5f, 0x70806058, 0x18202830, 0, 0},
            {1, 1, .5f, 0x6070A090, 0x28181820, 1, 0},   {-1, -1, .5f, 0x9060A070, 0x20302818, 0, 1},
            {1, 1, .5f, 0x6070A090, 0x28181820, 1, 0},   {1, -1, .5f, 0xA0907060, 0x30282018, 1, 1}};
        d.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(QuadVertex)), "Draw");
        check(d.EndScene(), "EndScene");
        check(d.GetRenderTargetData(target_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&out.pixels[y * kSize], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        for (DWORD i = 0; i < stages.size(); ++i)
            d.SetTexture(i, nullptr);
        check(d.SetRenderTarget(0, back_), "SetRenderTarget(back)");
        check(d.Present(), "Present");
        out.specialised = shim::perf().specialised_draws - frame_start.specialised_draws;
        out.uniform_bytes = shim::perf().uniform_bytes - frame_start.uniform_bytes;
    }

    // A baked floor of `stages` under a table, from above, with a lamp: settles the effects `options` at «Высокая»
    // until the AO volume is in (an interior) or the lamp lights the floor (outdoors), then reads the picture back.
    void draw_room(const std::vector<shim::FfStageCodes> &stages, const std::vector<render::EffectsOption> &options,
                   Draw &out, bool interior = true)
    {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        const auto box = [&](glm::vec3 lo, glm::vec3 hi) {
            const auto base = static_cast<uint32_t>(positions.size());
            for (int c = 0; c < 8; ++c)
                positions.push_back({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
            const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                                          {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
            for (const auto &f : faces)
                for (const uint32_t v : {f[0], f[1], f[2], f[0], f[2], f[3]})
                    indices.push_back(base + v);
        };
        box({-4.0f, 0.0f, -4.0f}, {4.0f, 3.0f, 4.0f});
        box({-0.6f, 0.76f, -0.6f}, {0.6f, 0.8f, 0.6f});
        render::SetEffectsLocationGeometry({"ff_specialised_check room",
                                            positions,
                                            std::vector<uint32_t>(positions.size(), 0xFF808080u),
                                            {},
                                            indices,
                                            {},
                                            interior,
                                            glm::vec3{-3.0f, -0.2f, -3.0f},
                                            glm::vec3{3.0f, 3.0f, 3.0f}});
        const render::EffectsPointLight lamp{{-2.0f, 2.0f, -2.0f}, {2.0f, 1.8f, 1.5f}, 15.0f, 0.5f, 0.0f, 0.1f};
        render::ConfigureEffects(2, options);
        // Outdoors every frame carries one weather, a night without a sun, so nothing of an earlier section's sun
        // (its town light, its shadow layers) reaches this floor; indoors no frame carries one, as in the game.
        // Every wait is on that progress (the switch, the place's probes the lit draws read, the AO volume's fade-in,
        // the lamp's fade), never on a frame or time budget a loaded host could exhaust: one that never finishes is
        // the check's TIMEOUT.
        for (int after = 0; after < 2;)
        {
            effects::Host &host = effects::Host::instance();
            after += !render::CurrentEffects().pending && !host.lighting().lamp_shadows().fading() &&
                     host.place().state() == effects::PlaceBake::State::Ready &&
                     (interior ? host.ao_volume() && host.lighting().ao_weight() >= 1.0f
                               : host.lighting().last_frame().lamp_lit > 0);
            if (!interior)
                render::SetEffectsWeather(render::EffectsWeather{});
            host_check::SetLamps(std::span(&lamp, 1));
            const render::PerfCounters frame_start = shim::perf();
            room_frame(stages, out);
            out.specialised = shim::perf().specialised_draws - frame_start.specialised_draws;
            out.uniform_bytes = shim::perf().uniform_bytes - frame_start.uniform_bytes;
            SDL_Delay(5);
        }
    }

    // A plain floor of `stages` outdoors under the sun, in perspective from 5 m above, with a lit plate 1 m over it
    // (a moving caster): settles «Высокая» with `options` until the floor is drawn as the ground under the plate's
    // shadow (fs_ff_dyn), then reads the picture back.
    void draw_ground(const std::vector<shim::FfStageCodes> &stages, const std::vector<render::EffectsOption> &options,
                     Draw &out)
    {
        const std::vector<glm::vec3> positions = {
            {-3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, 3.0f}, {-3.0f, 0.0f, 3.0f}};
        const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
        render::SetEffectsLocationGeometry({"ff_specialised_check ground",
                                            positions,
                                            std::vector<uint32_t>(positions.size(), 0xFF909090u),
                                            {},
                                            indices,
                                            {},
                                            false,
                                            glm::vec3{-3.0f, -0.2f, -3.0f},
                                            glm::vec3{3.0f, 3.5f, 3.0f}});
        render::EffectsWeather weather;
        weather.sun_dir = {0.7071f, 0.7071f, 0.0f};
        weather.sky_sun_dir = weather.sun_dir;
        weather.sun_present = true;
        std::vector<render::EffectsOption> settings = {{"fx_ao_volume", 0}, {"fx_lamp_shadows", 0}};
        settings.insert(settings.end(), options.begin(), options.end());
        render::ConfigureEffects(2, settings);
        // On the switch's and the shadow's progress, no frame budget: one that never comes is the check's TIMEOUT.
        for (int after = 0; after < 2;)
        {
            after += !render::CurrentEffects().pending && effects::Host::instance().lighting().last_frame().ground > 0;
            host_check::SetLamps({});
            render::SetEffectsWeather(weather);
            const render::PerfCounters frame_start = shim::perf();
            ground_frame(stages, weather.sun_dir, out);
            out.specialised = shim::perf().specialised_draws - frame_start.specialised_draws;
            SDL_Delay(5);
        }
    }

    Device &device()
    {
        return *device_;
    }

  private:
    void ground_frame(const std::vector<shim::FfStageCodes> &stages, glm::vec3 sun_dir, Draw &out)
    {
        Device &d = *device_;
        check(d.BeginScene(), "BeginScene");
        check(d.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX world{}, view{}, projection{};
        world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;
        view.m[0][0] = 1.0f;
        view.m[1][2] = -1.0f;
        view.m[2][1] = 1.0f;
        view.m[3][2] = 5.0f;
        view.m[3][3] = 1.0f;
        const float zn = 0.5f, zf = 50.0f, cot = 5.0f / 3.0f;
        projection.m[0][0] = projection.m[1][1] = cot;
        projection.m[2][2] = zf / (zf - zn);
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -zn * zf / (zf - zn);
        render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        d.SetTransform(D3DTS_WORLD, &world);
        d.SetTransform(D3DTS_VIEW, &view);
        d.SetTransform(D3DTS_PROJECTION, &projection);
        d.SetRenderState(D3DRS_ZENABLE, TRUE);
        d.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d.SetRenderState(D3DRS_AMBIENT, 0xFF404040);
        d.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d.SetRenderState(D3DRS_TEXTUREFACTOR, 0x70584838);
        D3DMATERIAL9 material{};
        material.Diffuse = material.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        d.SetMaterial(&material);
        D3DLIGHT9 sun{};
        sun.Type = D3DLIGHT_DIRECTIONAL;
        sun.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
        sun.Direction = {-sun_dir.x, -sun_dir.y, -sun_dir.z};
        d.SetLight(0, &sun);
        d.LightEnable(0, TRUE);
        struct Vertex
        {
            float pos[3];
            float nrm[3];
            uint32_t color;
            float u, v;
        };
        const auto quads = [](std::vector<Vertex> &to, float x0, float x1, float y, float z0, float z1, int n,
                              uint32_t color) {
            const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x)
                    for (const auto &c : corners)
                        to.push_back({{x0 + (x1 - x0) * (x + c[0]) / n, y, z0 + (z1 - z0) * (z + c[1]) / n},
                                      {0.0f, 1.0f, 0.0f},
                                      color,
                                      c[0],
                                      c[1]});
        };
        d.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        d.SetRenderState(D3DRS_LIGHTING, FALSE);
        d.SetRenderState(D3DRS_ALPHATESTENABLE, alpha_ref ? TRUE : FALSE);
        d.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        d.SetRenderState(D3DRS_ALPHAREF, alpha_ref);
        set_stages(stages);
        std::vector<Vertex> floor, plate;
        quads(floor, -3.0f, 3.0f, 0.0f, -3.0f, 3.0f, 12, 0xB0909070u);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw floor");
        for (DWORD i = 0; i < stages.size(); ++i)
            d.SetTexture(i, nullptr);
        set_stages({{.colour_op = D3DTOP_SELECTARG1,
                     .colour_arg1 = D3DTA_DIFFUSE,
                     .colour_arg2 = D3DTA_DIFFUSE,
                     .colour_arg0 = D3DTA_CURRENT,
                     .alpha_op = D3DTOP_SELECTARG1,
                     .alpha_arg1 = D3DTA_DIFFUSE,
                     .alpha_arg2 = D3DTA_DIFFUSE,
                     .alpha_arg0 = D3DTA_CURRENT}});
        d.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d.SetRenderState(D3DRS_LIGHTING, TRUE);
        quads(plate, -1.0f, 1.0f, 1.0f, -2.4f, -0.6f, 1, 0xFFFFFFFFu);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, plate.data(), sizeof(Vertex)), "draw plate");
        check(d.EndScene(), "EndScene");
        check(d.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&out.pixels[y * kSize], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d.Present(), "Present");
    }

    void room_frame(const std::vector<shim::FfStageCodes> &stages, Draw &out)
    {
        Device &d = *device_;
        check(d.BeginScene(), "BeginScene");
        check(d.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX world{}, view{}, projection{};
        world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;
        // World x -> view x, world z -> view y, world y -> towards the camera, 5 m up; orthographic over 6 x 6 m.
        view.m[0][0] = 1.0f;
        view.m[1][2] = -1.0f;
        view.m[2][1] = 1.0f;
        view.m[3][2] = 5.0f;
        view.m[3][3] = 1.0f;
        projection.m[0][0] = projection.m[1][1] = 1.0f / 3.0f;
        projection.m[2][2] = 0.1f;
        projection.m[3][3] = 1.0f;
        render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        d.SetTransform(D3DTS_WORLD, &world);
        d.SetTransform(D3DTS_VIEW, &view);
        d.SetTransform(D3DTS_PROJECTION, &projection);
        d.SetRenderState(D3DRS_ZENABLE, FALSE);
        d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d.SetRenderState(D3DRS_LIGHTING, FALSE);
        d.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d.SetRenderState(D3DRS_ALPHATESTENABLE, alpha_ref ? TRUE : FALSE);
        d.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        d.SetRenderState(D3DRS_ALPHAREF, alpha_ref);
        set_stages(stages);
        struct Vertex
        {
            float pos[3];
            float nrm[3];
            uint32_t color;
            float u, v;
        };
        // 12 x 12 quads: the baked colour per vertex, the AO and the lamp per pixel; the texture repeats per quad.
        std::vector<Vertex> floor;
        const int n = 12;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{-3.0f + 6.0f * (x + c[0]) / n, 0.0f, -3.0f + 6.0f * (z + c[1]) / n},
                                     {0.0f, 1.0f, 0.0f},
                                     0xB0607080u,
                                     c[0],
                                     c[1]});
            }
        d.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw floor");
        check(d.EndScene(), "EndScene");
        check(d.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&out.pixels[y * kSize], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        for (DWORD i = 0; i < stages.size(); ++i)
            d.SetTexture(i, nullptr);
        check(d.Present(), "Present");
    }

    SDL_Window *window_ = nullptr;
    Device *device_ = nullptr;
    shim::Surface *target_ = nullptr, *readback_ = nullptr, *back_ = nullptr;
    shim::Texture2D *texture_ = nullptr, *later_ = nullptr;
};

std::vector<shim::FfStageCodes> stages_of(const effects::FfSetupRow &row)
{
    return {row.stage.begin(), row.stage.begin() + row.count};
}

const effects::FfSetupRow &setup_row(uint8_t id)
{
    const auto found = std::find_if(effects::kFfSetupRows.begin(), effects::kFfSetupRows.end(),
                                    [id](const effects::FfSetupRow &s) { return s.id == id; });
    REQUIRE(found != effects::kFfSetupRows.end());
    return *found;
}

// The median alpha of a picture: an alpha test against it discards part of the picture and keeps part.
DWORD median_alpha(const Draw &d)
{
    std::vector<uint32_t> alphas;
    for (const uint32_t p : d.pixels)
        alphas.push_back(p >> 24);
    std::nth_element(alphas.begin(), alphas.begin() + alphas.size() / 2, alphas.end());
    return alphas[alphas.size() / 2];
}

// Pixels that differ and the largest channel step.
std::string compare(const Draw &a, const Draw &b, int &differ)
{
    differ = 0;
    int largest = 0;
    for (uint32_t i = 0; i < kSize * kSize; ++i)
        if (a.pixels[i] != b.pixels[i])
        {
            ++differ;
            for (int shift = 0; shift < 32; shift += 8)
                largest =
                    std::max(largest, std::abs(int(a.pixels[i] >> shift & 255) - int(b.pixels[i] >> shift & 255)));
        }
    char text[96];
    std::snprintf(text, sizeof(text), "%d pixels differ, by up to %d; middle %08x against %08x", differ, largest,
                  a.pixels[kSize * kSize / 2 + kSize / 2], b.pixels[kSize * kSize / 2 + kSize / 2]);
    return text;
}

} // namespace

TEST_CASE("a specialised program draws exactly what its general program draws, and only for its setup")
{
    // A session asked for the general programs draws them from its first frame, before the device's first settings
    // switch is prepared.
    Harness harness({{"fx_ff_specialised", 0}});
    {
        const effects::FfSetupRow &setup = effects::kFfSetupRows.front();
        Draw first;
        REQUIRE(render::CurrentEffects().pending);
        harness.frame({setup.stage.begin(), setup.stage.begin() + setup.count}, first);
        CHECK(first.specialised == 0);
    }
    size_t plain = 0, fading = 0;
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        if (row.program.entry != effects::FfEntry::Plain)
            continue;
        ++plain;
        INFO(row.name);
        const effects::FfSetupRow *setup = &setup_row(row.program.setup);
        Draw on, off;
        harness.draw(stages_of(*setup), true, on);
        harness.draw(stages_of(*setup), false, off);
        int differ = 0;
        const std::string text = compare(on, off, differ);
        INFO(text);
        CHECK(differ == 0);
        CHECK(on.specialised == 1);
        // A specialised program is sent its fade shares, not the stage rows.
        CHECK(on.uniform_bytes < off.uniform_bytes);
        CHECK(off.specialised == 0);
        // Not saturated: the picture is neither black nor white in any channel.
        for (const uint32_t p : on.pixels)
            for (int shift = 0; shift < 32; shift += 8)
                CHECK(((p >> shift & 255) > 0 && (p >> shift & 255) < 255));

        // The discarding program (not the opaque twin): an alpha test that keeps part of the quad.
        {
            harness.alpha_ref = median_alpha(on);
            Draw tested_on, tested_off;
            harness.draw(stages_of(*setup), true, tested_on);
            harness.draw(stages_of(*setup), false, tested_off);
            harness.alpha_ref = 0;
            int tested_differ = 0;
            const std::string tested = compare(tested_on, tested_off, tested_differ);
            INFO("alpha-tested: " << tested);
            CHECK(tested_differ == 0);
            CHECK(tested_on.specialised == 1);
            const auto discarded = std::count(std::begin(tested_on.pixels), std::end(tested_on.pixels), 0u);
            CHECK(discarded > 0);
            CHECK(discarded < static_cast<long>(kSize * kSize));
        }

        // A stage 0 texture swapped in front of the player fades from the previous one (its map z above 1).
        {
            Draw faded[2], before[2], after[2];
            for (const bool specialised : {true, false})
            {
                shim::Texture2D *shown = harness.make_texture(0x80603050), *next = harness.make_texture(0x60405070);
                Draw &before_swap = before[specialised];
                harness.bound = next; // drawn once: its GPU texture exists (made at the first draw)
                harness.frame(stages_of(*setup), before_swap);
                harness.bound = shown;
                Draw &fading = faded[specialised], &done = after[specialised];
                harness.draw(stages_of(*setup), specialised, before_swap);
                std::swap(shim::store_of(shown)->handle, shim::store_of(next)->handle);
                g_now += shim::kTextureFadeMs / 2;
                harness.frame(stages_of(*setup), fading); // the swap: the fade starts at this frame's time
                g_now += shim::kTextureFadeMs / 3;
                harness.frame(stages_of(*setup), fading);
                g_now += shim::kTextureFadeMs;
                harness.frame(stages_of(*setup), done);
                harness.frame(stages_of(*setup), done);
                std::swap(shim::store_of(shown)->handle, shim::store_of(next)->handle);
                harness.bound = nullptr;
                shown->Release();
                next->Release();
            }
            int fade_differ = 0, from_before = 0, from_after = 0, swapped = 0;
            const std::string text = compare(faded[1], faded[0], fade_differ);
            INFO("fading: " << text);
            CHECK(fade_differ == 0);
            compare(faded[1], before[1], from_before);
            compare(faded[1], after[1], from_after);
            // A setup that reads its stage 0 texture: the fade shows both textures, neither the one before nor after.
            compare(before[1], after[1], swapped);
            if (swapped > 0)
            {
                ++fading;
                CHECK(from_before > 0);
                CHECK(from_after > 0);
            }
            CHECK(faded[1].specialised == 1);
            CHECK(faded[0].specialised == 0);
        }
    }
    CHECK(plain > 0);
    CHECK(fading > 0);

    // The interior kind's programs, relief off, for a baked floor of their setup, also with linear light.
    size_t room = 0;
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        const effects::FfFamilyRow &family = effects::kFfFamilyRows[size_t(row.program.family)];
        if (row.program.entry != effects::FfEntry::Family || !family.room || row.program.relief != 0 ||
            row.program.specular != 0)
            continue;
        INFO(row.name);
        const std::vector<shim::FfStageCodes> stages = stages_of(setup_row(row.program.setup));
        Draw on, off, linear_on, linear_off;
        harness.draw_room(stages, {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}}, on);
        harness.draw_room(stages, {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}, {"fx_ff_specialised", 0}}, off);
        harness.draw_room(stages, {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}, {"fx_light_linear", 1}}, linear_on);
        harness.draw_room(
            stages, {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}, {"fx_light_linear", 1}, {"fx_ff_specialised", 0}},
            linear_off);
        // The discarding program: an alpha test that keeps part of the floor (no alpha to coverage instead of it).
        Draw tested_on, tested_off;
        harness.alpha_ref = median_alpha(on);
        harness.draw_room(stages, {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}, {"fx_alpha_to_coverage", 0}},
                          tested_on);
        harness.draw_room(
            stages,
            {{"fx_ao_volume", 1}, {"fx_lamp_shadows", 8}, {"fx_alpha_to_coverage", 0}, {"fx_ff_specialised", 0}},
            tested_off);
        harness.alpha_ref = 0;
        int tested_differ = 0;
        const std::string tested = compare(tested_on, tested_off, tested_differ);
        INFO("alpha-tested: " << tested);
        CHECK(tested_differ == 0);
        CHECK(tested_on.specialised == 1);
        CHECK(tested_off.specialised == 0);
        const auto discarded = std::count(std::begin(tested_on.pixels), std::end(tested_on.pixels), 0xFF000000u);
        CHECK(discarded > 0);
        CHECK(discarded < static_cast<long>(kSize * kSize));
        int differ = 0, linear_differ = 0;
        const std::string text = compare(on, off, differ), linear_text = compare(linear_on, linear_off, linear_differ);
        INFO(text);
        INFO("linear light: " << linear_text);
        CHECK(differ == 0);
        CHECK(linear_differ == 0);
        CHECK(on.specialised == 1);
        CHECK(off.specialised == 0);
        CHECK(linear_on.specialised == 1);
        CHECK(linear_off.specialised == 0);
        // Linear light shows: the pair is not the plain one.
        int linear_shows = 0;
        compare(on, linear_on, linear_shows);
        CHECK(linear_shows > 0);
        // The floor under the table is darker than in the open (the AO volume takes part), the lamp's corner brighter.
        const auto green = [](const Draw &d, float u, float v) {
            return int(d.pixels[int(v * kSize) * kSize + int(u * kSize)] >> 8 & 255);
        };
        CHECK(green(on, 0.5f, 0.5f) < green(on, 0.8f, 0.8f));
        CHECK(green(on, 0.5f, 0.5f) > 0);
        ++room;
    }
    CHECK(room > 0);

    // The ground programs: a floor of their setup under a moving caster's sun shadow, also alpha-tested.
    size_t ground = 0;
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        if (row.program.entry != effects::FfEntry::Ground)
            continue;
        INFO(row.name);
        const std::vector<shim::FfStageCodes> stages = stages_of(setup_row(row.program.setup));
        Draw on, off, tested_on, tested_off;
        harness.draw_ground(stages, {}, on);
        harness.draw_ground(stages, {{"fx_ff_specialised", 0}}, off);
        harness.alpha_ref = median_alpha(on);
        harness.draw_ground(stages, {{"fx_alpha_to_coverage", 0}}, tested_on);
        harness.draw_ground(stages, {{"fx_alpha_to_coverage", 0}, {"fx_ff_specialised", 0}}, tested_off);
        harness.alpha_ref = 0;
        int differ = 0, tested_differ = 0;
        const std::string text = compare(on, off, differ), tested = compare(tested_on, tested_off, tested_differ);
        INFO(text);
        INFO("alpha-tested: " << tested);
        CHECK(differ == 0);
        CHECK(tested_differ == 0);
        CHECK(on.specialised == 1);
        CHECK(off.specialised == 0);
        CHECK(tested_on.specialised == 1);
        CHECK(tested_off.specialised == 0);
        ++ground;
    }
    CHECK(ground > 0);

    // The outdoor lamp family's programs with the sun shadow variant, relief off, for a baked floor of their setup.
    size_t outdoor = 0;
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
    {
        const effects::FfFamilyRow &family = effects::kFfFamilyRows[size_t(row.program.family)];
        if (row.program.entry != effects::FfEntry::Family || family.room || family.lit || !family.blocks.lamps ||
            row.program.relief != 0 || row.program.specular != 0 || row.program.shadow != 1)
            continue;
        INFO(row.name);
        const std::vector<shim::FfStageCodes> stages = stages_of(setup_row(row.program.setup));
        Draw on, off, tested_on, tested_off;
        harness.draw_room(stages, {{"fx_lamp_shadows", 8}}, on, false);
        harness.draw_room(stages, {{"fx_lamp_shadows", 8}, {"fx_ff_specialised", 0}}, off, false);
        harness.alpha_ref = median_alpha(on);
        harness.draw_room(stages, {{"fx_lamp_shadows", 8}, {"fx_alpha_to_coverage", 0}}, tested_on, false);
        harness.draw_room(stages, {{"fx_lamp_shadows", 8}, {"fx_alpha_to_coverage", 0}, {"fx_ff_specialised", 0}},
                          tested_off, false);
        harness.alpha_ref = 0;
        int differ = 0, tested_differ = 0;
        const std::string text = compare(on, off, differ), tested = compare(tested_on, tested_off, tested_differ);
        INFO(text);
        INFO("alpha-tested: " << tested);
        CHECK(differ == 0);
        CHECK(tested_differ == 0);
        CHECK(on.specialised == 1);
        CHECK(off.specialised == 0);
        CHECK(tested_on.specialised == 1);
        const auto discarded = std::count(std::begin(tested_on.pixels), std::end(tested_on.pixels), 0xFF000000u);
        CHECK(discarded > 0);
        CHECK(discarded < static_cast<long>(kSize * kSize));
        ++outdoor;
    }
    CHECK(outdoor > 0);

    // A setup that differs from a listed one only in its alpha row, its result argument or by a stage more.
    REQUIRE_FALSE(effects::kFfSetupRows.empty());
    for (const effects::FfSetupRow &setup : effects::kFfSetupRows)
    {
        std::vector<shim::FfStageCodes> alpha = stages_of(setup), temp = stages_of(setup), longer = stages_of(setup);
        alpha.back().alpha_op = alpha.back().alpha_op == D3DTOP_MODULATE ? D3DTOP_SELECTARG1 : D3DTOP_MODULATE;
        temp.back().temp = !temp.back().temp;
        // And the setup with a lightmap stage after it.
        longer.push_back({.colour_op = D3DTOP_MODULATE2X,
                          .colour_arg1 = D3DTA_CURRENT,
                          .colour_arg2 = D3DTA_TEXTURE,
                          .colour_arg0 = D3DTA_CURRENT,
                          .alpha_op = D3DTOP_DISABLE,
                          .alpha_arg1 = D3DTA_CURRENT,
                          .alpha_arg2 = D3DTA_CURRENT,
                          .alpha_arg0 = D3DTA_CURRENT,
                          .bound = true});
        for (const auto &stages : {alpha, temp, longer})
        {
            Draw on;
            harness.draw(stages, true, on);
            CHECK(on.specialised == 0);
        }
    }
}

// The programs Lighting::prepare_programs creates at a settings switch against every (vertex, fragment) pair a lit,
// baked or ground draw of those settings can take: ChooseProgram and ChoosePlainProgram over every draw's facts
// within FfVariants, with the vertex shaders a draw of each kind pairs with and the opaque twins. Neither side may
// have a pair the other lacks: an unprepared one is linked in play, a prepared one that nothing draws is a link for
// nothing.
TEST_CASE("prepare_programs prepares exactly the lit, baked and ground programs the draws of its settings can take")
{
    using Pair = std::pair<uint32_t, uint32_t>;
    const uint32_t vs_lit = shim::shader_id_by_name("vs_ff_lit"), vs_skin = shim::shader_id_by_name("vs_ff_lit_skin");
    const uint32_t vs_ground[] = {shim::shader_id_by_name("vs_ff_dyn"), shim::shader_id_by_name("vs_ff_unlit_dyn")};
    const uint32_t fs_ground = shim::shader_id_by_name("fs_ff_dyn");
    // The fragment shaders compared: the families' programs, the specialised ones, the ground's, with their twins.
    std::set<uint32_t> compared{fs_ground};
    for (const effects::FfProgramRow &row : effects::kFfProgramRows)
        compared.insert(shim::shader_id_by_name(row.name));
    for (const effects::FfSpecialisedRow &row : effects::kFfSpecialisedRows)
        compared.insert(shim::shader_id_by_name(row.name));
    for (const uint32_t fs : std::set<uint32_t>(compared))
        compared.insert(shim::opaque_twin(fs));
    compared.erase(UINT32_MAX);
    const auto name_of = [](const char *name) { return shim::shader_id_by_name(name); };
    std::vector<std::optional<uint8_t>> setups{std::nullopt};
    for (const effects::FfSetupRow &row : effects::kFfSetupRows)
        setups.push_back(row.id);

    for (const int preset : {0, 1, 2})
        for (const int processing : {0, 1, 2}) // none, normal maps, normal and height maps
            for (const bool option : {true, false})
                for (const bool specular_on : {true, false})
                {
                    if (!specular_on && preset != 2)
                        continue;
                    fx::Settings settings = fx::Settings::FromPreset(static_cast<fx::QualityPreset>(preset));
                    fx::ApplyTextureProcessing(settings,
                                               {.normal_maps = processing > 0, .height_maps = processing > 1});
                    settings.ff_specialised = option;
                    if (!specular_on)
                        settings.lighting_flags &= ~fx::kLightingSpecular;
                    INFO("preset " << preset << " processing " << processing << " option " << option << " specular "
                                   << specular_on);
                    const effects::FfVariants v = effects::VariantsFor(settings);
                    std::set<Pair> expected;
                    const auto take = [&](uint32_t vs, uint32_t fs) {
                        expected.insert({vs, fs});
                        if (const uint32_t twin = shim::opaque_twin(fs); twin != UINT32_MAX)
                            expected.insert({vs, twin});
                    };
                    if (v.lighting)
                        for (const bool lit : {true, false})
                            for (int bits = 0; bits < 16; ++bits)
                                for (int specular = 0; specular <= 1; ++specular)
                                    for (int relief = 0; relief <= v.relief; ++relief)
                                        for (const bool interior : {false, true})
                                            for (const bool sun_shadow : {false, true})
                                                for (const std::optional<uint8_t> setup : setups)
                                                {
                                                    const effects::FfBlocks needs{.lamps = (bits & 1) != 0,
                                                                                  .ao = (bits & 2) != 0,
                                                                                  .town_wall = (bits & 4) != 0,
                                                                                  .wet = (bits & 8) != 0};
                                                    // What prepare() can turn on; indoors no town wall, rain or sun.
                                                    if ((needs.lamps && !v.possible.lamps) ||
                                                        (needs.ao && !v.possible.ao) ||
                                                        (needs.town_wall && (lit || !v.possible.town_wall)) ||
                                                        (needs.wet && !v.possible.wet) ||
                                                        (interior && (needs.town_wall || needs.wet || sun_shadow)) ||
                                                        (lit ? specular != v.lit_specular
                                                             : specular > v.baked_specular))
                                                        continue;
                                                    const effects::FfChoice choice =
                                                        effects::ChooseProgram({.lit = lit,
                                                                                .needs = needs,
                                                                                .specular = specular != 0,
                                                                                .relief = static_cast<uint8_t>(relief),
                                                                                .shadow = v.shadow != 0,
                                                                                .interior = interior,
                                                                                .sun_shadow = sun_shadow,
                                                                                .setup = setup,
                                                                                .specialised_on = v.specialised});
                                                    std::vector<uint32_t> fs;
                                                    for (const effects::FfProgramRow &row : effects::kFfProgramRows)
                                                        if (row.program == choice.program)
                                                            fs.push_back(name_of(row.name));
                                                    if (choice.specialised)
                                                        fs.push_back(name_of(
                                                            effects::kFfSpecialisedRows[*choice.specialised].name));
                                                    for (const uint32_t f : fs)
                                                    {
                                                        take(vs_lit, f);
                                                        if (lit && relief <= v.character_relief)
                                                            take(vs_skin, f);
                                                    }
                                                }
                    if (v.ground)
                        for (const uint32_t vs : vs_ground)
                        {
                            take(vs, fs_ground);
                            for (const std::optional<uint8_t> setup : setups)
                                if (const std::optional<size_t> row = effects::ChoosePlainProgram(
                                        {.setup = setup, .ground = true, .specialised_on = v.specialised}))
                                    take(vs, name_of(effects::kFfSpecialisedRows[*row].name));
                        }

                    Harness harness({});
                    const auto taken = [&] {
                        std::set<Pair> pairs;
                        for (const auto &[ids, handle] : harness.device().programs)
                            if (compared.contains(ids.second))
                                pairs.insert(ids);
                        return pairs;
                    };
                    REQUIRE(taken().empty());
                    int budget = std::numeric_limits<int>::max();
                    REQUIRE(effects::Host::instance().lighting().prepare_programs(harness.device(), settings, budget));
                    const std::set<Pair> prepared = taken();
                    for (const Pair &pair : expected)
                    {
                        INFO("not prepared: " << pair.first << " + " << pair.second);
                        CHECK(prepared.contains(pair));
                    }
                    for (const Pair &pair : prepared)
                    {
                        INFO("prepared for no draw: " << pair.first << " + " << pair.second);
                        CHECK(expected.contains(pair));
                    }
                }
}
