// A game's own materials (render::EffectsSurfaceMaterial, shaders/fs_ff_pbr.sc) through the shim on a real GPU: a lit
// 4 x 4 m quad facing an orthographic camera, its synthetic maps uniform, the game one that ships its maps
// (render::EffectsGame), «Высокая» unless a case says otherwise. Compared by the mean of the picture's middle:
//   the material draws, and otherwise than the legacy lit program;
//   its normal map leans the light: a texel leaning towards the sun is brighter than a flat one, one leaning away
//   darker; on «Низкая» (no normal maps) the leaning map draws as the flat one;
//   a smooth material under a sun behind the camera shows a highlight a rough one has not; a metal's highlight takes
//   its colour; the rma map's roughness, metal and occlusion draw as the same constants;
//   the emission map adds its colour by its strength and the scene's emission factor, without any light;
//   the second texture coordinates' occlusion darkens by twice its texel (the modulate-2x stage), only when set;
//   the light is the scene environment's (environment::State), and a higher exposure is brighter;
//   a layered material: the mask picks the layers (b over g over r over the base), each layer tiles by its own
//   factor and leans the light by its own normal map («Средняя», «Высокая»; «Низкая» draws the layers flat), and a
//   layer whose file is missing draws the base. The layers are DXT1 files the check writes (nothing of a game);
//   a puddle mesh lies over its ground only where its bed passes the wetness's threshold: nothing of it when dry,
//   its deeper bed under water first.
#include "device.hpp"
#include "check_backend.hpp"
#include "effects/layer_arrays.hpp"
#include "storm/render/effects.hpp"

#include <storm_glm.h>

#include <SDL2/SDL.h>

#include <catch2/catch.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{
constexpr uint32_t kSize = 64;

void check(HRESULT result, const char *what)
{
    INFO(what);
    REQUIRE(SUCCEEDED(result));
}

struct Vertex
{
    float pos[3];
    float nrm[3];
    float uv0[2];
    float uv1[2];
};

struct Mean
{
    float r = 0.0f, g = 0.0f, b = 0.0f;
    float luma() const
    {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
};

class Harness
{
  public:
    Harness()
    {
        render::ConfigureEffectsGame({.generates_texture_maps = false});
        render::ConfigureEffects(2, {});
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
        host_check::UseBackendFromEnvironment();
        window_ = SDL_CreateWindow("materials_check", 0, 0, kSize, kSize, 0);
        REQUIRE(window_);
        REQUIRE(shim::Context::instance().init(window_, kSize, kSize, false));
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        device_ = new Device(window_, params);
        check(
            device_->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
            "CreateOffscreenPlainSurface");
        check(device_->GetRenderTarget(0, &back_), "GetRenderTarget");
        albedo = Texture(0xFFB4B4B4);
    }
    ~Harness()
    {
        for (shim::Texture2D *texture : made_)
            texture->Release();
        readback_->Release();
        back_->Release();
        device_->Release();
        shim::Context::instance().shutdown();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        render::ConfigureEffectsGame({});
    }

    // A 4 x 4 texture of one A8R8G8B8 texel value, released with the harness.
    shim::Texture2D *Texture(uint32_t argb, bool map = false)
    {
        shim::Texture2D *texture = nullptr;
        check(device_->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
        D3DLOCKED_RECT rect;
        check(texture->LockRect(0, &rect, nullptr, 0), "LockRect");
        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) = argb;
        texture->UnlockRect(0);
        if (map)
            render::DeclareMaterialMap(texture);
        made_.push_back(texture);
        return texture;
    }

    // Until the preset is prepared (its programs linked, its self-test run).
    void Settle(int preset)
    {
        render::ConfigureEffects(preset, {});
        for (int frame = 0;; ++frame)
        {
            Render(nullptr);
            if (!render::CurrentEffects().pending && frame > 30)
                return;
            SDL_Delay(5);
        }
    }

