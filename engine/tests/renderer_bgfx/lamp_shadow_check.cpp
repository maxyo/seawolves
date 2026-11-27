// The lamps' shadow faces as the frame uses them (src/effects/lamp_shadows.hpp), on OpenGL ES and Vulkan in an SDL
// window (an X display, like cutout_caster_check). A 6 x 6 m floor seen from straight above, a lamp 3 m over its
// middle, a 2 x 2 m opaque plate 1 m over the floor drawn as a character (a lit draw: the dynamic faces):
//   * same frame: the plate is drawn after the floor, in one frame only. The floor behind it is in shadow in that
//     very frame: the faces that hold a caster are a frame uniform set while the plate is drawn, after the floor's
//     draw, and bgfx gives every draw of the frame its value at the end of the frame. The next frame without the
//     plate the floor is lit again;
//   * on demand: frames without a caster neither clear nor draw the characters' atlas (render::PerfCounters
//     lamp_atlas_clears, lamp_face_draws); a frame with one clears it once and draws the plate into the faces its
//     sphere reaches (not all six);
//   * reach: a plate inside the lamp's radius but beyond the reach of its light (fx::LampReach of a dim lamp) is
//     drawn into no face;
//   * a fixture of two flames (the receivers' merged path) is shadowed by the plate like a single lamp;
//   * a light that moves (a carried lantern) takes its whole shape every frame: another attenuation at the same
//     place and range changes the floor's light at once, as much as a light that had it from the start;
//     another range draws its still faces again;
//   * edge maps (fx_lamp_edges): posts and a bar of the still geometry around a low lamp shadow the floor through all
//     of its faces but the upper one. With the maps every pixel is what it is without them: under the 5x5 and the 3x3
//     filter and after a change of the filter, with a character's shadow over the still ones in the same frame, for
//     four lamps and for a carried lantern. At most two slots get their maps in a frame; a lantern gets its in the
//     first frame it stands still, not while it moves, and they follow its new place. A device whose compare between
//     two texels is not their mean (the check asks with a point-filtered sampler) gets no maps, one error line and
//     the same picture;
//   * edge maps under the sun: outdoors, the lit floor lit by the low lamp over the posts reads the sun's map in the
//     same draw (a plate drawn for the shadow volumes only casts the sun's shadow on it). That draw keeps the sun's
//     map at its unit and the plain filters: with the maps every pixel is what it is without them, the sun's shadow
//     included.
#include "device.hpp"
#include "check_backend.hpp"
#include "lamps.hpp"
#include "log_lines.hpp"
#include "effects/effects.hpp"

#include <SDL2/SDL.h>
#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/light_falloff.hpp"
#include "storm/fx/settings.hpp"
#include "environment.hpp"
#include "storm/render/effects.hpp"
#include "storm/render/perf_stats.hpp"
#include "storm/render/shadow_casters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{

constexpr uint32_t kSize = 96;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

struct Vertex
{
    float pos[3];
    float nrm[3];
    uint32_t color;
    float uv[2];
};

