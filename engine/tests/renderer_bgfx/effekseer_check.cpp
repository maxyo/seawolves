// Effekseer effects through the shim (storm/render/effekseer.hpp) with the samples of the pinned efkbgfx sources
// ($STORM_EFFEKSEER_SAMPLES): drawn over the scene against its depth, textures through the texture cache, a missing
// effect refused. "[corpus]": every effect of the installed game in $STORM_GAME_DIR loads, plays and draws.
#include "device.hpp"

#include "check_backend.hpp"
#include "storm/render/effekseer.hpp"
#include "istring.hpp"
#include "storm/render/texture_cache.hpp"

#include <SDL2/SDL.h>
#include <bx/math.h>
#include <catch2/catch.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <thread>

using namespace storm;
namespace efk = storm::render::effekseer;

namespace
{
constexpr uint32_t kSize = 96;

std::filesystem::path samples()
{
    const char *dir = std::getenv("STORM_EFFEKSEER_SAMPLES");
    REQUIRE(dir);
    return dir;
}

struct Scene
{
    SDL_Window *window = nullptr;
    shim::Device *device = nullptr;
    shim::Surface *target = nullptr, *depth = nullptr, *readback = nullptr, *back = nullptr;
    float view[16], projection[16];
    std::map<std::string, int> reads; // file reads by path

    Scene()
    {
        render::TextureCacheConfig cache;
        cache.cache_directory = std::filesystem::path(std::getenv("TMPDIR")) / "texture-cache";
        render::ConfigureTextureCache(cache);
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
        host_check::UseBackendFromEnvironment();
        window = SDL_CreateWindow("effekseer_check", 0, 0, kSize, kSize, 0);
        REQUIRE(window);
        REQUIRE(shim::Context::instance().init(window, kSize, kSize, false));
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = params.BackBufferHeight = kSize;
        device = new shim::Device(window, params);
        REQUIRE(SUCCEEDED(device->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                     &target, nullptr)));
        REQUIRE(SUCCEEDED(device->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                            &depth, nullptr)));
        REQUIRE(SUCCEEDED(
            device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr)));
        REQUIRE(SUCCEEDED(device->GetRenderTarget(0, &back)));
        // The samples' camera: 20 m back and 5 m up, looking at the origin (D3D depth 0..1, left-handed).
        const bx::Vec3 eye = {0.0f, 5.0f, -20.0f}, at = {0.0f, 0.0f, 0.0f};
        bx::mtxLookAt(view, eye, at);
        bx::mtxProj(projection, 60.0f, 1.0f, 0.5f, 200.0f, false);
        efk::SetFileReader([this, root = samples()](const std::string &path) -> std::optional<std::vector<uint8_t>> {
            ++reads[path];
            std::ifstream file(root / path, std::ios::binary);
            if (!file)
                return std::nullopt;
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
        });
    }

    ~Scene()
    {
        efk::UnloadAllEffects();
        target->Release();
        depth->Release();
        readback->Release();
        back->Release();
        device->Release();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // One frame: the target cleared to black with the scene depth at `scene_depth`, the effects drawn; the pixels
    // the effects lit.
    uint32_t frame(float scene_depth)
    {
        REQUIRE(SUCCEEDED(device->BeginScene()));
        REQUIRE(SUCCEEDED(device->SetRenderTarget(0, target)));
        REQUIRE(SUCCEEDED(device->SetDepthStencilSurface(depth)));
        REQUIRE(SUCCEEDED(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, scene_depth, 0)));
        efk::Draw(view, projection);
        REQUIRE(SUCCEEDED(device->EndScene()));
        REQUIRE(SUCCEEDED(device->GetRenderTargetData(target, readback)));
        D3DLOCKED_RECT locked{};
        REQUIRE(SUCCEEDED(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
        uint32_t lit = 0;
        for (uint32_t y = 0; y < kSize; ++y)
            for (uint32_t x = 0; x < kSize; ++x)
            {
                const auto *p = static_cast<const uint8_t *>(locked.pBits) + y * locked.Pitch + 4 * x;
                lit += p[0] > 24 || p[1] > 24 || p[2] > 24;
            }
        REQUIRE(SUCCEEDED(readback->UnlockRect()));
        REQUIRE(SUCCEEDED(device->SetRenderTarget(0, back)));
        REQUIRE(SUCCEEDED(device->Present()));
        return lit;
    }

    void advance(int frames)
    {
        for (int i = 0; i < frames; ++i)
            efk::Update(1.0f / 60.0f);
    }

    // Unloaded effects let go of their textures once the manager has removed their instances.
    void unload_all()
    {
        efk::UnloadAllEffects();
        for (int i = 0; i < 60 && efk::GetStats().textures > 0; ++i)
            advance(1);
        REQUIRE(efk::GetStats().textures == 0);
        REQUIRE(efk::GetStats().texture_bytes == 0);
    }
};

Scene &scene()
{
    static Scene s;
    return s;
}
} // namespace

