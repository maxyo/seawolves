// Detailed diagnostics through the device (bgfx Noop backend): textured scene draws of named game textures run the
// parallax census (Lighting::census, counted in the frame's parallax_miss) and log its lines only while
// storm::render::DetailedDiagnostics is on.
#include "device.hpp"
#include "log_lines.hpp"
#include "shim.hpp"
#include "effects/effects.hpp"
#include "storm/render/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace storm;

namespace
{
constexpr uint32_t kTextures = 50, kDraws = 200, kSize = 64;

void require(bool good, const char *what)
{
    if (!good)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
    std::printf("PASS: %s\n", what);
}

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
    float x, y, z, u, v;
};

std::vector<shim::Texture2D *> make_textures(shim::Device &device)
{
    std::vector<shim::Texture2D *> textures;
    for (uint32_t i = 0; i < kTextures; ++i)
    {
        shim::Texture2D *texture = nullptr;
        check(device.CreateTexture(kSize, kSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr),
              "CreateTexture");
        D3DLOCKED_RECT rect;
        check(texture->LockRect(0, &rect, nullptr, 0), "LockRect");
        for (uint32_t y = 0; y < kSize; ++y)
            std::fill_n(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch, kSize * 4, static_cast<uint8_t>(i + y));
        texture->UnlockRect(0);
        shim::store_of(texture)->name = "textures/town/wall" + std::to_string(i) + ".tga.tx"; // as TextureLoad names it
        textures.push_back(texture);
    }
    return textures;
}

// One frame of kDraws perspective draws over the textures.
void frame(shim::Device &device, const std::vector<shim::Texture2D *> &textures)
{
    check(device.BeginScene(), "BeginScene");
    check(device.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102030, 1.0f, 0), "Clear");
    D3DMATRIX identity{}, proj{};
    identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
    proj.m[0][0] = 1.0f, proj.m[1][1] = 1.777f, proj.m[2][2] = 1.0001f, proj.m[2][3] = 1.0f, proj.m[3][2] = -0.1f;
    device.SetTransform(D3DTS_WORLD, &identity);
    device.SetTransform(D3DTS_VIEW, &identity);
    device.SetTransform(D3DTS_PROJECTION, &proj);
    device.SetRenderState(D3DRS_LIGHTING, FALSE);
    device.SetRenderState(D3DRS_ZENABLE, TRUE);
    device.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device.SetVertexShader(nullptr);
    device.SetPixelShader(nullptr);
    device.SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
    const Vertex quad[4] = {{-1, -1, 5, 0, 1}, {1, -1, 5, 1, 1}, {-1, 1, 5, 0, 0}, {1, 1, 5, 1, 0}};
    for (uint32_t i = 0; i < kDraws; ++i)
    {
        device.SetTexture(0, textures[i % kTextures]);
        check(device.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "draw");
    }
    device.SetTexture(0, nullptr);
    check(device.EndScene(), "EndScene");
    check(device.Present(), "Present");
}

std::vector<std::string> lines;
size_t census_lines()
{
    return std::count_if(lines.begin(), lines.end(),
                         [](const std::string &line) { return line.rfind("effects parallax", 0) == 0; });
}

// Draws the census took in each of `frames` frames, the fewest and the most.
struct Census
{
    uint32_t least = UINT32_MAX, most = 0;
};
Census run(shim::Device &device, const std::vector<shim::Texture2D *> &textures, uint32_t frames)
{
    Census census;
    for (uint32_t i = 0; i < frames; ++i)
    {
        frame(device, textures);
        uint32_t draws = 0;
        for (const uint32_t n : effects::Host::instance().lighting().last_frame().parallax_miss)
            draws += n;
        census.least = std::min(census.least, draws);
        census.most = std::max(census.most, draws);
    }
    return census;
}
} // namespace

int main()
{
    if (!shim::Context::instance().init_headless(1280, 720))
    {
        std::fprintf(stderr, "FAIL: headless bgfx did not start\n");
        return 1;
    }
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = 1280;
    params.BackBufferHeight = 720;
    auto *device = new shim::Device(nullptr, params);
    const std::vector<shim::Texture2D *> textures = make_textures(*device);
    storm::host_check::CollectLog([](spdlog::level::level_enum, std::string_view line) { lines.emplace_back(line); });
    render::SetDetailedDiagnostics(false);
    run(*device, textures, 30); // every texture made
    lines.clear();
    constexpr uint32_t kFrames = 600; // two census windows
    const Census off = run(*device, textures, kFrames);
    require(census_lines() == 0, "off: textured draws log no census line");
    std::printf("census draws per frame off: %u..%u\n", off.least, off.most);
    require(off.most == 0, "off: the draws do not run the census");
    render::SetDetailedDiagnostics(true);
    const Census on = run(*device, textures, kFrames);
    require(census_lines() >= kTextures, "on: every drawn texture's census line");
    std::printf("census draws per frame on: %u..%u of %u\n", on.least, on.most, kDraws);
    require(on.least == kDraws && on.most == kDraws, "on: every textured draw runs the census once");
    render::SetDetailedDiagnostics(false);
    require(run(*device, textures, 30).most == 0, "off again: the census stops");
    storm::host_check::StopCollectingLog();
    for (shim::Texture2D *texture : textures)
        texture->Release();
    device->Release();
    return 0;
}