class Harness
{
  public:
    explicit Harness(Device &device) : d_(device)
    {
        check(d_.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
    }
    ~Harness()
    {
        readback_->Release();
        back_->Release();
    }

    // The floor from 5 m above, orthographic over 6 x 6 m; with `plate`, the plate centred over (x, z), drawn after
    // the floor as a character. `lit_floor`: the floor is a lit draw (a moving light's receiver), else baked. `sun`:
    // light 0 of the lit draws; `sun_caster`: a plate 4 m up drawn for the shadow volumes only.
    std::vector<uint8_t> render(bool plate, float x = 0.0f, float z = 0.0f, bool lit_floor = false,
                                const D3DLIGHT9 *sun = nullptr, bool sun_caster = false)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        D3DMATRIX view{};
        view.m[0][0] = 1.0f;
        view.m[1][2] = -1.0f;
        view.m[2][1] = 1.0f;
        view.m[3][2] = 5.0f;
        view.m[3][3] = 1.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = 1.0f / 3.0f;
        projection.m[2][2] = 0.1f;
        projection.m[3][3] = 1.0f;
        render::SetEffectsWorldOrigin({0.0f, 0.0f, 0.0f});
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_AMBIENT, 0xFF404040);
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        D3DMATERIAL9 material{};
        material.Diffuse = material.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        d_.SetMaterial(&material);
        if (sun)
            d_.SetLight(0, sun);
        d_.LightEnable(0, sun ? TRUE : FALSE);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        d_.SetTexture(0, nullptr);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        d_.SetRenderState(D3DRS_LIGHTING, lit_floor ? TRUE : FALSE);
        std::vector<Vertex> floor;
        const int n = 12;
        for (int zi = 0; zi < n; ++zi)
            for (int xi = 0; xi < n; ++xi)
            {
                const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
                for (const auto &c : corners)
                    floor.push_back({{-3.0f + 6.0f * (xi + c[0]) / n, 0.0f, -3.0f + 6.0f * (zi + c[1]) / n},
                                     {0.0f, 1.0f, 0.0f},
                                     0xFF909090u,
                                     {0.0f, 0.0f}});
            }
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(floor.size() / 3), floor.data(), sizeof(Vertex)),
              "draw floor");
        if (sun_caster)
        {
            const render::ShadowCasterOnly only;
            d_.SetRenderState(D3DRS_LIGHTING, TRUE);
            const Vertex quad[6] = {
                {{1.0f, 4.0f, -0.8f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{3.0f, 4.0f, -0.8f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 0.0f}},
                {{3.0f, 4.0f, 1.2f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{1.0f, 4.0f, -0.8f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{3.0f, 4.0f, 1.2f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{1.0f, 4.0f, 1.2f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 1.0f}},
            };
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Vertex)), "draw sun caster");
        }
        if (plate)
        {
            d_.SetRenderState(D3DRS_LIGHTING, TRUE);
            render::SetEffectsTechnique("animation");
            const Vertex quad[6] = {
                {{x - 1.0f, 1.0f, z - 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{x + 1.0f, 1.0f, z - 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 0.0f}},
                {{x + 1.0f, 1.0f, z + 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{x - 1.0f, 1.0f, z - 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 0.0f}},
                {{x + 1.0f, 1.0f, z + 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {1.0f, 1.0f}},
                {{x - 1.0f, 1.0f, z + 1.0f}, {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu, {0.0f, 1.0f}},
            };
            check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Vertex)), "draw plate");
            render::SetEffectsTechnique(nullptr);
        }
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    Device &d_;
    storm::shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

// Mean green of a few pixels around the floor point (x, z).
float floor_at(const std::vector<uint8_t> &p, float x, float z)
{
    const int cx = static_cast<int>((x + 3.0f) / 6.0f * kSize), cy = static_cast<int>((3.0f - z) / 6.0f * kSize);
    float sum = 0.0f;
    int count = 0;
    for (int y = cy - 1; y <= cy + 1; ++y)
        for (int xx = cx - 1; xx <= cx + 1; ++xx)
        {
            sum += p[(static_cast<size_t>(y) * kSize + xx) * 4 + 1];
            ++count;
        }
    return sum / static_cast<float>(count);
}

// Pixels whose colour differs.
size_t differing(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    size_t count = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        count += std::memcmp(&a[i], &b[i], 3) != 0;
    return count;
}

bool ok = true;
void expect(bool pass, const char *what, const char *detail)
{
    std::printf("%s %s: %s\n", pass ? "PASS" : "FAIL", what, detail);
    ok &= pass;
}

// A lamp slot fades in and out over kFadeSeconds of the steady clock, not over frames: how many frames that takes
// depends on the machine's load, so the checks wait for the fade's end, never for a frame count.
bool lamps_fading()
{
    return effects::Host::instance().lighting().lamp_shadows().fading();
}

// Frames until the preset is active, the lamps' still faces are drawn and their fades are over (render(false) each
// frame); `lamps` reports this frame's lights first.
template <typename Lights> void settle(Harness &harness, Lights lamps, bool lit_floor = false)
{
    for (int frame = 0, active = 0;; ++frame) // on the preparation's progress, no frame budget
    {
        lamps();
        harness.render(false, 0.0f, 0.0f, lit_floor);
        active = render::CurrentEffects().pending || lamps_fading() ? 0 : active + 1;
        if (active > 20 && frame > 40)
            break;
        SDL_Delay(5);
    }
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "FAIL: SDL video: %s (an X display is needed)\n", SDL_GetError());
        return 1;
    }
    host_check::UseBackendFromEnvironment();
    SDL_Window *window = SDL_CreateWindow("lamp_shadow_check", 0, 0, kSize, kSize, 0);
    if (!window || !shim::Context::instance().init(window, kSize, kSize, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        auto *device = new Device(window, params);
        {
            Harness harness(*device);
            const std::vector<glm::vec3> positions = {
                {-3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, 3.0f}, {-3.0f, 0.0f, 3.0f}};
            const std::vector<std::array<float, 2>> uvs(positions.size(), {0.0f, 0.0f});
            const std::vector<uint32_t> colors(positions.size(), 0xFF909090u);
            const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
            render::SetEffectsLocationGeometry({"lamp_shadow_check",
                                                positions,
                                                colors,
                                                uvs,
                                                indices,
                                                {},
                                                true,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            render::ConfigureEffects(2, {{"fx_ao_volume", 0}, {"fx_lamp_shadows", 8}, {"fx_shadows", 0}});
            const render::EffectsPointLight lamp{{0.0f, 3.0f, 0.0f}, {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
            const auto bright = [&] { host_check::SetLamps(std::span(&lamp, 1)); };
            settle(harness, bright);
            char detail[256];

            // Same frame, on demand.
            render::PerfCounters start = render::PerfTotals();
            bright();
            const std::vector<uint8_t> empty = harness.render(false);
            render::PerfCounters d = render::PerfTotals().Since(start);
            std::snprintf(detail, sizeof(detail), "atlas clears %llu, face draws %llu",
                          static_cast<unsigned long long>(d.lamp_atlas_clears),
                          static_cast<unsigned long long>(d.lamp_face_draws));
            expect(d.lamp_atlas_clears == 0 && d.lamp_face_draws == 0, "a frame without casters draws no lamp face",
                   detail);
            start = render::PerfTotals();
            bright();
            const std::vector<uint8_t> shaded = harness.render(true);
            d = render::PerfTotals().Since(start);
            std::snprintf(detail, sizeof(detail), "atlas clears %llu, face draws %llu",
                          static_cast<unsigned long long>(d.lamp_atlas_clears),
                          static_cast<unsigned long long>(d.lamp_face_draws));
            expect(d.lamp_atlas_clears == 1 && d.lamp_face_draws >= 1 && d.lamp_face_draws < 6,
                   "a frame with one caster clears the characters' atlas once and draws only the faces it reaches",
                   detail);
            const float before = floor_at(empty, -1.125f, -1.125f), during = floor_at(shaded, -1.125f, -1.125f),
                        open = floor_at(shaded, 2.4f, 2.4f);
            bright();
            const std::vector<uint8_t> after = harness.render(false);
            const float later = floor_at(after, -1.125f, -1.125f);
            std::snprintf(detail, sizeof(detail),
                          "floor behind the plate %.1f before, %.1f in its frame, %.1f after; open %.1f", before,
                          during, later, open);
            expect(during < 0.85f * before && later > 0.95f * before && during < 0.85f * open,
                   "a caster drawn after its receiver shadows it in the same frame, and only then", detail);

            // Reach: a dim lamp lights its whole radius, but its light counts only near it. Its colour and
            // attenuation are chosen so that the plate 2 m under it (its sphere 1.41 m) is within its reach and the
            // plate 4.06 m from it is not (fx::LampReach with the flicker's headroom, at the preset's
            // Settings::lamp_reach_light for lamp_gain 1).
            render::EffectsPointLight dim{{0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
            fx::LightShape dim_shape;
            float dim_reach = 0.0f;
            bool found = false;
            for (const float att2 : {0.02f, 0.05f, 0.1f, 0.3f})
                for (const float c : {0.05f, 0.02f, 0.01f, 0.005f, 0.003f, 0.002f})
                {
                    if (found)
                        break;
                    dim_shape = fx::ShapeOfGameLight(dim.range, dim.att0, dim.att1, att2, fx::LightFalloff{});
                    const float h = c * fx::kLampReachHeadroom;
                    dim_reach =
                        fx::LampReach({dim.position, {h, h, h}, dim_shape.range, dim_shape.intensity, dim_shape.core},
                                      fx::Settings::FromPreset(fx::QualityPreset::High).lamp_reach_light);
                    dim.att2 = att2;
                    dim.color = {c, c, c};
                    found = dim_reach > 0.8f && dim_reach < 2.6f && dim_shape.range > 5.5f;
                }
            std::snprintf(detail, sizeof(detail), "reach %.2f m of radius %.2f m", dim_reach, dim_shape.range);
            expect(found, "a dim lamp's light counts only near it, well inside its radius", detail);
            const auto dimmed = [&] { host_check::SetLamps(std::span(&dim, 1)); };
            settle(harness, dimmed);
            start = render::PerfTotals();
            dimmed();
            harness.render(true, 2.5f, 2.5f); // its centre 4.06 m from the lamp
            const uint64_t far_draws = render::PerfTotals().Since(start).lamp_face_draws;
            start = render::PerfTotals();
            dimmed();
            harness.render(true, 0.0f, 0.0f); // 2 m under it
            const uint64_t near_draws = render::PerfTotals().Since(start).lamp_face_draws;
            std::snprintf(detail, sizeof(detail), "face draws %llu far, %llu near",
                          static_cast<unsigned long long>(far_draws), static_cast<unsigned long long>(near_draws));
            expect(far_draws == 0 && near_draws > 0,
                   "a caster beyond the reach of a lamp's light casts into none of its faces", detail);

            // A fixture of two flames (one group: the merged path of the receivers) shadows like a single lamp.
            const render::EffectsPointLight flames[2] = {
                {{-0.1f, 3.0f, 0.0f}, {2.0f, 1.8f, 1.5f}, 15.0f, 0.5f, 0.0f, 0.1f},
                {{0.1f, 3.0f, 0.0f}, {2.0f, 1.8f, 1.5f}, 15.0f, 0.5f, 0.0f, 0.1f}};
            const auto fixture = [&] { host_check::SetFixture(flames); };
            settle(harness, fixture);
            fixture();
            const float fixture_before = floor_at(harness.render(false), -1.125f, -1.125f);
            fixture();
            const std::vector<uint8_t> fixture_shaded = harness.render(true);
            const float fixture_during = floor_at(fixture_shaded, -1.125f, -1.125f),
                        fixture_open = floor_at(fixture_shaded, 2.4f, 2.4f);
            std::snprintf(detail, sizeof(detail), "floor behind the plate %.1f before, %.1f with it; open %.1f",
                          fixture_before, fixture_during, fixture_open);
            expect(fixture_before > 30.0f && fixture_during < 0.85f * fixture_before &&
                       fixture_during < 0.85f * fixture_open,
                   "a merged fixture's light is shadowed by a caster", detail);
            host_check::SetLamps({});

            // A light that moves takes its whole shape every frame.
            host_check::SetLamps({});
            render::EffectsPointLight lantern{{0.0f, 3.0f, 0.0f}, {1.5f, 1.4f, 1.2f}, 12.0f, 0.5f, 0.0f, 0.05f};
            const auto carry = [&] { render::SetEffectsDynamicLights(std::span(&lantern, 1)); };
            settle(harness, carry, true);
            carry();
            const float soft = floor_at(harness.render(false, 0.0f, 0.0f, true), 2.4f, 2.4f);
            lantern.att2 = 0.6f; // the same place and range, a steeper falloff: less light at the floor's corner
            for (int i = 0; i < 3; ++i)
            {
                carry();
                harness.render(false, 0.0f, 0.0f, true);
            }
            carry();
            const float steep = floor_at(harness.render(false, 0.0f, 0.0f, true), 2.4f, 2.4f);
            // The same light from the start.
            render::SetEffectsDynamicLights({});
            do // the slot fades out and is freed
            {
                harness.render(false, 0.0f, 0.0f, true);
                SDL_Delay(5);
            } while (lamps_fading());
            settle(harness, carry, true);
            carry();
            const float fresh = floor_at(harness.render(false, 0.0f, 0.0f, true), 2.4f, 2.4f);
            std::snprintf(detail, sizeof(detail),
                          "floor corner %.1f, then %.1f with the steeper falloff, %.1f from the start", soft, steep,
                          fresh);
            expect(steep < soft - 3.0f && std::fabs(steep - fresh) <= 2.0f,
                   "a moving light's intensity and core follow the light while it keeps its slot", detail);
            // Its still faces end at its radius (2 m: shorter than the game's light reaches, so the shape's radius
            // follows it). The lit floor itself casts into the lantern's dynamic faces: one clear every frame, a
            // second one when the still row is drawn again.
            const auto clears = [&] {
                const render::PerfCounters at = render::PerfTotals();
                carry();
                harness.render(false, 0.0f, 0.0f, true);
                return render::PerfTotals().Since(at).lamp_atlas_clears;
            };
            const uint64_t same = clears();
            lantern.range = 2.0f;
            const uint64_t changed = clears(), after_change = clears();
            std::snprintf(detail, sizeof(detail), "row clears %llu, %llu at the new radius, %llu after",
                          static_cast<unsigned long long>(same), static_cast<unsigned long long>(changed),
                          static_cast<unsigned long long>(after_change));
            expect(same == 1 && changed == 2 && after_change == 1,
                   "a moving light's new radius draws its still faces again (once)", detail);

            render::SetEffectsDynamicLights({});
            do
            {
                harness.render(false, 0.0f, 0.0f, true);
                SDL_Delay(5);
            } while (lamps_fading());

            // Edge maps. The floor with eight posts (two crossed quads each) and a bar over it.
            std::vector<glm::vec3> still = positions;
            std::vector<uint32_t> still_indices = indices;
            const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
                const auto first = static_cast<uint32_t>(still.size());
                still.insert(still.end(), {a, b, c, d});
                still_indices.insert(still_indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
            };
            for (int i = 0; i < 8; ++i)
            {
                const float angle = 0.4f + 0.785f * static_cast<float>(i), radius = i % 2 ? 1.9f : 0.9f;
                const float x = 0.3f + radius * std::cos(angle), z = 0.2f + radius * std::sin(angle);
                quad({x - 0.06f, 0.0f, z}, {x + 0.06f, 0.0f, z}, {x + 0.06f, 1.0f, z}, {x - 0.06f, 1.0f, z});
                quad({x, 0.0f, z - 0.06f}, {x, 0.0f, z + 0.06f}, {x, 1.0f, z + 0.06f}, {x, 1.0f, z - 0.06f});
            }
            quad({-2.5f, 0.6f, -1.6f}, {2.5f, 0.6f, -1.3f}, {2.5f, 0.6f, -1.2f}, {-2.5f, 0.6f, -1.5f});
            const std::vector<std::array<float, 2>> still_uvs(still.size(), {0.0f, 0.0f});
            const std::vector<uint32_t> still_colors(still.size(), 0xFF909090u);
            render::SetEffectsLocationGeometry({"lamp_shadow_check posts",
                                                still,
                                                still_colors,
                                                still_uvs,
                                                still_indices,
                                                {},
                                                true,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            const render::EffectsPointLight low{{0.3f, 1.2f, 0.2f}, {4.0f, 3.6f, 3.0f}, 15.0f, 0.5f, 0.0f, 0.1f};
            const auto low_lamp = [&] { host_check::SetLamps(std::span(&low, 1)); };
            const auto configure = [](int edges, int soft) {
                render::ConfigureEffects(2, {{"fx_ao_volume", 0},
                                             {"fx_lamp_shadows", 8},
                                             {"fx_shadows", 0},
                                             {"fx_lamp_soft", soft},
                                             {"fx_lamp_edges", edges}});
            };
            const auto floor_under = [&](int edges, int soft) {
                configure(edges, soft);
                settle(harness, low_lamp);
                low_lamp();
                return harness.render(false);
            };
            // (wide_after_narrow: the maps of the 3x3 filter are drawn again for the 5x5.) Then the plate, a
            // character, over a part of the posts in the frame that is read.
            const auto plated = [&](int edges) {
                configure(edges, 1);
                settle(harness, low_lamp);
                low_lamp();
                return harness.render(true, 1.6f, 1.2f);
            };
            const std::vector<uint8_t> wide = floor_under(0, 1), wide_mapped = floor_under(1, 1),
                                       narrow = floor_under(0, 0), narrow_mapped = floor_under(1, 0),
                                       wide_after_narrow = floor_under(1, 1), plate_mapped = plated(1),
                                       plate = plated(0);
            render::SetEffectsLocationGeometry({"lamp_shadow_check",
                                                positions,
                                                colors,
                                                uvs,
                                                indices,
                                                {},
                                                true,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            const std::vector<uint8_t> bare = floor_under(0, 1);
            std::snprintf(detail, sizeof(detail),
                          "%zu pixels shadowed by the posts, %zu at edges (5x5 against 3x3); with the maps %zu differ "
                          "under 5x5, %zu under 3x3, %zu under 5x5 after 3x3",
                          differing(wide, bare), differing(wide, narrow), differing(wide, wide_mapped),
                          differing(narrow, narrow_mapped), differing(wide, wide_after_narrow));
            expect(differing(wide, bare) > 300 && differing(wide, narrow) > 100 && differing(wide, wide_mapped) == 0 &&
                       differing(narrow, narrow_mapped) == 0 && differing(wide, wide_after_narrow) == 0,
                   "the edge maps leave every pixel of the still shadows as it is", detail);
            std::snprintf(detail, sizeof(detail), "the plate changes %zu pixels; with the maps %zu differ",
                          differing(wide, plate), differing(plate, plate_mapped));
            expect(differing(wide, plate) > 300 && differing(plate, plate_mapped) == 0,
                   "the edge maps leave a character's shadow over the still ones as it is", detail);

            // Four lamps over the posts: their maps come two slots a frame.
            render::SetEffectsLocationGeometry({"lamp_shadow_check posts",
                                                still,
                                                still_colors,
                                                still_uvs,
                                                still_indices,
                                                {},
                                                true,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            std::vector<render::EffectsPointLight> four;
            for (int i = 0; i < 4; ++i)
                four.push_back(
                    {{i % 2 ? 1.5f : -1.5f, 1.2f, i / 2 ? 1.4f : -1.4f}, {2.0f, 1.8f, 1.5f}, 15.0f, 0.5f, 0.0f, 0.1f});
            const auto four_lamps = [&] { host_check::SetLamps(four); };
            // Frames as settle() runs them; the most maps drawn in one of them and all of them.
            const auto settle_counting = [&](uint64_t &most, uint64_t &all) {
                most = all = 0;
                for (int frame = 0, active = 0; active <= 20 || frame <= 40; ++frame)
                {
                    const render::PerfCounters at = render::PerfTotals();
                    four_lamps();
                    harness.render(false);
                    const uint64_t maps = render::PerfTotals().Since(at).lamp_map_bakes;
                    most = std::max(most, maps);
                    all += maps;
                    active = render::CurrentEffects().pending || lamps_fading() ? 0 : active + 1;
                    SDL_Delay(5);
                }
                four_lamps();
                return harness.render(false);
            };
            uint64_t most = 0, all = 0;
            configure(0, 1);
            const std::vector<uint8_t> four_plain = settle_counting(most, all);
            const uint64_t plain_maps = all;
            configure(1, 1);
            const std::vector<uint8_t> four_mapped = settle_counting(most, all);
            std::snprintf(detail, sizeof(detail),
                          "%llu maps without the option; with it %llu, at most %llu in a frame; %zu pixels differ",
                          static_cast<unsigned long long>(plain_maps), static_cast<unsigned long long>(all),
                          static_cast<unsigned long long>(most), differing(four_plain, four_mapped));
            expect(plain_maps == 0 && all == 4 && most <= 2 && differing(four_plain, four_mapped) == 0,
                   "four lamps get their edge maps at most two a frame", detail);

            // A device whose compare is not filtered linearly.
            int errors = 0;
            host_check::CollectLog([&](spdlog::level::level_enum level, std::string_view text) {
                if (level >= spdlog::level::err && text.find("not their mean") != std::string_view::npos)
                    ++errors;
            });
            auto &lamp_shadows = effects::Host::instance().lighting().lamp_shadows();
            lamp_shadows.ask_pair_read(BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP |
                                       BGFX_SAMPLER_V_CLAMP);
            const std::vector<uint8_t> four_point = settle_counting(most, all);
            host_check::StopCollectingLog();
            const uint64_t point_maps = all;
            lamp_shadows.ask_pair_read(UINT32_MAX);
            settle_counting(most, all);
            std::snprintf(detail, sizeof(detail),
                          "asked with a point filter: %llu maps, %d error lines, %zu pixels differ; asked again as "
                          "the device is: %llu maps",
                          static_cast<unsigned long long>(point_maps), errors, differing(four_plain, four_point),
                          static_cast<unsigned long long>(all));
            expect(point_maps == 0 && errors == 1 && differing(four_plain, four_point) == 0 && all == 4,
                   "a device whose depth compare is not filtered gets no edge maps, one error and the same picture",
                   detail);

            // A carried lantern over the posts: lit floor.
            host_check::SetLamps({});
            render::SetEffectsLocationGeometry({"lamp_shadow_check posts",
                                                still,
                                                still_colors,
                                                still_uvs,
                                                still_indices,
                                                {},
                                                true,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            render::EffectsPointLight carried{{0.3f, 1.2f, 0.2f}, {1.5f, 1.4f, 1.2f}, 12.0f, 0.5f, 0.0f, 0.05f};
            const auto hold = [&] { render::SetEffectsDynamicLights(std::span(&carried, 1)); };
            const auto frame = [&](std::vector<uint8_t> *picture = nullptr) {
                const render::PerfCounters at = render::PerfTotals();
                hold();
                std::vector<uint8_t> pixels = harness.render(false, 0.0f, 0.0f, true);
                if (picture)
                    *picture = std::move(pixels);
                return render::PerfTotals().Since(at).lamp_map_bakes;
            };
            configure(0, 1);
            settle(harness, hold, true);
            std::vector<uint8_t> here, there, here_mapped, there_mapped;
            frame(&here);
            carried.position[0] += 0.5f;
            settle(harness, hold, true);
            frame(&there);
            carried.position[0] -= 0.5f;
            configure(1, 1);
            settle(harness, hold, true);
            frame(&here_mapped);
            uint64_t walking = 0;
            for (int step = 1; step <= 5; ++step)
            {
                carried.position[0] += 0.1f;
                walking += frame();
            }
            const uint64_t stopped = frame(&there_mapped), standing = frame();
            std::snprintf(detail, sizeof(detail),
                          "maps drawn: %llu in five walking frames, %llu in the first standing one, %llu after; %zu "
                          "pixels differ where it stood, %zu where it stopped (the two places differ in %zu)",
                          static_cast<unsigned long long>(walking), static_cast<unsigned long long>(stopped),
                          static_cast<unsigned long long>(standing), differing(here, here_mapped),
                          differing(there, there_mapped), differing(here, there));
            expect(walking == 0 && stopped == 1 && standing == 0 && differing(here, there) > 300 &&
                       differing(here, here_mapped) == 0 && differing(there, there_mapped) == 0,
                   "a carried lantern gets its edge maps when it stands still, and they are of where it stands",
                   detail);

            // Under the sun: the posts outdoors, the sun from (1, 2, 0) (its shadows fall 0.5 m towards -x per metre
            // of height: the plate 4 m up shades x -1..1, z -0.8..1.2 around the low lamp). The option goes off
            // before the place changes: a switch in progress would still draw the maps of its lamp.
            host_check::SetLamps({});
            configure(0, 1);
            do
            {
                harness.render(false);
                SDL_Delay(5);
            } while (render::CurrentEffects().pending);
            render::SetEffectsLocationGeometry({"lamp_shadow_check outdoors",
                                                still,
                                                still_colors,
                                                still_uvs,
                                                still_indices,
                                                {},
                                                false,
                                                {-3.0f, -0.2f, -3.0f},
                                                {3.0f, 3.5f, 3.0f}});
            D3DLIGHT9 sun{};
            sun.Type = D3DLIGHT_DIRECTIONAL;
            sun.Diffuse = {0.5f, 0.5f, 0.5f, 1.0f};
            sun.Direction = {-1.0f, -2.0f, 0.0f};
            const environment::State weather = SunlitEnvironment({0.4472f, 0.8944f, 0.0f});
            const auto sunlit = [&] {
                render::SetSceneEnvironment(weather);
                low_lamp();
            };
            struct Sunlit
            {
                std::vector<uint8_t> shaded, open;
                uint64_t maps = 0;
                effects::Lighting::Counts counts;
            };
            const auto under_sun = [&](int edges) {
                render::ConfigureEffects(2, {{"fx_ao_volume", 0},
                                             {"fx_lamp_shadows", 8},
                                             {"fx_shadows", 2},
                                             {"fx_lamp_soft", 1},
                                             {"fx_lamp_edges", edges}});
                Sunlit result;
                const render::PerfCounters at = render::PerfTotals();
                for (int frame = 0, active = 0; active <= 20 || frame <= 40; ++frame) // as settle()
                {
                    sunlit();
                    harness.render(false, 0.0f, 0.0f, true, &sun, true);
                    active = render::CurrentEffects().pending || lamps_fading() ? 0 : active + 1;
                    SDL_Delay(5);
                }
                sunlit();
                result.shaded = harness.render(false, 0.0f, 0.0f, true, &sun, true);
                result.counts = effects::Host::instance().lighting().last_frame();
                result.maps = render::PerfTotals().Since(at).lamp_map_bakes;
                sunlit();
                result.open = harness.render(false, 0.0f, 0.0f, true, &sun, false);
                return result;
            };
            const Sunlit sun_plain = under_sun(0), sun_mapped = under_sun(1);
            std::snprintf(detail, sizeof(detail),
                          "lamp-lit draws %u/%u, sun-shadowed %u/%u, maps %llu/%llu (off/on); the sun's caster changes "
                          "%zu/%zu pixels; with the maps %zu differ with it, %zu without",
                          sun_plain.counts.lamp_lit, sun_mapped.counts.lamp_lit, sun_plain.counts.shadowed,
                          sun_mapped.counts.shadowed, static_cast<unsigned long long>(sun_plain.maps),
                          static_cast<unsigned long long>(sun_mapped.maps), differing(sun_plain.shaded, sun_plain.open),
                          differing(sun_mapped.shaded, sun_mapped.open), differing(sun_plain.shaded, sun_mapped.shaded),
                          differing(sun_plain.open, sun_mapped.open));
            expect(sun_plain.counts.lamp_lit > 0 && sun_mapped.counts.lamp_lit > 0 && sun_plain.counts.shadowed > 0 &&
                       sun_mapped.counts.shadowed > 0 && sun_plain.maps == 0 && sun_mapped.maps > 0 &&
                       differing(sun_plain.shaded, sun_plain.open) > 300 &&
                       differing(sun_mapped.shaded, sun_mapped.open) > 300 &&
                       differing(sun_plain.shaded, sun_mapped.shaded) == 0 &&
                       differing(sun_plain.open, sun_mapped.open) == 0,
                   "a lamp-lit draw that reads the sun's map keeps it: the edge maps leave the sun's shadow as it is",
                   detail);

            render::SetEffectsDynamicLights({});
            host_check::SetLamps({});
            render::SetEffectsLocationGeometry({});
            render::ConfigureEffects(0, {});
            harness.render(false);
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(
        ok ? "PASS: lamp faces: same-frame caster faces, atlas on demand, reach, moving light shape, edge maps\n"
           : "FAIL: lamp faces\n");
    return ok ? 0 : 1;
}
