// The fixed-function stage setups ff_stages.sh takes without its interpreter (the common setups of the game's
// models, lightmaps and blended layers) show what the interpreter shows for the same arithmetic. Real GPU readback.
#include "device.hpp"
#include "check_backend.hpp"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{
constexpr uint32_t kSize = 16;
constexpr uint32_t kTexel = 0xFF603020; // A, R, G, B

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

int channel(uint32_t argb, int shift)
{
    return static_cast<int>((argb >> shift) & 0xFF);
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
    SDL_Window *window = SDL_CreateWindow("ff_stage_check", 0, 0, kSize, kSize, 0);
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
        shim::Texture2D *texture = nullptr, *bright = nullptr;
        check(d.CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr),
              "CreateRenderTarget");
        check(d.CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr),
              "CreateOffscreenPlainSurface");
        check(d.GetRenderTarget(0, &back), "GetRenderTarget");
        // kTexel, and a bright texel whose 2x lightmap stage passes white before the next stage darkens it.
        for (auto [made, texel] : {std::pair{&texture, kTexel}, std::pair{&bright, 0xFFE0E0E0u}})
        {
            check(d.CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, made, nullptr), "CreateTexture");
            D3DLOCKED_RECT rect;
            check((*made)->LockRect(0, &rect, nullptr, 0), "LockRect(texture)");
            for (uint32_t y = 0; y < 4; ++y)
                for (uint32_t x = 0; x < 4; ++x)
                    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch + x * 4) = texel;
            (*made)->UnlockRect(0);
        }

        struct Stage
        {
            DWORD color_op, color1, color2, color0, alpha_op, alpha1, alpha2;
        };
        struct Setup
        {
            std::vector<Stage> stages;
            uint32_t diffuse, factor;
            shim::Texture2D *texture;
        };
        // Draws the setup over the whole target and reads the middle pixel back (0xAARRGGBB).
        const auto draw = [&](const Setup &setup) {
            uint32_t shown = 0;
            for (int frame = 0; frame < 3; ++frame) // the first frames may build the programs
            {
                check(d.BeginScene(), "BeginScene");
                check(d.SetRenderTarget(0, target), "SetRenderTarget");
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
                d.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
                d.SetRenderState(D3DRS_TEXTUREFACTOR, setup.factor);
                for (DWORD i = 0; i < setup.stages.size(); ++i)
                {
                    const Stage &stage = setup.stages[i];
                    d.SetTextureStageState(i, D3DTSS_COLOROP, stage.color_op);
                    d.SetTextureStageState(i, D3DTSS_COLORARG1, stage.color1);
                    d.SetTextureStageState(i, D3DTSS_COLORARG2, stage.color2);
                    d.SetTextureStageState(i, D3DTSS_COLORARG0, stage.color0);
                    d.SetTextureStageState(i, D3DTSS_ALPHAOP, stage.alpha_op);
                    d.SetTextureStageState(i, D3DTSS_ALPHAARG1, stage.alpha1);
                    d.SetTextureStageState(i, D3DTSS_ALPHAARG2, stage.alpha2);
                    d.SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, 0);
                    d.SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                    d.SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                    d.SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                    d.SetTexture(i, setup.texture);
                }
                d.SetTextureStageState(static_cast<DWORD>(setup.stages.size()), D3DTSS_COLOROP, D3DTOP_DISABLE);
                struct QuadVertex
                {
                    float x, y, z;
                    uint32_t diffuse;
                    float u, v;
                };
                const uint32_t c = setup.diffuse;
                const QuadVertex quad[] = {{-1, -1, .5f, c, 0, 1}, {-1, 1, .5f, c, 0, 0}, {1, 1, .5f, c, 1, 0},
                                           {-1, -1, .5f, c, 0, 1}, {1, 1, .5f, c, 1, 0},  {1, -1, .5f, c, 1, 1}};
                d.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
                check(d.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(QuadVertex)), "Draw");
                check(d.EndScene(), "EndScene");
                check(d.GetRenderTargetData(target, readback), "GetRenderTargetData");
                D3DLOCKED_RECT locked;
                check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
                std::memcpy(&shown, static_cast<uint8_t *>(locked.pBits) + kSize / 2 * locked.Pitch + kSize / 2 * 4, 4);
                readback->UnlockRect();
                for (DWORD i = 0; i < setup.stages.size(); ++i)
                    d.SetTexture(i, nullptr);
                check(d.SetRenderTarget(0, back), "SetRenderTarget(back)");
                check(d.Present(), "Present");
            }
            return shown;
        };
        const auto same = [](uint32_t a, uint32_t b) {
            for (int shift = 0; shift < 32; shift += 8)
                if (std::abs(channel(a, shift) - channel(b, shift)) > 1)
                    return false;
            return true;
        };

        constexpr DWORD kTex = D3DTA_TEXTURE, kDif = D3DTA_DIFFUSE, kCur = D3DTA_CURRENT;
        constexpr DWORD kOff = D3DTOP_DISABLE, kMod = D3DTOP_MODULATE, kMod2 = D3DTOP_MODULATE2X;
        const Stage modulate2x{kMod2, kDif, kTex, kCur, kMod, kDif, kTex};
        const Stage lightmap_base{D3DTOP_SELECTARG2, kDif, kTex, kCur, kMod, kDif, kTex};
        const Stage lightmap{kMod2, kCur, kTex, kCur, kOff, kCur, kCur};
        const Stage vertex_light{kMod2, kCur, kDif, kCur, kOff, kCur, kCur};
        const Stage modulate{kMod, kTex, kDif, kCur, kOff, kCur, kCur};
        const Stage factor_blend{D3DTOP_BLENDFACTORALPHA, kTex, kCur, kCur, kOff, kCur, kCur};
        constexpr uint32_t kDiffuse = 0x80A0C0E0, kFactor = 0x60FFFFFF;
        // The setups the shader recognises (ff_stages.sh ffCommonStage), each beside the same arithmetic through the
        // interpreter (its arguments turned, or an equal operation).
        const struct
        {
            const char *name;
            Setup common, interpreted;
        } pairs[] = {
            {"2x diffuse * texture",
             {{modulate2x}, kDiffuse, kFactor, texture},
             {{{kMod2, kTex, kDif, kCur, kMod, kTex, kDif}}, kDiffuse, kFactor, texture}},
            {"2x diffuse * texture past white",
             {{modulate2x}, 0xFFFFFFFF, kFactor, texture},
             {{{kMod2, kTex, kDif, kCur, kMod, kTex, kDif}}, 0xFFFFFFFF, kFactor, texture}},
            {"texture * diffuse, then the texture over it by the factor's alpha",
             {{modulate, factor_blend}, kDiffuse, kFactor, texture},
             {{{kMod, kDif, kTex, kCur, kOff, kCur, kCur},
               {D3DTOP_LERP, kTex, kCur, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE, kOff, kCur, kCur}},
              kDiffuse,
              kFactor,
              texture}},
            {"a lightmap: the texture, 2x the lightmap, 2x the diffuse",
             {{lightmap_base, lightmap, vertex_light}, kDiffuse, kFactor, texture},
             {{{D3DTOP_SELECTARG1, kTex, kDif, kCur, kMod, kTex, kDif},
               {kMod2, kTex, kCur, kCur, kOff, kCur, kCur},
               {kMod2, kDif, kCur, kCur, kOff, kCur, kCur}},
              kDiffuse,
              kFactor,
              texture}},
            {"a lightmap past white, darkened by the diffuse",
             {{lightmap_base, lightmap, vertex_light}, 0x40404040, kFactor, bright},
             {{{D3DTOP_SELECTARG1, kTex, kDif, kCur, kMod, kTex, kDif},
               {kMod2, kTex, kCur, kCur, kOff, kCur, kCur},
               {kMod2, kDif, kCur, kCur, kOff, kCur, kCur}},
              0x40404040,
              kFactor,
              bright}},
        };
        for (const auto &pair : pairs)
        {
            const uint32_t common = draw(pair.common), interpreted = draw(pair.interpreted);
            const bool pass = same(common, interpreted);
            std::printf("%s: %s: %08x, through the interpreter %08x\n", pass ? "ok" : "FAIL", pair.name, common,
                        interpreted);
            ok &= pass;
        }
        // What the common 2x modulate shows, worked out by hand: 2 * (0xA0, 0xC0, 0xE0) * (0x60, 0x30, 0x20) / 255 per
        // colour, 0x80 * 0xFF / 255 for the alpha.
        const uint32_t shown = draw({{modulate2x}, kDiffuse, kFactor, texture});
        const bool pass = same(shown, 0x80784838);
        std::printf("%s: 2x diffuse * texture shows %08x, expected 80784838\n", pass ? "ok" : "FAIL", shown);
        ok &= pass;
        texture->Release();
        bright->Release();
        target->Release();
        readback->Release();
        back->Release();
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf(ok ? "PASS: fixed-function stages\n" : "FAIL: fixed-function stages\n");
    return ok ? 0 : 1;
}
