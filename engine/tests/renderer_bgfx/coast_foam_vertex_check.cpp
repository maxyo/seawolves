// Raster check of the coast foam's vertex shader (shaders/ports/vs_port_coastfoam.sc): the sea's height under a
// strand, from a real wave field uploaded as SEA uploads it (src/libs/sea/src/ocean_field.hpp), must be the height of
// the field on the CPU (Field::Point). A strand lies across the target; the projection moves it up by the height, so
// the band's rows give the height back. The field leans no waves (no horizontal displacement): the band's rows would
// move with it.
#include "device.hpp"
#include "ports.hpp"
#include "check_backend.hpp"
#include "foam_gpu.hpp"
#include "ocean_field.hpp"
#include <SDL2/SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace storm;
namespace
{
constexpr unsigned size = 256;
constexpr float kPixelsPerMetre = 1.28f; // clip y per metre 0.01, 128 pixels per clip unit
void check(HRESULT value)
{
    if (FAILED(value))
    {
        std::fprintf(stderr, "D3D failure %x\n", unsigned(value));
        std::exit(2);
    }
}

} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO))
        return 2;
    host_check::UseBackendFromEnvironment();
    auto *window = SDL_CreateWindow("coast_foam_vertex_check", 0, 0, size, size, 0);
    if (!window || !shim::Context::instance().init(window, size, size, false))
        return 2;
    bool ok = true;
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = params.BackBufferHeight = size;
        auto *d = new shim::Device(window, params);
        shim::Surface *target = nullptr, *readback = nullptr, *back = nullptr;
        check(d->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr));
        check(d->CreateOffscreenPlainSurface(size, size, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr));
        check(d->GetRenderTarget(0, &back));
        auto *vs = new shim::VertexShader;
        vs->port = shim::find_port("coastfoam_foam_vertex_shader", true);
        auto *ps = new shim::PixelShader;
        ps->port = shim::find_port("coastfoam_foam_pixel_shader", false);
        if (!vs->port || !ps->port)
        {
            std::puts("FAIL coast foam ports not found");
            return 1;
        }

        // A heavy swell without lean, as SEA uploads it: cascade c in rows c * n..
        sea::ocean::Field wave(sea::ocean::CascadeLayout(3, 64));
        wave.SetState(sea::ocean::MapSea2({100.0f, 0.08f, 3.0f, 1.0f, 0.0f, 200.0f, 0.3f}));
        // A moment when a crest or trough lies under the strand: a height the band's rows show well.
        double moment = 0.0;
        float deepest = 0.0f;
        for (double t = 0.0; t < 60.0; t += 0.5)
        {
            wave.Update(t);
            const float h = std::fabs(wave.Point(0.0f, 0.5f, 3).offset.y);
            if (h > deepest)
                deepest = h, moment = t;
        }
        wave.Update(moment);
        const int n = wave.Layout()[0].size;
        shim::Texture2D *field = nullptr;
        check(d->CreateTexture(n, 3 * n, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &field, nullptr));
        D3DLOCKED_RECT fieldRect{};
        check(field->LockRect(0, &fieldRect, nullptr, 0));
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < n; ++y)
                std::memcpy(static_cast<uint8_t *>(fieldRect.pBits) + (c * n + y) * fieldRect.Pitch,
                            wave.Samples(c).data() + static_cast<size_t>(y) * n, n * 16);
        check(field->UnlockRect(0));
        const float patches[3] = {wave.Layout()[0].patch, wave.Layout()[1].patch, wave.Layout()[2].patch};
        shim::Texture2D *white = nullptr;
        check(d->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white, nullptr));
        D3DLOCKED_RECT rect{};
        check(white->LockRect(0, &rect, nullptr, 0));
        *static_cast<uint32_t *>(rect.pBits) = 0xff00ff00;
        check(white->UnlockRect(0));

        // One strand of two columns at x = -0.5 and 0.5, rows from z = 0 to z = 1 (move 1)
        std::vector<foam_gpu::Vertex> vertices;
        for (int row = 0; row < foam_gpu::kRows; ++row)
            for (int x = 0; x < 2; ++x)
                vertices.push_back({x - 0.5f, 0.0f, x - 0.5f, 1.0f, 0.0f, static_cast<uint8_t>(row), 4, 0, 0});
        std::vector<uint32_t> indices;
        for (uint32_t y = 0; y + 1 < foam_gpu::kRows; ++y)
            for (uint32_t i : {0u, 2u, 1u, 2u, 3u, 1u})
                indices.push_back(y * 2 + i);
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 16, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 20, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
            D3DDECL_END()};
        shim::VertexDeclaration *decl = nullptr;
        check(d->CreateVertexDeclaration(elements, &decl));
        shim::VertexBuffer *vb = nullptr;
        shim::IndexBuffer *ib = nullptr;
        check(d->CreateVertexBuffer(vertices.size() * sizeof(foam_gpu::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                    &vb, nullptr));
        check(d->CreateIndexBuffer(indices.size() * 4, D3DUSAGE_WRITEONLY, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &ib,
                                   nullptr));
        void *data = nullptr;
        check(vb->Lock(0, 0, &data, 0));
        std::memcpy(data, vertices.data(), vertices.size() * sizeof(foam_gpu::Vertex));
        check(vb->Unlock());
        check(ib->Lock(0, 0, &data, 0));
        std::memcpy(data, indices.data(), indices.size() * 4);
        check(ib->Unlock());

        struct Case
        {
            const char *name;
            int cascades;
            float cameraX;
        };
        // The camera far away flattens the sea (WaveXZ's reach).
        const Case cases[] = {
            {"flat (beyond the sea's reach)", 3, 1.0e4f},
            {"three cascades", 3, 0.0f},
            {"two cascades", 2, 0.0f},
        };
        float flatRow = 0.0f;
        for (const Case &test : cases)
        {
            std::array<std::array<float, 4>, foam_gpu::kRegisters> c{};
            // clip x = 2 x, clip y = (z - 0.5) * 0.5 + 0.01 * y (columns of view * projection)
            c[0] = {2, 0, 0, 0};
            c[1] = {0, 0.01f, 0, 0};
            c[2] = {0, 0.5f, 0, 0};
            c[3] = {0, -0.25f, 0.5f, 1};
            float cascades[3][4];
            foam_gpu::cascade_registers(n, test.cascades, patches, cascades);
            for (int k = 0; k < 3; ++k)
                std::memcpy(c[4 + k].data(), cascades[k], sizeof(cascades[k]));
            c[7] = {test.cameraX, 0.0f, 1000.0f * 1000.0f,
                    0.0f}; // the still level at 0: the band's rows are the height
            c[8] = {0.0f, 0.0f, 0.0f, 0.0f};
            c[foam_gpu::kFirstStrand] = {1.0f, 1.0f, 0.0f, 1.0f};
            check(d->SetVertexShaderConstantF(0, c[0].data(), foam_gpu::kRegisters));
            check(d->BeginScene());
            check(d->SetRenderTarget(0, target));
            check(d->SetDepthStencilSurface(nullptr));
            check(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0));
            d->SetRenderState(D3DRS_ZENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_LIGHTING, FALSE);
            d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetRenderState(D3DRS_FOGENABLE, FALSE);
            d->SetTexture(0, white);
            d->SetTexture(D3DVERTEXTEXTURESAMPLER0, field);
            for (DWORD stage : {0u})
            {
                d->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                d->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                d->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            }
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            d->SetVertexDeclaration(decl);
            d->SetStreamSource(0, vb, 0, sizeof(foam_gpu::Vertex));
            d->SetIndices(ib);
            check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices.size(), 0, indices.size() / 3));
            check(d->EndScene());
            check(d->GetRenderTargetData(target, readback));
            D3DLOCKED_RECT locked{};
            check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
            // The band's rows at the centre column (x = 0)
            int first = -1, last = -1;
            for (unsigned y = 0; y < size; ++y)
            {
                const auto *p = static_cast<const unsigned char *>(locked.pBits) + y * locked.Pitch + 4 * (size / 2);
                if (p[1] > 180 && p[0] < 100 && p[2] < 100)
                {
                    if (first < 0)
                        first = static_cast<int>(y);
                    last = static_cast<int>(y);
                }
            }
            check(readback->UnlockRect());
            check(d->SetRenderTarget(0, back));
            check(d->Present());
            const float centre = 0.5f * (first + last);
            if (test.cameraX > 1000.0f)
                flatRow = centre;
            // The strand's middle (x = 0, z = 0.5): the field over 1 m of a strand changes by less than the tolerance.
            const float expected = test.cameraX > 1000.0f ? 0.0f : wave.Point(0.0f, 0.5f, test.cascades).offset.y;
            const float measured = (flatRow - centre) / kPixelsPerMetre; // up the target: fewer rows
            const bool pass = first >= 0 && last - first >= 50 && std::fabs(measured - expected) <= 1.5f;
            std::printf("%s coast foam height, %s: band rows %d..%d, %.2f m (expected %.2f m)\n",
                        pass ? "PASS" : "FAIL", test.name, first, last, measured, expected);
            ok &= pass;
        }
        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        d->SetVertexDeclaration(nullptr);
        d->SetTexture(0, nullptr);
        d->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);
        vs->Release();
        ps->Release();
        decl->Release();
        vb->Release();
        ib->Release();
        field->Release();
        white->Release();
        target->Release();
        readback->Release();
        back->Release();
        d->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
