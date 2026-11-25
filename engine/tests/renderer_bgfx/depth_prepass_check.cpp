// The depth pre-pass (fx_depth_prepass, renderer_bgfx/README.md "Depth pre-pass") through the shim in an SDL window,
// on OpenGL ES and on Vulkan, with MSAA:
// - a frame with the pre-pass is the frame without it, pixel for pixel: opaque quads back to front with two cameras, an
//   alpha-tested, a depth-biased, a blended, a less-than, a skinned quad, a blended and a colourless quad that write
//   depth in front of a later opaque one, and a stencil marked before an opaque quad and read after it (a copy that
//   lays another depth than its draw leaves holes; one that reaches a draw before it changes what that draw sees), and
//   opaque quads drawn otherwise: lit, indexed from static buffers, scissored;
// - the opaque draws get a depth-only copy, the others none (bgfx's draw count, with the option against without);
// - Vulkan, STORM_VK_PIPELINE_STATISTICS=1 (the test sets it): the colour draws of the opaque quads, drawn twice with a
//   depth clear between, run the fragment shader once a pixel each time, once a layer without the pre-pass; the
//   scene's passes and bytes are those without it;
// - Vulkan: a copy is never drawn while the pipeline of its colour draw is being created (patches/
//   bgfx-vulkan-pipeline-cache.patch, STORM_STATE_PIPELINE_PAIRED): a new colour program for one quad, whose copy's
//   pipeline exists, leaves both out of the frames until it exists.
#include "check_backend.hpp"
#include "device.hpp"
#include "gpu_passes.hpp"
#include "skinning.hpp"
#include "skinning_palette.hpp"
#include "storm/render/effects.hpp"

#include <SDL2/SDL.h>

#include <bgfx/bgfx.h>
#include <bgfx/storm_vk_traffic.h>

namespace bgfx
{
uint32_t getVkDrawsLeftOut();     // patches/bgfx-vulkan-pipeline-cache.patch
uint32_t getVkPipelinesPending(); // patches/bgfx-vulkan-pipeline-cache.patch
} // namespace bgfx

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;

namespace
{
constexpr uint32_t kWidth = 320, kHeight = 240;
constexpr uint64_t kPixels = uint64_t(kWidth) * kHeight;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::fprintf(stderr, "FAIL: %s returned 0x%08x\n", what, static_cast<unsigned>(result));
        std::exit(1);
    }
}

bool expect(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    return ok;
}

struct Vertex
{
    float x, y, z;
    uint32_t color;
};

// What a frame draws besides the three opaque quads (each alone, or all of them).
enum Extra : uint32_t
{
    kNone = 0,
    kAlphaTested = 1,
    kBiased = 2,
    kBlended = 4,
    kLess = 8,
    kSkinned = 16,
    kStencil = 32,
    kWritesDepth = 128, // a blended and a colourless quad that write depth, each before an opaque quad behind it
    kVaried = 256,      // opaque quads drawn otherwise: lit, indexed from static buffers, scissored
    kAll = 63 | kWritesDepth | kVaried,
    kTwice = 64, // the three opaque quads again after a depth clear (a second pair of views)
};