    shim::Texture2D *albedo = nullptr;
    shim::Texture2D *second = nullptr; // stage 1 (the second texture coordinates)
    glm::vec3 to_sun{0.5f, 0.3f, -1.0f};
    float sun = 0.8f; // the D3D light's colour
    uint32_t ambient = 0x404040;
    float exposure = 1.0f, emission = 1.0f, sun_scale = 1.0f; // the scene environment's
    float wetness = 0.0f;
    glm::vec3 normal{0.0f, 0.0f, -1.0f}; // the quad's vertices'
    uint32_t clear = 0xFF000000;
    bool blended = false;                                      // as GEOM sets a blended surface's states
    std::array<uint32_t, 2> columns{kSize / 4, 3 * kSize / 4}; // of the picture the mean takes

    // A layered material's picture once its arrays are there (asked for by the first draw, made on a worker).
    Mean RenderLayered(const render::EffectsSurfaceMaterial &material)
    {
        Render(&material);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (effects::LayerArraysPending() && std::chrono::steady_clock::now() < deadline)
        {
            SDL_Delay(2);
            Render(&material);
        }
        REQUIRE(effects::LayerArraysPending() == 0);
        return Render(&material);
    }

    // The quad with `material` (null: the legacy lit program); the mean of the middle half of the picture.
    Mean Render(const render::EffectsSurfaceMaterial *material)
    {
        // The scene environment the material draws take their light from: the same sun and ambient as the device's
        // lights, linear.
        environment::State environment;
        environment.light_dir = glm::normalize(to_sun);
        environment.sun_dir = environment.light_dir;
        environment.light_color = glm::vec3(sun * sun * sun_scale);
        environment.light_is_sun = true;
        const float fill = float(ambient & 255) / 255.0f;
        environment.ambient = glm::vec3(fill * fill);
        environment.exposure = exposure;
        environment.emission = emission;
        environment.wetness = wetness;
        render::SetSceneEnvironment(environment);
        check(device_->BeginScene(), "BeginScene");
        check(device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.0f, 0), "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = 0.5f;
        projection.m[2][2] = 0.1f;
        projection.m[3][3] = 1.0f;
        device_->SetTransform(D3DTS_WORLD, &identity);
        device_->SetTransform(D3DTS_VIEW, &identity);
        device_->SetTransform(D3DTS_PROJECTION, &projection);
        device_->SetRenderState(D3DRS_ZENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, blended);
        device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device_->SetRenderState(D3DRS_LIGHTING, TRUE);
        device_->SetRenderState(D3DRS_AMBIENT, 0xFF000000u | ambient);
        D3DMATERIAL9 surface{};
        surface.Diffuse = surface.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        device_->SetMaterial(&surface);
        D3DLIGHT9 light0{};
        light0.Type = D3DLIGHT_DIRECTIONAL;
        light0.Diffuse = {sun, sun, sun, 1.0f};
        const glm::vec3 direction = -glm::normalize(to_sun);
        light0.Direction = {direction.x, direction.y, direction.z};
        device_->SetLight(0, &light0);
        device_->LightEnable(0, sun > 0.0f);
        // As the geometry sets a model's stages: the colour 2x the light, a second texture as a 2x layer.
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device_->SetTexture(0, albedo);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, second ? D3DTOP_MODULATE2X : D3DTOP_DISABLE);
        device_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        device_->SetTexture(1, second);
        device_->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        std::vector<Vertex> quad;
        const float corners[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
        for (const auto &c : corners)
            quad.push_back({{-2.0f + 4.0f * c[0], 2.0f - 4.0f * c[1], 5.0f},
                            {normal.x, normal.y, normal.z},
                            {c[0], c[1]},
                            {c[0], c[1]}});
        device_->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2);
        render::SetEffectsSurfaceMaterial(material);
        check(device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad.data(), sizeof(Vertex)), "draw");
        render::SetEffectsSurfaceMaterial(nullptr);
        check(device_->EndScene(), "EndScene");
        check(device_->GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        Mean mean;
        uint32_t n = 0;
        for (uint32_t y = kSize / 4; y < 3 * kSize / 4; ++y)
            for (uint32_t x = columns[0]; x < columns[1]; ++x, ++n)
            {
                const uint8_t *p = static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch + x * 4; // B, G, R, A
                mean.r += p[2];
                mean.g += p[1];
                mean.b += p[0];
            }
        readback_->UnlockRect();
        check(device_->Present(), "Present");
        mean.r /= n;
        mean.g /= n;
        mean.b /= n;
        return mean;
    }

