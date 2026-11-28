// Raster check of the sea's vertex shader (shaders/include/sea_surface.sh, here through vs_port_sea2): a grid quad
// displaced on the GPU by a real wave field (src/libs/sea/src/ocean_field.hpp, its samples uploaded as SEA uploads
// them: src/libs/sea/include/sea_surface_gpu.hpp) must land where the field puts it on the CPU (Field::Point: the
// displacement of each corner's point of the still water). The projection maps x to the screen's x and z plus the
// height to its y; the quad's edges, measured at the middle row and column, are compared with the edges of the quad
// the CPU displaces, within one pixel.
//
// Then the sea's self-check (storm/render/sea_surface_check.hpp: the same shader code for points of the field, read
// back) must return the field's heights, normals and shifts.
#include "device.hpp"
#include "ports.hpp"
#include "check_backend.hpp"
#include "ocean_field.hpp"
#include "sea_surface_gpu.hpp"
#include "storm/render/sea_surface_check.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace storm;
namespace
{
constexpr unsigned size = 512;
void check(HRESULT value)
{
    if (FAILED(value))
    {
        std::fprintf(stderr, "D3D failure %x\n", unsigned(value));
        std::exit(2);
    }
}

struct Case
{
    const char *name;
    size_t cascades; // of the field's three the registers name
    double originX, originZ;
    float cameraDistance; // from the quad; beyond 1000 m the sea is flat
};

void registers(const sea::ocean::Field &field, size_t cascades, double originX, double originZ, float out[5][4])
{
    const int n = field.Layout()[0].size;
    for (int c = 0; c < sea_gpu::kMaxCascades; ++c)
        if (static_cast<size_t>(c) < cascades)
            sea_gpu::cascade_register(originX, originZ, n, field.Layout()[c].patch, c, out[c]);
        else
            sea_gpu::absent_cascade(out[c]);
    out[3][2] = field.MeanLevel();
}
} // namespace

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO))
        return 2;
    host_check::UseBackendFromEnvironment();
    auto *window = SDL_CreateWindow("sea_surface_vertex_check", 0, 0, size, size, 0);
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
        vs->port = shim::find_port("sea_sea2_vertex_shader", true);
        auto *ps = new shim::PixelShader;
        ps->port = shim::find_port("sea_sea2_pixel_shader", false); // its varyings are the vertex port's
        if (!vs->port || !ps->port || !vs->port->vertex_texture)
        {
            std::puts("FAIL sea surface port not found or without its vertex texture");
            return 1;
        }

        // A rough, choppy sea of three cascades, as SEA uploads it: cascade c in rows c * n..
        sea::ocean::Field wave(sea::ocean::CascadeLayout(3, 64));
        wave.SetState(sea::ocean::MapSea2({6.0f, 0.5f, 1.5f, 2.0f, 2.0f, 200.0f, 0.7f}));
        wave.Update(17.25);
        const int n = wave.Layout()[0].size;
        shim::Texture2D *field = nullptr;
        check(d->CreateTexture(n, 3 * n, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED, &field, nullptr));
        D3DLOCKED_RECT fieldRect{};
        check(field->LockRect(0, &fieldRect, nullptr, 0));
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < n; ++y)
                std::memcpy(static_cast<uint8_t *>(fieldRect.pBits) + (c * n + y) * fieldRect.Pitch,
                            wave.SampleHalves(c).data() + static_cast<size_t>(y) * n, n * 8);
        check(field->UnlockRect(0));
        // The pixel port's cubes (stage 2: the light's colour, stage 3: the reflection): green, unread with the
        // constants below, which make the whole quad the water's green (opaque, nothing reflected) and the rest black.
        shim::CubeTexture *green = nullptr;
        check(d->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &green, nullptr));
        for (int face = 0; face < 6; ++face)
        {
            D3DLOCKED_RECT rect{};
            check(green->LockRect(static_cast<D3DCUBEMAP_FACES>(face), 0, &rect, nullptr, 0));
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + y * rect.Pitch)[x] = 0xff00ff00;
            check(green->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), 0));
        }

        // One grid quad, metres from the mesh's origin: corners 0 (x0, z0), 1 (x1, z0), 2 (x0, z1), 3 (x1, z1); the
        // sea's vertex (position, normal, texture coordinates: the flat grid leaves the last two 0)
        constexpr float x0 = 1.3f, x1 = 3.3f, z0 = -0.7f, z1 = 1.3f;
        const float corners[4][8] = {{x0, 0, z0}, {x1, 0, z0}, {x0, 0, z1}, {x1, 0, z1}};
        const uint16_t indices[6] = {0, 2, 1, 2, 3, 1};
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
            {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        shim::VertexDeclaration *decl = nullptr;
        check(d->CreateVertexDeclaration(elements, &decl));
        shim::VertexBuffer *vb = nullptr;
        shim::IndexBuffer *ib = nullptr;
        check(d->CreateVertexBuffer(sizeof(corners), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr));
        check(d->CreateIndexBuffer(sizeof(indices), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr));
        void *data = nullptr;
        check(vb->Lock(0, 0, &data, 0));
        std::memcpy(data, corners, sizeof(corners));
        check(vb->Unlock());
        check(ib->Lock(0, 0, &data, 0));
        std::memcpy(data, indices, sizeof(indices));
        check(ib->Unlock());

        const Case cases[] = {
            {"flat (beyond the sea's reach)", 3, 0.0, 0.0, 5000.0f},
            {"three cascades", 3, 0.0, 0.0, 10.0f},
            {"three cascades, far origin", 3, 9175.04, -2293.76, 10.0f},
            {"two cascades (the third absent)", 2, -1234.5, 777.25, 10.0f},
        };
        // clip x = (x - 2.3) * 0.75, clip y = z * 0.35 + 0.2 y (D3D rows of the matrix c24..c27)
        constexpr float sx = 0.75f, cx = 2.3f, sz = 0.35f, sy = 0.2f, cz = 0.3f;
        for (const Case &test : cases)
        {
            std::array<std::array<float, 4>, 14> c{};
            float surface[sea_gpu::kRegisters][4]{};
            registers(wave, test.cascades, test.originX, test.originZ, surface);
            surface[4][0] = 2.3f + test.cameraDistance; // the camera minus the mesh's origin
            surface[4][1] = 0.3f;
            surface[4][2] = 1000.0f * 1000.0f;
            surface[4][3] = sea_gpu::kSurfaceGpu;
            std::memcpy(c[sea_gpu::kFirstRegister].data(), surface, sizeof(surface));

            // The CPU's displaced corners on the screen (pixels, D3D: pixel centres at whole numbers), the quad
            // centred vertically
            float clipX[4], clipY[4], px[4], py[4];
            for (int i = 0; i < 4; ++i)
            {
                const double wx = test.originX + corners[i][0], wz = test.originZ + corners[i][2];
                sea::ocean::SurfacePoint point;
                if (test.cameraDistance < 1000.0f)
                    point = wave.Point(static_cast<float>(wx), static_cast<float>(wz), test.cascades);
                const float h = wave.MeanLevel() + point.offset.y;
                const float x = corners[i][0] + point.offset.x, z = corners[i][2] + point.offset.z;
                clipX[i] = (x - cx) * sx;
                clipY[i] = (z - cz) * sz + h * sy;
            }
            const float centre = 0.25f * (clipY[0] + clipY[1] + clipY[2] + clipY[3]);
            for (int i = 0; i < 4; ++i)
            {
                px[i] = (clipX[i] + 1.0f) * 0.5f * size;
                py[i] = (1.0f - (clipY[i] - centre)) * 0.5f * size;
            }
            // The rest of the port's registers as SEA sets them: no fog, no foam, the camera, the projection.
            c[0] = {0.0f, 1000.0f, 0.0f, 0.0f};
            c[13] = {2.3f, 50.0f, 0.3f, 1.0f};
            c[9] = {sx, 0, 0, -cx * sx};
            c[10] = {0, sy, sz, -cz * sz - centre};
            c[11] = {0, 0, 0, 0.5f};
            c[12] = {0, 0, 0, 1};
            const float p[8][4] = {{0.0f, 0.0f, 0.0f, 0.0f},  {0.0f, 0.0f, 0.0f, -1.0f},  {0.0f, 0.0f, 0.0f, -1.0f},
                                   {0.0f, 0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 0.0f},   {0.0f, 1.0f, 0.0f, 0.0f},
                                   {0.0f, 0.0f, 0.0f, 0.0f},  {1000.0f, 0.0f, 0.0f, 0.0f}};
            check(d->SetPixelShaderConstantF(0, &p[0][0], 8));
            check(d->SetVertexShaderConstantF(0, c[0].data(), static_cast<UINT>(c.size())));

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
            d->SetTexture(2, green);
            d->SetTexture(3, green);
            d->SetTexture(D3DVERTEXTEXTURESAMPLER0, field);
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            d->SetVertexDeclaration(decl);
            d->SetStreamSource(0, vb, 0, 32);
            d->SetIndices(ib);
            check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
            check(d->EndScene());
            check(d->GetRenderTargetData(target, readback));
            D3DLOCKED_RECT locked{};
            check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
            const auto drawn = [&](unsigned x, unsigned y) {
                const auto *p = static_cast<const unsigned char *>(locked.pBits) + y * locked.Pitch + 4 * x;
                return p[1] > 180 && p[0] < 100 && p[2] < 100;
            };
            // The middle row's left and right ends and the middle column's top and bottom ends
            const unsigned mid = size / 2;
            int left = -1, right = -1, top = -1, bottom = -1;
            for (unsigned x = 0; x < size; ++x)
                if (drawn(x, mid))
                {
                    if (left < 0)
                        left = static_cast<int>(x);
                    right = static_cast<int>(x);
                }
            for (unsigned y = 0; y < size; ++y)
                if (drawn(mid, y))
                {
                    if (top < 0)
                        top = static_cast<int>(y);
                    bottom = static_cast<int>(y);
                }
            const auto *middle = static_cast<const unsigned char *>(locked.pBits) + mid * locked.Pitch + 4 * mid;
            const unsigned centreColour = middle[0] | middle[1] << 8 | middle[2] << 16;
            check(readback->UnlockRect());
            check(d->SetRenderTarget(0, back));
            check(d->Present());
            // The CPU quad's edges there: left 0-2, right 1-3 at the row's centre y; top 2-3, bottom 0-1 at the
            // column's centre x
            const float at = static_cast<float>(mid);
            const auto along = [](float a0, float b0, float a1, float b1, float a) {
                return b0 + (b1 - b0) * (a - a0) / (a1 - a0);
            };
            const float eLeft = along(py[0], px[0], py[2], px[2], at), eRight = along(py[1], px[1], py[3], px[3], at);
            const float eTop = along(px[2], py[2], px[3], py[3], at), eBottom = along(px[0], py[0], px[1], py[1], at);
            // A covered pixel's centre lies inside: the first and last covered pixels are within a pixel of the edge
            const auto near = [](int measured, float edge) {
                return measured >= 0 && std::fabs(static_cast<float>(measured) - edge) <= 1.0f;
            };
            const bool pass = near(left, eLeft) && near(right, eRight) && near(top, eTop) && near(bottom, eBottom);
            std::printf("%s sea surface, %s: left %d (%.1f) right %d (%.1f) top %d (%.1f) bottom %d (%.1f), centre "
                        "BGR %06x\n",
                        pass ? "PASS" : "FAIL", test.name, left, eLeft, right, eRight, top, eTop, bottom, eBottom,
                        centreColour);
            ok &= pass;
        }

        // The self-check: points of the field through the same shader code, read back (a far origin), against the
        // field.
        {
            const Case &test = cases[2];
            float surface[sea_gpu::kRegisters][4]{};
            registers(wave, 3, test.originX, test.originZ, surface);
            surface[4][2] = 1.0e8f;
            constexpr uint32_t kPoints = render::kSeaSurfaceCheckMaxPoints;
            float points[kPoints][2];
            for (uint32_t i = 0; i < kPoints; ++i)
            {
                const float angle = static_cast<float>(i) * 2.39996f, radius = 1.37f * static_cast<float>(i) + 0.21f;
                points[i][0] = radius * std::cos(angle);
                points[i][1] = radius * std::sin(angle);
            }
            std::string why;
            bool retry = false;
            uint32_t ticket = 0;
            std::vector<render::SeaSurfaceSample> samples;
            render::SeaSurfaceCheckState state = render::SeaSurfaceCheckState::Lost;
            for (int frame = 0; frame < 20 && state != render::SeaSurfaceCheckState::Done; ++frame)
            {
                check(d->BeginScene());
                if (!ticket)
                {
                    ticket = render::StartSeaSurfaceCheck(field, &surface[0][0], &points[0][0], kPoints, why, retry);
                    if (!ticket)
                        std::printf("     self-check not started: %s%s\n", why.c_str(), retry ? " (retry)" : "");
                }
                check(d->EndScene());
                check(d->Present());
                if (ticket)
                    state = render::PollSeaSurfaceCheck(ticket, samples);
            }
            float worstHeight = 0.0f, worstNormal = 0.0f, worstShift = 0.0f;
            bool pass = state == render::SeaSurfaceCheckState::Done && samples.size() == kPoints;
            for (uint32_t i = 0; pass && i < kPoints; ++i)
            {
                const float wx = static_cast<float>(test.originX + points[i][0]);
                const float wz = static_cast<float>(test.originZ + points[i][1]);
                const sea::ocean::SurfacePoint point = wave.Point(wx, wz, 3);
                const auto normal = point.Normal();
                const auto &s = samples[i];
                worstHeight = std::max(worstHeight, std::fabs(s.height - (wave.MeanLevel() + point.offset.y)));
                worstShift = std::max(worstShift, std::fabs(s.dx - point.offset.x));
                worstNormal = std::max({worstNormal, std::fabs(s.nx - normal.x), std::fabs(s.nz - normal.z)});
            }
            // A half-float target: three significant digits of heights and shifts of metres.
            pass = pass && worstHeight <= 0.01f && worstShift <= 0.01f && worstNormal <= 2e-3f;
            std::printf("%s sea surface self-check (%s): %zu points, largest difference height %.6f m, shift %.6f m, "
                        "normal %.6f\n",
                        pass ? "PASS" : "FAIL", render::SeaSurfaceCheckDevice().c_str(), samples.size(), worstHeight,
                        worstShift, worstNormal);
            ok &= pass;
        }

        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetStreamSource(0, nullptr, 0, 0);
        d->SetIndices(nullptr);
        d->SetVertexDeclaration(nullptr);
        d->SetTexture(0, nullptr);
        d->SetTexture(2, nullptr);
        d->SetTexture(3, nullptr);
        d->SetTexture(D3DVERTEXTEXTURESAMPLER0, nullptr);
        vs->Release();
        ps->Release();
        decl->Release();
        vb->Release();
        ib->Release();
        field->Release();
        green->Release();
        target->Release();
        readback->Release();
        back->Release();
        d->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