class Frames
{
  public:
    explicit Frames(Device &device) : d_(device)
    {
        check(d_.GetRenderTarget(0, &back_), "GetRenderTarget");
        check(d_.CreateOffscreenPlainSurface(kWidth, kHeight, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
              "CreateOffscreenPlainSurface");
        // One bone, the identity: the skinned quad lands where its vertices say.
        check(d_.CreateTexture(model::kPaletteTexelsPerBone, 1, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F,
                               D3DPOOL_DEFAULT, &palette_, nullptr),
              "CreateTexture(palette)");
        D3DLOCKED_RECT locked;
        check(palette_->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD), "LockRect(palette)");
        const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        model::writePaletteRow(identity, static_cast<float *>(locked.pBits));
        palette_->UnlockRect(0);
        check(d_.CreateVertexBuffer(6 * sizeof(Vertex), D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE,
                                    D3DPOOL_MANAGED, &vertices_, nullptr),
              "CreateVertexBuffer");
        check(d_.CreateIndexBuffer(6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices_,
                                   nullptr),
              "CreateIndexBuffer");
        void *data = nullptr;
        check(vertices_->Lock(0, 0, &data, 0), "Lock(vertices)");
        // Two vertices no index reaches first: drawn without the indices, the quad would cover far more.
        const Vertex corners[6] = {{-40, -20, 6, 0xFF60E0A0}, {40, -20, 6, 0xFF60E0A0}, {1, 2, 6, 0xFF60E0A0},
                                   {3, 2, 6, 0xFF60E0A0},     {1, 4, 6, 0xFF60E0A0},    {3, 4, 6, 0xFF60E0A0}};
        std::memcpy(data, corners, sizeof(corners));
        vertices_->Unlock();
        check(indices_->Lock(0, 0, &data, 0), "Lock(indices)");
        const uint16_t order[6] = {2, 3, 4, 4, 3, 5};
        std::memcpy(data, order, sizeof(order));
        indices_->Unlock();
        check(d_.CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube_, nullptr), "CreateCubeTexture");
        for (int face = 0; face < 6; ++face)
        {
            check(cube_->LockRect(static_cast<D3DCUBEMAP_FACES>(face), 0, &locked, nullptr, 0), "LockRect(cube)");
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch)[x] =
                        0xFF30C0F0;
            cube_->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), 0);
        }
    }
    ~Frames()
    {
        cube_->Release();
        indices_->Release();
        vertices_->Release();
        palette_->Release();
        readback_->Release();
        back_->Release();
    }

    // One game frame; `cubed`: the nearest opaque quad takes its colour from a cube texture (another fragment program,
    // the same vertex shader). With `read`, the picture (BGRA) after the frame, by a read-back.
    std::vector<uint8_t> render(uint32_t extras, bool read, bool cubed = false)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.SetRenderTarget(0, back_), "SetRenderTarget");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF2050A0, 1.0f, 0),
              "Clear");
        D3DMATRIX identity{};
        identity.m[0][0] = identity.m[1][1] = identity.m[2][2] = identity.m[3][3] = 1.0f;
        d_.SetTransform(D3DTS_WORLD, &identity);
        d_.SetTransform(D3DTS_VIEW, &identity);
        camera(1.0f);
        d_.SetRenderState(D3DRS_ZENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        d_.SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        d_.SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d_.SetRenderState(D3DRS_STENCILENABLE, FALSE);
        d_.SetRenderState(D3DRS_DEPTHBIAS, 0);
        diffuse();
        layers(cubed);
        if (extras & kAlphaTested)
        {
            d_.SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            d_.SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            d_.SetRenderState(D3DRS_ALPHAREF, 0x40);
            quad(1, 3, -2, 2, 7, 0x80E0E040, "draw (alpha-tested)");
            d_.SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        if (extras & kBiased)
        {
            // Coplanar with the far quad, pulled in front of it by its bias.
            d_.SetRenderState(D3DRS_DEPTHBIAS, std::bit_cast<DWORD>(-0.0005f));
            quad(-4, 4, -5, -3, 10, 0xFFE04040, "draw (biased)");
            d_.SetRenderState(D3DRS_DEPTHBIAS, 0);
        }
        if (extras & kBlended)
        {
            d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            quad(-1, 1, -3, 3, 5, 0x8040E0E0, "draw (blended)");
            d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        }
        if (extras & kLess)
        {
            d_.SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
            quad(-40, 40, -20, 0, 9, 0xFF606060, "draw (less)");
            d_.SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        }
        if (extras & kWritesDepth)
        {
            // Over the far quad, each in front of an opaque quad drawn after it: the blended one shows the far quad
            // through it, the colourless one leaves the far quad.
            d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            quad(-6, -4.5f, -4.5f, -3, 5, 0x80F0F040, "draw (blended, depth write)");
            d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            quad(-9, -6, -7, -4, 7, 0xFF4040F0, "draw (opaque behind the blended one)");
            d_.SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            quad(4.5f, 6, -4.5f, -3, 5, 0xFFFFFFFF, "draw (colourless, depth write)");
            d_.SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            quad(6, 9, -7, -4, 7, 0xFF4040F0, "draw (opaque behind the colourless one)");
        }
        if (extras & kVaried)
            varied();
        if (extras & kSkinned)
            skinned_quad(-3, -1, 1, 3, 4);
        if (extras & kStencil)
        {
            // The stencil marks where its quad is nearest so far; an opaque quad covers part of it afterwards; a
            // draw over everything shows where the mark is.
            d_.SetRenderState(D3DRS_STENCILENABLE, TRUE);
            d_.SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
            d_.SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
            d_.SetRenderState(D3DRS_STENCILREF, 1);
            quad(-40, 40, -20, 20, 9.5f, 0xFF202020, "draw (stencil mark)");
            d_.SetRenderState(D3DRS_STENCILENABLE, FALSE);
            quad(2, 40, -20, 20, 3, 0xFFF0F0F0, "draw (opaque over the mark)");
            d_.SetRenderState(D3DRS_STENCILENABLE, TRUE);
            d_.SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
            d_.SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
            d_.SetRenderState(D3DRS_ZENABLE, FALSE);
            quad(-40, 40, -20, 20, 2, 0xFF00FF00, "draw (where the mark is)");
            d_.SetRenderState(D3DRS_ZENABLE, TRUE);
            d_.SetRenderState(D3DRS_STENCILENABLE, FALSE);
        }
        if (extras & kTwice)
        {
            check(d_.Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0), "Clear (depth, mid-scene)");
            layers(false);
        }
        render::EffectsSceneComplete();
        check(d_.EndScene(), "EndScene");
        std::vector<uint8_t> pixels;
        if (read)
        {
            check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
            D3DLOCKED_RECT locked;
            check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect");
            pixels.resize(size_t(kPixels) * 4);
            for (uint32_t y = 0; y < kHeight; ++y)
                std::memcpy(&pixels[size_t(y) * kWidth * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch,
                            kWidth * 4);
            readback_->UnlockRect();
        }
        check(d_.Present(), "Present");
        return pixels;
    }

  private:
    // A lit quad (the lit vertex shader and its depth-only partner), one indexed from static buffers and a scissored
    // one, each in front of the far quad.
    void varied()
    {
        struct LitVertex
        {
            float x, y, z, nx, ny, nz;
            uint32_t color;
        };
        D3DLIGHT9 light{};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = {0.9f, 0.8f, 0.7f, 1};
        light.Ambient = {0.2f, 0.2f, 0.2f, 1};
        light.Direction = {0.3f, -0.4f, 1.0f};
        d_.SetLight(0, &light);
        d_.LightEnable(0, TRUE);
        D3DMATERIAL9 material{};
        material.Diffuse = material.Ambient = {1, 1, 1, 1};
        d_.SetMaterial(&material);
        d_.SetRenderState(D3DRS_LIGHTING, TRUE);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);
        const auto lit = [](float x, float y) { return LitVertex{x, y, 6.5f, 0, 0, -1, 0xFFE08060}; };
        const LitVertex v[6] = {lit(-3, 2), lit(-1, 2), lit(-3, 4), lit(-3, 4), lit(-1, 2), lit(-1, 4)};
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(LitVertex)), "draw (lit)");
        d_.SetRenderState(D3DRS_LIGHTING, FALSE);
        d_.LightEnable(0, FALSE);
        diffuse();

        d_.SetStreamSource(0, vertices_, 0, sizeof(Vertex));
        d_.SetIndices(indices_);
        check(d_.DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 2, 4, 0, 2), "draw (indexed, static buffers)");
        d_.SetStreamSource(0, nullptr, 0, 0);
        d_.SetIndices(nullptr);

        const RECT scissor{40, 150, 120, 200};
        d_.SetScissorRect(&scissor);
        d_.SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        quad(-40, 40, -20, 20, 9.2f, 0xFF60A0E0, "draw (scissored)");
        d_.SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }
    // Back to front: the far quad over everything, an opaque blended one, one over the left half, one over a middle
    // band seen by a second camera.
    void layers(bool cubed)
    {
        quad(-40, 40, -20, 20, 10, 0xFFC08040, "draw (far)");
        // Blended with an alpha of 1 everywhere and writing depth: it replaces the colour, so the copies after it
        // still reach the far quad.
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d_.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d_.SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        quad(-40, 40, -20, 20, 9.8f, 0xFFB09060, "draw (blended, opaque)");
        d_.SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        quad(-40, 0, -20, 20, 8, 0xFF40C080, "draw (left half)");
        camera(1.25f, 0.25f);
        if (cubed)
        {
            d_.SetTexture(0, cube_);
            d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            d_.SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            d_.SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        }
        quad(-2, 2, -1, 1, 6, 0xFF8040C0, "draw (band)");
        diffuse();
        camera(1.0f);
    }
    // A perspective camera; the second one also maps depth otherwise (a nearer near plane: its depth is farther).
    void camera(float zoom, float near = 1.0f)
    {
        D3DMATRIX projection{};
        projection.m[0][0] = zoom * static_cast<float>(kHeight) / kWidth;
        projection.m[1][1] = zoom;
        projection.m[2][2] = 100.0f / (100.0f - near);
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -100.0f * near / (100.0f - near);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
    }
    void diffuse()
    {
        d_.SetTexture(0, nullptr);
        d_.SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        d_.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        d_.SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        d_.SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        d_.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    }
    void quad(float x0, float x1, float y0, float y1, float z, uint32_t color, const char *what)
    {
        const Vertex v[6] = {{x0, y0, z, color}, {x1, y0, z, color}, {x0, y1, z, color},
                             {x0, y1, z, color}, {x1, y0, z, color}, {x1, y1, z, color}};
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex)), what);
    }
    void skinned_quad(float x0, float x1, float y0, float y1, float z)
    {
        const auto vertex = [&](float x, float y) {
            return skinning::Source{{x, y, z}, 1.0f, 0, {0, 0, -1}, static_cast<int32_t>(0xFFE0A0E0), 0, 0};
        };
        const skinning::Source v[6] = {vertex(x0, y0), vertex(x1, y0), vertex(x0, y1),
                                       vertex(x0, y1), vertex(x1, y0), vertex(x1, y1)};
        d_.SetTexture(D3DVERTEXTEXTURESAMPLER0, palette_);
        d_.SetFVF(model::kSkinnedSourceFvf);
        check(d_.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, model::kSkinnedSourceStride), "draw (skinned)");
        d_.SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);
        diffuse();
    }

    Device &d_;
    shim::Surface *back_ = nullptr, *readback_ = nullptr;
    shim::Texture2D *palette_ = nullptr;
    shim::CubeTexture *cube_ = nullptr;
    shim::VertexBuffer *vertices_ = nullptr;
    shim::IndexBuffer *indices_ = nullptr;
};