  private:
    SDL_Window *window_ = nullptr;
    Device *device_ = nullptr;
    shim::Surface *readback_ = nullptr, *back_ = nullptr;
    std::vector<shim::Texture2D *> made_;
};

Harness &harness()
{
    static Harness h;
    return h;
}

// A normal map's texel leaning by `x` (-1..1) along the texture's u, which runs along the picture's x.
uint32_t Leaning(float x)
{
    const auto r = static_cast<uint32_t>(std::lround((0.5f + 0.5f * x) * 255.0f));
    return 0xFF000000u | r << 16 | 128u << 8 | 255u;
}

// An 8 x 8 DXT1 file of solid 4 x 4 blocks (colour of block x, y as A8R8G8B8) in the check's own folder.
std::string LayerFile(const std::string &name, const std::function<uint32_t(uint32_t, uint32_t)> &block)
{
    static const std::filesystem::path folder = [] {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "storm_materials_check_layers";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }();
    const std::filesystem::path path = folder / (name + ".dds");
    uint32_t header[32] = {};
    std::memcpy(header, "DDS ", 4);
    header[1] = 124;
    header[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;
    header[3] = header[4] = 8;
    header[5] = 32;
    header[7] = 1;
    header[19] = 32;
    header[20] = 0x4;
    std::memcpy(&header[21], "DXT1", 4);
    header[27] = 0x1000;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(header), sizeof(header));
    for (uint32_t by = 0; by < 2; ++by)
        for (uint32_t bx = 0; bx < 2; ++bx)
        {
            const uint32_t c = block(bx, by);
            const uint16_t rgb565 = uint16_t(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31));
            const uint16_t ends[2] = {rgb565, rgb565};
            const uint32_t indices = 0;
            out.write(reinterpret_cast<const char *>(ends), 4);
            out.write(reinterpret_cast<const char *>(&indices), 4);
        }
    return path.string();
}

std::string SolidLayer(const std::string &name, uint32_t argb)
{
    return LayerFile(name, [argb](uint32_t, uint32_t) { return argb; });
}
} // namespace

TEST_CASE("a material draws, otherwise than the legacy lit program")
{
    Harness &h = harness();
    h.Settle(2);
    const render::EffectsSurfaceMaterial matte{};
    const Mean legacy = h.Render(nullptr);
    const Mean material = h.Render(&matte);
    INFO("legacy " << legacy.luma() << ", material " << material.luma());
    CHECK(material.luma() > 20.0f);
    CHECK(std::fabs(material.luma() - legacy.luma()) > 2.0f);
}

TEST_CASE("the normal map leans the light, on «Средняя» and «Высокая» only")
{
    Harness &h = harness();
    h.to_sun = {0.8f, 0.0f, -0.6f}; // low on the picture's right
    render::EffectsSurfaceMaterial material;
    shim::Texture2D *flat_map = h.Texture(Leaning(0.0f), true);
    shim::Texture2D *towards = h.Texture(Leaning(0.6f), true), *away = h.Texture(Leaning(-0.6f), true);
    for (const int preset : {2, 1, 0})
    {
        h.Settle(preset);
        material.normal = flat_map;
        const Mean flat = h.Render(&material);
        material.normal = towards;
        const Mean lit = h.Render(&material);
        material.normal = away;
        const Mean shaded = h.Render(&material);
        INFO("preset " << preset << ": flat " << flat.luma() << ", towards " << lit.luma() << ", away "
                       << shaded.luma());
        if (preset > 0)
        {
            CHECK(lit.luma() > flat.luma() + 8.0f);
            CHECK(shaded.luma() < flat.luma() - 8.0f);
        }
        else
        {
            // «Низкая» draws the material too, without its normal map.
            const Mean legacy = h.Render(nullptr);
            INFO("legacy " << legacy.luma());
            CHECK(std::fabs(flat.luma() - legacy.luma()) > 2.0f);
            CHECK(std::fabs(lit.luma() - flat.luma()) < 1.0f);
            CHECK(std::fabs(shaded.luma() - flat.luma()) < 1.0f);
        }
    }
    h.Settle(2);
    h.to_sun = {0.5f, 0.3f, -1.0f};
}