TEST_CASE("an effect is drawn over the scene and hidden behind nearer scene depth")
{
    Scene &s = scene();
    const efk::EffectId sword = efk::LoadEffect("sword_lightning.efkefc");
    REQUIRE(sword != 0);
    const efk::InstanceId instance = efk::Play(sword, {});
    REQUIRE(efk::Exists(instance));
    s.advance(30);
    REQUIRE(efk::GetStats().instances > 0);
    const uint32_t open = s.frame(1.0f);
    CHECK(efk::GetStats().draw_calls > 0);
    CHECK(open > 100);
    // Scene depth nearer than every particle: the depth test hides all of them.
    CHECK(s.frame(0.0f) == 0);
    efk::Stop(instance);
    s.advance(1);
    CHECK_FALSE(efk::Exists(instance));
    CHECK(s.frame(1.0f) == 0);
}

TEST_CASE("effect textures are compressed in the background and read from the cache on the next load")
{
    // An effect whose texture no other case loads: its first load decodes it.
    Scene &s = scene();
    s.unload_all();
    const efk::Stats before = efk::GetStats();
    const efk::EffectId arrow = efk::LoadEffect("Simple_Model_UV.efkefc");
    REQUIRE(arrow != 0);
    const efk::Stats loaded = efk::GetStats();
    CHECK(loaded.textures_decoded > before.textures_decoded);
    CHECK(loaded.textures_encoding > 0);
    // Paused, the arrow keeps its pose while its texture is swapped; the compressed copy has mips, the interim one
    // none, so their pixels differ a little.
    const efk::InstanceId instance = efk::Play(arrow, {});
    s.advance(30);
    efk::SetPaused(instance, true);
    const uint32_t interim = s.frame(1.0f);
    CHECK(interim > 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (efk::GetStats().textures_encoding > 0 && std::chrono::steady_clock::now() < deadline)
    {
        s.advance(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(efk::GetStats().textures_encoding == 0);
    const uint32_t compressed = s.frame(1.0f);
    CHECK(compressed * 2 >= interim);
    CHECK(compressed <= interim * 2);
    CHECK(efk::GetStats().texture_bytes < loaded.texture_bytes);
    s.unload_all();
    const efk::Stats unloaded = efk::GetStats();
    REQUIRE(efk::LoadEffect("Simple_Model_UV.efkefc") != 0);
    const efk::Stats reloaded = efk::GetStats();
    CHECK(reloaded.textures_from_cache > unloaded.textures_from_cache);
    CHECK(reloaded.textures_decoded == unloaded.textures_decoded);
    CHECK(reloaded.textures_encoding == 0);
}

TEST_CASE("an effect file that cannot be read is refused once and nothing plays")
{
    Scene &s = scene();
    const uint32_t loaded = efk::GetStats().effects;
    CHECK(efk::LoadEffect("missing.efkefc") == 0);
    CHECK(efk::LoadEffect("missing.efkefc") == 0);
    CHECK(s.reads["missing.efkefc"] == 1);
    CHECK(efk::GetStats().effects == loaded);
    CHECK(efk::Play(0, {}) == efk::kNoInstance);
}

TEST_CASE("every effect of an installed game loads, plays and draws", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    // The game's files by their path in lower case, as the file service finds them.
    std::map<std::string, fs::path> files;
    std::vector<std::string> effects;
    for (const auto &entry : fs::recursive_directory_iterator(fs::path(root) / ""))
    {
        const std::string key = storm::AsciiLower(fs::relative(entry.path(), root).generic_string());
        files[key] = entry.path();
        if (key.starts_with("resource/effects/") && key.ends_with(".efkefc"))
            effects.push_back(key);
    }
    if (effects.empty())
    {
        std::puts("NOT_RUN: this game has no effects in resource/effects");
        std::exit(77);
    }
    Scene &s = scene();
    s.unload_all();
    efk::SetFileReader([&files](const std::string &path) -> std::optional<std::vector<uint8_t>> {
        const auto found = files.find(storm::ResourceKey(path));
        if (found == files.end())
            return std::nullopt;
        std::ifstream file(found->second, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    });
    std::printf("%-40s %5s %5s %5s %5s %5s %5s %8s %6s\n", "effect", "nodes", "sound", "dist", "soft", "mat", "gpu",
                "peak", "lit");
    int32_t peak_all = 0;
    for (const std::string &path : effects)
    {
        INFO(path);
        const efk::EffectId effect = efk::LoadEffect(path);
        REQUIRE(effect != 0);
        const efk::EffectInfo info = *efk::GetEffectInfo(effect);
        CHECK(info.missing_textures == 0);
        CHECK(info.missing_models == 0);
        CHECK(info.material_nodes == 0);
        CHECK(info.gpu_particle_nodes == 0);
        efk::Play(effect, {});
        int32_t peak = 0;
        uint32_t lit = 0;
        for (int frame = 0; frame < 120; ++frame)
        {
            s.advance(1);
            peak = std::max(peak, efk::GetStats().instances);
            if (frame % 30 == 29)
                lit = std::max(lit, s.frame(1.0f));
        }
        peak_all += peak;
        std::printf("%-40s %5d %5d %5d %5d %5d %5d %8d %6u\n", path.c_str() + 17, info.nodes, info.sounds,
                    info.distortion_nodes, info.soft_particle_nodes, info.material_nodes, info.gpu_particle_nodes, peak,
                    lit);
        efk::StopAll();
        s.advance(2);
    }
    std::printf("instances of all effects at their peaks: %d\n", peak_all);
}