// The scene's passes of the frame published after `after` (frames of `extras` are drawn until one is).
std::vector<bgfx::VkPassTraffic> scene_traffic(Frames &frames, uint32_t extras, uint32_t after, uint32_t *published)
{
    std::vector<bgfx::VkPassTraffic> passes(256);
    for (;;)
    {
        frames.render(extras, false);
        uint32_t frame = 0;
        const uint32_t count = bgfx::getVkPassTraffic(passes.data(), static_cast<uint32_t>(passes.size()), &frame);
        if (frame > after && count <= passes.size())
        {
            passes.resize(count);
            std::erase_if(passes, [](const bgfx::VkPassTraffic &pass) { return !shim::PassTraffic::IsScene(pass); });
            *published = frame;
            return passes;
        }
    }
}

// A field of the passes added up; UINT64_MAX when a pass did not count it.
template <size_t N>
uint64_t sum(const std::vector<bgfx::VkPassTraffic> &passes, uint64_t (bgfx::VkPassTraffic::*field)[N], int k)
{
    uint64_t total = 0;
    for (const bgfx::VkPassTraffic &pass : passes)
    {
        if ((pass.*field)[k] == UINT64_MAX)
            return UINT64_MAX;
        total += (pass.*field)[k];
    }
    return total;
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
    SDL_Window *window = SDL_CreateWindow("depth_prepass_check", 0, 0, kWidth, kHeight, 0);
    if (!window || !shim::Context::instance().init(window, kWidth, kHeight, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    bool paired_seen = true;
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = kWidth;
    params.BackBufferHeight = kHeight;
    auto *device = new Device(window, params);
    {
        Frames frames(*device);
        const bool vulkan = bgfx::getRendererType() == bgfx::RendererType::Vulkan;
        const auto configure = [&](bool prepass) {
            // Without the dither: its noise moves from frame to frame.
            render::ConfigureEffects(1, {{"fx_msaa", 4}, {"fx_dither", 0}, {"fx_depth_prepass", prepass ? 1 : 0}});
            while (render::CurrentEffects().pending)
                frames.render(kAll, false);
            // Every pipeline of the frame exists: nothing is left out waiting for one.
            for (int i = 0; i < 8 || (vulkan && bgfx::getVkPipelinesPending() != 0); ++i)
                frames.render(kAll, false);
        };
        // bgfx's draws of a frame of `extras` (the stats of the last frame the render thread finished): the fewest of
        // a few frames, since some frames carry a draw of the device's own.
        const auto draws = [&](uint32_t extras) {
            for (int i = 0; i < 6 || (vulkan && bgfx::getVkPipelinesPending() != 0); ++i)
                frames.render(extras, false);
            uint32_t fewest = UINT32_MAX;
            for (int i = 0; i < 4; ++i)
            {
                frames.render(extras, false);
                fewest = std::min(fewest, bgfx::getStats()->numDraw);
            }
            return fewest;
        };
        struct Kind
        {
            uint32_t extras;
            uint32_t copies; // its opaque draws
            const char *name;
        };
        const Kind kinds[] = {{kNone, 3, "the three opaque quads"},
                              {kAlphaTested, 3, "an alpha-tested quad"},
                              {kBiased, 3, "a depth-biased quad"},
                              {kBlended, 3, "a blended quad"},
                              {kLess, 3, "a less-than quad"},
                              {kSkinned, 3, "a skinned quad"},
                              {kStencil, 4, "the stencil's quads and the opaque quad between them"},
                              {kWritesDepth, 5, "depth-writing quads that do not replace the colour"},
                              {kVaried, 6, "a lit, an indexed and a scissored opaque quad"}};
        // Every depth-only partner links with its vertex shader (bgfx refuses a program whose fragment inputs are not
        // the vertex outputs).
        uint32_t partners = 0;
        bool linked = true;
        for (uint32_t id = 0; id < shim::embedded_shader_count(); ++id)
            if (const uint32_t fs = shim::depth_only_partner(id); fs != UINT32_MAX)
            {
                ++partners;
                linked &= bgfx::isValid(device->program(id, fs));
            }
        ok &= expect(partners == 5 && linked,
                     std::to_string(partners) + " vertex shaders link with their depth-only partner");
        configure(false);
        const std::vector<uint8_t> without = frames.render(kAll, true);
        std::vector<uint32_t> draws_without;
        for (const Kind &kind : kinds)
            draws_without.push_back(draws(kind.extras));
        std::vector<bgfx::VkPassTraffic> traffic_without;
        uint32_t published = 0;
        if (vulkan)
            traffic_without = scene_traffic(frames, kTwice, shim::Context::instance().last_frame(), &published);

        configure(true);
        const std::vector<uint8_t> with = frames.render(kAll, true);
        size_t differ = 0;
        for (size_t i = 0; i < with.size(); i += 4)
            differ += std::memcmp(&with[i], &without[i], 4) != 0;
        ok &= expect(differ == 0, "the frame with the pre-pass is the frame without it (" + std::to_string(differ) +
                                      " pixels differ)");
        for (size_t i = 0; i < std::size(kinds); ++i)
        {
            const uint32_t copies = draws(kinds[i].extras) - draws_without[i];
            ok &= expect(copies == kinds[i].copies,
                         std::string("depth-only copies with ") + kinds[i].name + ": " + std::to_string(copies));
        }
        if (vulkan)
        {
            const std::vector<bgfx::VkPassTraffic> traffic =
                scene_traffic(frames, kTwice, shim::Context::instance().last_frame(), &published);
            ok &= expect(traffic.size() == traffic_without.size(),
                         "the scene's passes: " + std::to_string(traffic.size()) + ", without the pre-pass " +
                             std::to_string(traffic_without.size()));
            for (int k = 0; k < 2; ++k)
                ok &= expect(sum(traffic, &bgfx::VkPassTraffic::loaded, k) ==
                                     sum(traffic_without, &bgfx::VkPassTraffic::loaded, k) &&
                                 sum(traffic, &bgfx::VkPassTraffic::stored, k) ==
                                     sum(traffic_without, &bgfx::VkPassTraffic::stored, k) &&
                                 sum(traffic, &bgfx::VkPassTraffic::resolved, k) ==
                                     sum(traffic_without, &bgfx::VkPassTraffic::resolved, k),
                             std::string("the scene's ") + (k ? "depth" : "colour") +
                                 " bytes loaded, stored and resolved are those without the pre-pass");
            const uint64_t opaque = sum(traffic, &bgfx::VkPassTraffic::invocations, bgfx::VkDrawClass::Opaque);
            const uint64_t opaque_without =
                sum(traffic_without, &bgfx::VkPassTraffic::invocations, bgfx::VkDrawClass::Opaque);
            if (opaque == UINT64_MAX || opaque_without == UINT64_MAX || traffic.empty())
                ok &= expect(false, "fragment shader invocations counted (STORM_VK_PIPELINE_STATISTICS=1)");
            else
            {
                const double per_pixel = double(opaque) / kPixels, per_pixel_without = double(opaque_without) / kPixels;
                const double depth_only =
                    double(sum(traffic, &bgfx::VkPassTraffic::invocations, bgfx::VkDrawClass::DepthOnly)) / kPixels;
                std::printf("     opaque invocations per pixel %.4f (without the pre-pass %.4f), depth only %.4f\n",
                            per_pixel, per_pixel_without, depth_only);
                ok &= expect(per_pixel_without > 3.0, "without the pre-pass the hidden layers are shaded");
                ok &= expect(per_pixel < 2.04, "with it every pixel is shaded once on each side of the depth clear");
            }

            // A new colour program for the band quad: its copy's pipeline exists, its own does not. Until it does,
            // neither is drawn: a scene pass never has more depth-only draws than opaque ones.
            const uint32_t left_out = bgfx::getVkDrawsLeftOut();
            bool waited = false, paired = true;
            uint32_t frame = shim::Context::instance().last_frame();
            for (int i = 0; i < 16 || bgfx::getVkPipelinesPending() != 0; ++i)
            {
                frames.render(kNone, false, true);
                std::vector<bgfx::VkPassTraffic> passes(256);
                uint32_t number = 0;
                const uint32_t count = bgfx::getVkPassTraffic(passes.data(), 256, &number);
                if (number <= frame || count > passes.size())
                    continue;
                frame = number;
                for (uint32_t p = 0; p < count; ++p)
                    if (shim::PassTraffic::IsScene(passes[p]))
                    {
                        const uint32_t depth_only = passes[p].draws[bgfx::VkDrawClass::DepthOnly];
                        const uint32_t opaque_draws = passes[p].draws[bgfx::VkDrawClass::Opaque];
                        waited |= opaque_draws < 3;
                        paired &= depth_only <= opaque_draws;
                        if (depth_only > opaque_draws)
                            std::printf("     frame %u: %u depth-only draws, %u opaque\n", number, depth_only,
                                        opaque_draws);
                    }
            }
            ok &= expect(paired, "no depth-only copy drawn without its colour draw");
            if (!waited || bgfx::getVkDrawsLeftOut() == left_out)
            {
                std::printf("NOT_RUN: the new colour pipeline was never waited for (from the driver's cache?)\n");
                paired_seen = false;
            }
        }
        render::ConfigureEffects(0, {});
        while (render::CurrentEffects().pending)
            frames.render(kNone, false);
    }
    device->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%s: depth_prepass_check\n", ok ? "PASS" : "FAIL");
    return !ok ? 1 : paired_seen ? 0 : 77;
}