TEST_CASE("roughness and metal shape the highlight; the rma map is read as its constants")
{
    Harness &h = harness();
    h.Settle(2);
    h.to_sun = {0.0f, 0.0f, -1.0f}; // behind the camera: the mirror direction
    render::EffectsSurfaceMaterial smooth{.roughness = 0.25f};
    const render::EffectsSurfaceMaterial rough{.roughness = 1.0f};
    const Mean glossy = h.Render(&smooth), matte = h.Render(&rough);
    INFO("smooth " << glossy.luma() << ", rough " << matte.luma());
    CHECK(glossy.luma() > matte.luma() + 15.0f);
    // At roughness 1 the highlight is gone, as the materials were authored: a fully rough metal shows nothing.
    const render::EffectsSurfaceMaterial rough_metal{.roughness = 1.0f, .metallic = 1.0f};
    const Mean dull = h.Render(&rough_metal);
    INFO("rough metal " << dull.luma());
    CHECK(dull.luma() < 3.0f);

    // R 0.25 roughness, G 0 metal, B 1 occlusion.
    render::EffectsSurfaceMaterial mapped{.rma = h.Texture(0xFF4000FFu, true), .roughness = 0.9f, .metallic = 1.0f};
    const Mean from_map = h.Render(&mapped);
    INFO("from the rma map " << from_map.luma());
    CHECK(std::fabs(from_map.r - glossy.r) < 2.0f);
    CHECK(std::fabs(from_map.b - glossy.b) < 2.0f);

    shim::Texture2D *grey = h.albedo;
    h.albedo = h.Texture(0xFFD02020); // red
    const Mean dielectric = h.Render(&smooth);
    smooth.metallic = 1.0f;
    const Mean metal = h.Render(&smooth);
    INFO("red dielectric " << dielectric.r << "/" << dielectric.b << ", red metal " << metal.r << "/" << metal.b);
    CHECK(metal.r > 3.0f * metal.b);       // the highlight in the metal's colour
    CHECK(dielectric.b > metal.b + 10.0f); // a white highlight on the dielectric
    h.albedo = grey;
    h.to_sun = {0.5f, 0.3f, -1.0f};
}

TEST_CASE("the emission map adds its colour by its strength and the scene's factor")
{
    Harness &h = harness();
    h.Settle(2);
    const float sun = h.sun;
    const uint32_t ambient = h.ambient;
    h.sun = 0.0f;
    h.ambient = 0;
    render::EffectsSurfaceMaterial glowing{.emission = h.Texture(0xFFC00000), .emission_strength = 1.0f};
    const Mean on = h.Render(&glowing);
    h.emission = 0.0f;
    const Mean day = h.Render(&glowing);
    h.emission = 1.0f;
    glowing.emission_strength = 0.0f;
    const Mean off = h.Render(&glowing);
    INFO("glowing " << on.r << "/" << on.g << ", by day " << day.r << ", strength 0 " << off.r);
    CHECK(on.r > 60.0f);
    CHECK(on.g < 5.0f);
    CHECK(day.r < 5.0f);
    CHECK(off.r < 5.0f);
    h.sun = sun;
    h.ambient = ambient;
}

TEST_CASE("the second texture coordinates' occlusion darkens by twice its texel, when the material says so")
{
    Harness &h = harness();
    h.Settle(2);
    const render::EffectsSurfaceMaterial plain{};
    render::EffectsSurfaceMaterial occluded{.occlusion_uv1 = true};
    const Mean without = h.Render(&plain);
    h.second = h.Texture(0xFF808080); // 2 x 0.5: no occlusion
    const Mean open = h.Render(&occluded);
    h.second = h.Texture(0xFF404040); // 2 x 0.25: half the colour, a quarter of its light
    const Mean half = h.Render(&occluded);
    const Mean ignored = h.Render(&plain);
    h.second = nullptr;
    INFO("none " << without.luma() << ", texel 0.5 " << open.luma() << ", not the material's " << ignored.luma()
                 << ", texel 0.25 " << half.luma());
    CHECK(std::fabs(open.luma() - without.luma()) < 2.0f);
    CHECK(std::fabs(ignored.luma() - without.luma()) < 1.0f);
    CHECK(half.luma() < 0.75f * without.luma());
}

