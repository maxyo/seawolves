// An alpha test that cannot fail draws like an opaque draw (Device::alpha_test_discards): the location's models are
// all drawn with the game's alpha test on, most with fully opaque textures and vertex alpha. Real GPU readback: what
// can fall below the reference is still cut out (a texture's holes, the vertex alpha, the texture factor, a texture
// written again after it was loaded), and only a stage alpha of 1 everywhere leaves the test off.
#include "device.hpp"
#include "check_backend.hpp"

#include <SDL2/SDL.h>
#include "storm/render/effects.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace storm;
using shim::Device;

namespace
{
constexpr uint32_t kSize = 32;
constexpr uint32_t kRed = 0xFFFF0000, kClear = 0xFF0000FF;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

// 16x16 red; `holes`: its left half has alpha 0.
void fill(shim::Texture2D *texture, bool holes)
{
    D3DLOCKED_RECT rect;
    check(texture->LockRect(0, &rect, nullptr, 0), "LockRect(texture)");
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x)
            *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) =
                holes && x < 8 ? (kRed & 0x00FFFFFF) : kRed;
    texture->UnlockRect(0);
}

bool expect(const uint8_t *pixels, uint32_t x, uint32_t argb, const char *what)
{
    const uint8_t *p = &pixels[(kSize / 2 * kSize + x) * 4]; // B, G, R, A
    const int expected[3] = {int(argb & 0xFF), int((argb >> 8) & 0xFF), int((argb >> 16) & 0xFF)};
    for (int k = 0; k < 3; ++k)
        if (std::abs(int(p[k]) - expected[k]) > 8)
        {
            std::printf("FAIL: %s: x %u shows %02x%02x%02x, expected %06x\n", what, x, p[2], p[1], p[0],
                        argb & 0xFFFFFF);
            return false;
        }
    return true;
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
    SDL_Window *window = SDL_CreateWindow("alpha_test_check", 0, 0, kSize, kSize, 0);
    if (!window || !shim::Context::instance().init(window, kSize, kSize, false))
    {
        std::fprintf(stderr, "FAIL: no selected backend window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        auto *device = new Device(window, params);
        Device &d = *device;
        shim::Surface *target = nullptr, *readback = nullptr, *back = nullptr;
        shim::Texture2D *opaque = nullptr, *holed = nullptr, *rewritten = nullptr;
        check(d.CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr),
              "CreateRenderTarget");
        check(d.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr),
              "CreateOffscreenPlainSurface");
        check(d.GetRenderTarget(0, &back), "GetRenderTarget");
        for (shim::Texture2D **texture : {&opaque, &holed, &rewritten})
            check(d.CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, texture, nullptr), "CreateTexture");
        fill(opaque, false);
        fill(holed, true);
        fill(rewritten, false);
        for (shim::Texture2D *texture : {opaque, holed, rewritten})
            render::SetEffectsTextureSource(texture, "alpha_test_check", {}); // as the game's loader does
        fill(rewritten, true); // written again after its load: its opacity is no longer known

        struct Case
        {
            const char *name;
            shim::Texture2D *texture;
            DWORD alpha_op, arg1;
            uint32_t diffuse, factor;
            bool discards;        // the test can cut something out
            uint32_t left, right; // the expected colours of the left and the right half
        };
        const Case cases[] = {
            {"opaque texture and vertex alpha", opaque, D3DTOP_MODULATE, D3DTA_DIFFUSE, 0xFFFFFFFF, 0xFFFFFFFF, false,
             kRed, kRed},
            {"vertex alpha below the reference", opaque, D3DTOP_MODULATE, D3DTA_DIFFUSE, 0x40FFFFFF, 0xFFFFFFFF, true,
             kClear, kClear},
            {"vertex alpha above the reference", opaque, D3DTOP_MODULATE, D3DTA_DIFFUSE, 0xF0FFFFFF, 0xFFFFFFFF, true,
             kRed, kRed},
            {"texture with holes", holed, D3DTOP_MODULATE, D3DTA_DIFFUSE, 0xFFFFFFFF, 0xFFFFFFFF, true, kClear, kRed},
            {"texture written after its load", rewritten, D3DTOP_MODULATE, D3DTA_DIFFUSE, 0xFFFFFFFF, 0xFFFFFFFF, true,
             kClear, kRed},
            {"texture factor below the reference", opaque, D3DTOP_MODULATE, D3DTA_TFACTOR, 0xFFFFFFFF, 0x40FFFFFF, true,
             kClear, kClear},
            {"opaque texture factor", opaque, D3DTOP_MODULATE, D3DTA_TFACTOR, 0xFFFFFFFF, 0xFFFFFFFF, false, kRed,
             kRed},
        };
        for (const Case &test : cases)
            for (int frame = 0; frame < 3; ++frame) // the first frames may build the programs
            {
                check(d.BeginScene(), "BeginScene");
                check(d.SetRenderTarget(0, target), "SetRenderTarget");
                check(d.SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
                check(d.Clear(0, nullptr, D3DCLEAR_TARGET, kClear, 1.0f, 0), "Clear");
                D3DMATRIX identity{};
                identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
                d.SetTransform(D3DTS_WORLD, &identity);
                d.SetTransform(D3DTS_VIEW, &identity);
                d.SetTransform(D3DTS_PROJECTION, &identity);
                d.SetRenderState(D3DRS_ZENABLE, FALSE);
                d.SetRenderState(D3DRS_LIGHTING, FALSE);
                d.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                d.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                // The game's common states (Renderer::SetCommonStates).
                d.SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                d.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
                d.SetRenderState(D3DRS_ALPHAREF, 0xA0);
                d.SetRenderState(D3DRS_TEXTUREFACTOR, test.factor);
                d.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                d.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                d.SetTextureStageState(0, D3DTSS_ALPHAOP, test.alpha_op);
                d.SetTextureStageState(0, D3DTSS_ALPHAARG1, test.arg1);
                d.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
                d.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
                d.SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                d.SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                d.SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                d.SetTexture(0, test.texture);
                struct QuadVertex
                {
                    float x, y, z;
                    uint32_t diffuse;
                    float u, v;
                };
                const QuadVertex quad[] = {{-1, -1, .5f, test.diffuse, 0, 1}, {-1, 1, .5f, test.diffuse, 0, 0},
                                           {1, 1, .5f, test.diffuse, 1, 0},   {-1, -1, .5f, test.diffuse, 0, 1},
                                           {1, 1, .5f, test.diffuse, 1, 0},   {1, -1, .5f, test.diffuse, 1, 1}};
                d.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(QuadVertex)), "Draw");
                const bool discards = d.draw_alpha_test;
                check(d.EndScene(), "EndScene");
                check(d.GetRenderTargetData(target, readback), "GetRenderTargetData");
                if (frame == 2)
                {
                    D3DLOCKED_RECT locked;
                    check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
                    uint8_t pixels[kSize * kSize * 4];
                    for (uint32_t y = 0; y < kSize; ++y)
                        std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch,
                                    kSize * 4);
                    readback->UnlockRect();
                    bool pass = expect(pixels, kSize / 4, test.left, test.name) &&
                                expect(pixels, 3 * kSize / 4, test.right, test.name);
                    if (discards != test.discards)
                    {
                        std::printf("FAIL: %s: the alpha test %s\n", test.name,
                                    discards ? "is kept although nothing can fall below the reference"
                                             : "is left off although the alpha can fall below the reference");
                        pass = false;
                    }
                    std::printf("%s: %s\n", pass ? "ok" : "FAIL", test.name);
                    ok &= pass;
                }
                check(d.SetRenderTarget(0, back), "SetRenderTarget(back)");
                check(d.Present(), "Present");
            }
        for (shim::Texture2D *texture : {opaque, holed, rewritten})
            texture->Release();
        target->Release();
        readback->Release();
        back->Release();
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: alpha test\n" : "FAIL: alpha test\n");
    return ok ? 0 : 1;
}