TEST_CASE("the light is the scene environment's; a higher exposure is brighter")
{
    Harness &h = harness();
    h.Settle(2);
    const render::EffectsSurfaceMaterial matte{};
    const Mean one = h.Render(&matte);
    h.exposure = 2.0f;
    const Mean two = h.Render(&matte);
    h.exposure = 1.0f;
    // The environment's direct light stronger, the device's light the same.
    h.sun_scale = 3.0f;
    const Mean stronger = h.Render(&matte);
    h.sun_scale = 1.0f;
    INFO("the environment's sun x3 " << stronger.luma());
    CHECK(stronger.luma() > one.luma() + 10.0f);
    INFO("exposure 1 " << one.luma() << ", 2 " << two.luma());
    CHECK(two.luma() > one.luma() + 10.0f);
}

namespace
{
constexpr uint32_t kRed = 0xFFC02020, kGreen = 0xFF20C020, kBlue = 0xFF2020C0;

// The layers of `albedo` (r, g, b; empty: none) and of `normal`, drawn by `mask` over the harness's albedo.
render::EffectsSurfaceMaterial Layered(const std::shared_ptr<render::EffectsLayerSet> &set, shim::Texture2D *mask)
{
    render::EffectsSurfaceMaterial material;
    material.layers = set.get();
    material.layer_mask = mask;
    return material;
}

std::shared_ptr<render::EffectsLayerSet> Layers(const std::array<std::string, 3> &albedo,
                                                const std::array<std::string, 3> &normal = {})
{
    return render::MakeEffectsLayerSet({albedo, std::array<std::string, 3>{}, normal}, 0);
}

bool Dominant(const Mean &m, float Mean::*channel)
{
    const float others = channel == &Mean::r   ? std::max(m.g, m.b)
                         : channel == &Mean::g ? std::max(m.r, m.b)
                                               : std::max(m.r, m.g);
    return m.*channel > others + 30.0f;
}
} // namespace

TEST_CASE("a layered material: the mask picks the layers, b over g over r over the base, on every preset")
{
    Harness &h = harness();
    h.to_sun = {0.5f, 0.3f, -1.0f};
    const auto set = Layers({SolidLayer("red", kRed), SolidLayer("green", kGreen), SolidLayer("blue", kBlue)});
    for (const int preset : {2, 0})
    {
        INFO("preset " << preset);
        h.Settle(preset);
        const render::EffectsSurfaceMaterial plain{};
        const Mean base = h.Render(&plain);
        const Mean none = h.RenderLayered(Layered(set, h.Texture(0xFF000000, true)));
        CHECK(std::fabs(none.luma() - base.luma()) < 2.0f);
        CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFFFF0000, true))), &Mean::r));
        CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFF00FF00, true))), &Mean::g));
        CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFF0000FF, true))), &Mean::b));
        CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFFFFFFFF, true))), &Mean::b));
        CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFFFFFF00, true))), &Mean::g));
    }
}

TEST_CASE("each layer tiles by its own factor")
{
    Harness &h = harness();
    h.Settle(2);
    // Black on the texture's left half, white on its right.
    const auto set = Layers({LayerFile("stripes", [](uint32_t x, uint32_t) { return x ? 0xFFFFFFFF : 0xFF000000; }),
                             std::string(), std::string()});
    render::EffectsSurfaceMaterial material = Layered(set, h.Texture(0xFFFF0000, true));
    const auto halves = [&] {
        h.columns = {4, 28};
        const Mean left = h.RenderLayered(material);
        h.columns = {36, 60};
        const Mean right = h.RenderLayered(material);
        h.columns = {kSize / 4, 3 * kSize / 4};
        return std::pair{left.luma(), right.luma()};
    };
    const auto [left1, right1] = halves();
    INFO("tiling 1: " << left1 << " | " << right1);
    CHECK(right1 > left1 + 80.0f);
    material.layer_tiling = {1.0f, 2.0f, 1.0f, 1.0f};
    const auto [left2, right2] = halves();
    INFO("tiling 2: " << left2 << " | " << right2);
    CHECK(std::fabs(right2 - left2) < 20.0f);
    CHECK(left2 > left1 + 30.0f);
}

TEST_CASE("each layer leans the light by its own normal map, on «Средняя» and «Высокая» only")
{
    Harness &h = harness();
    h.to_sun = {0.8f, 0.0f, -0.6f}; // low on the picture's right
    const std::string grey = SolidLayer("grey", 0xFFB4B4B4);
    const auto set = Layers({grey, grey, grey}, {SolidLayer("towards", Leaning(0.6f)), std::string(), std::string()});
    for (const int preset : {2, 1, 0})
    {
        INFO("preset " << preset);
        h.Settle(preset);
        render::EffectsSurfaceMaterial material = Layered(set, h.Texture(0xFF000000, true));
        material.normal = h.Texture(Leaning(0.0f), true);
        const Mean flat = h.RenderLayered(material);
        material.layer_mask = h.Texture(0xFFFF0000, true);
        const Mean leaning = h.RenderLayered(material);
        material.layer_mask = h.Texture(0xFF00FF00, true); // the g layer has no normal map: the base's
        const Mean other = h.RenderLayered(material);
        INFO("flat " << flat.luma() << ", r layer " << leaning.luma() << ", g layer " << other.luma());
        // Within the layers' DXT1 grey against the base's A8R8G8B8 one.
        CHECK(std::fabs(other.luma() - flat.luma()) < 3.0f);
        if (preset > 0)
            CHECK(leaning.luma() > flat.luma() + 8.0f);
        else
            CHECK(std::fabs(leaning.luma() - flat.luma()) < 3.0f);
    }
}

TEST_CASE("a layer whose file is missing draws the base")
{
    Harness &h = harness();
    h.to_sun = {0.5f, 0.3f, -1.0f};
    h.Settle(2);
    const std::string missing = (std::filesystem::temp_directory_path() / "storm_materials_check_none.dds").string();
    const auto set = Layers({SolidLayer("red", kRed), missing, SolidLayer("blue", kBlue)});
    const render::EffectsSurfaceMaterial plain{};
    const Mean base = h.Render(&plain);
    const Mean green = h.RenderLayered(Layered(set, h.Texture(0xFF00FF00, true)));
    INFO("base " << base.luma() << ", missing layer " << green.luma());
    CHECK(std::fabs(green.luma() - base.luma()) < 2.0f);
    CHECK(Dominant(h.RenderLayered(Layered(set, h.Texture(0xFFFF0000, true))), &Mean::r));
}

TEST_CASE("a puddle lies over its ground where its bed passes the wetness's threshold")
{
    Harness &h = harness();
    h.Settle(2);
    h.normal = {0.0f, 1.0f, 0.0f}; // flat ground, open to the sky: the place's wetness in full
    h.clear = 0xFFFFFFFF;
    h.blended = true;
    // Beds of 0.9 and 0.5 (the mask through the game's square root), the water still.
    render::EffectsSurfaceMaterial deep{.puddle_mask = h.Texture(0xFFCFCFCF, true),
                                        .puddle_normal = h.Texture(0xFF8080FF, true)};
    render::EffectsSurfaceMaterial shallow{.puddle_mask = h.Texture(0xFF404040, true),
                                           .puddle_normal = deep.puddle_normal};
    h.wetness = 0.0f;
    CHECK(h.Render(&deep).luma() > 250.0f); // dry: nothing of it
    // 0.15: the threshold is at 0.71 of the bed. The deep bed is under water, the shallow one not yet.
    h.wetness = 0.15f;
    const float under = h.Render(&deep).luma(), above = h.Render(&shallow).luma();
    CHECK(under < 150.0f);
    CHECK(above > 250.0f);
    // Soaked: the threshold is at 0.01, both are under water.
    h.wetness = 1.0f;
    CHECK(h.Render(&shallow).luma() < 150.0f);
    h.wetness = 0.0f;
    h.normal = {0.0f, 0.0f, -1.0f};
    h.clear = 0xFF000000;
    h.blended = false;
}
