// The drawn sky's clouds (fx::Sky::Draw, fs_fx_sky.sc) from the scene environment, on OpenGL ES and Vulkan in an SDL
// window: the camera looks straight up at a day sky, then at a night sky.
//   * a clear sky is smooth; clouds break it up, the more of it the larger their cover;
//   * the rain clouds' full cover is an even grey, less blue than the clear sky;
//   * the wind's drift moves the clouds; the same drift draws the same picture;
//   * at night clouds hide the stars (seen through a narrow view: a star covers a pixel);
//   * three octaves shade the clouds towards the light, a fourth adds detail.
#include <bits/stdc++.h> // every standard header before the access change below

#define private public // the host's active settings (the clouds' octaves)
#include "device.hpp"
#include "effects/effects.hpp"
#undef private
#include "check_backend.hpp"
#include "environment.hpp"
#include "storm/render/effects.hpp"

#include <SDL2/SDL.h>

using namespace storm;
using shim::Device;

namespace
{
constexpr uint32_t kSize = 256;

void check(HRESULT result, const char *what)
{
    if (FAILED(result))
    {
        std::printf("FAIL: %s\n", what);
        std::exit(1);
    }
}

struct Picture
{
    std::vector<uint8_t> bgra;
    glm::vec3 at(uint32_t x, uint32_t y) const
    {
        const uint8_t *p = &bgra[(y * kSize + x) * 4];
        return {p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f};
    }
    // The share of pixels further than `step` (0..1) from the mean of their 5 x 5 neighbourhood: broken-up texture.
    float rough(float step) const
    {
        int count = 0, total = 0;
        for (uint32_t y = 2; y + 2 < kSize; ++y)
            for (uint32_t x = 2; x + 2 < kSize; ++x)
            {
                glm::vec3 mean(0.0f);
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        mean += at(x + dx, y + dy);
                mean /= 25.0f;
                count += glm::length(at(x, y) - mean) > step;
                ++total;
            }
        return float(count) / float(total);
    }
    // Mean blue over red: how blue the picture is.
    float blueness() const
    {
        glm::vec3 sum(0.0f);
        for (uint32_t y = 0; y < kSize; ++y)
            for (uint32_t x = 0; x < kSize; ++x)
                sum += at(x, y);
        return sum.b / std::max(sum.r, 1e-3f);
    }
    // Pixels much brighter than all their 4 neighbours: stars.
    int points() const
    {
        int count = 0;
        const auto lum = [&](uint32_t x, uint32_t y) { return glm::dot(at(x, y), glm::vec3(1.0f / 3.0f)); };
        for (uint32_t y = 1; y + 1 < kSize; ++y)
            for (uint32_t x = 1; x + 1 < kSize; ++x)
            {
                const float l = lum(x, y);
                count += l > lum(x - 1, y) + 0.05f && l > lum(x + 1, y) + 0.05f && l > lum(x, y - 1) + 0.05f &&
                         l > lum(x, y + 1) + 0.05f;
            }
        return count;
    }
    float difference(const Picture &other) const
    {
        float sum = 0.0f;
        for (size_t i = 0; i < bgra.size(); ++i)
            sum += std::abs(float(bgra[i]) - float(other.bgra[i]));
        return sum / float(bgra.size()) / 255.0f;
    }
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

    // The sky straight overhead, 90 degrees across (`zoom` times narrower), under `state`.
    Picture render(const environment::State &state, float zoom = 1.0f)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX view{}; // world y to the view's z: looking up
        view.m[0][0] = 1.0f;
        view.m[1][2] = 1.0f;
        view.m[2][1] = -1.0f;
        view.m[3][3] = 1.0f;
        const float zn = 1.0f, zf = 1000.0f;
        D3DMATRIX projection{};
        projection.m[0][0] = projection.m[1][1] = zoom;
        projection.m[2][2] = zf / (zf - zn);
        projection.m[2][3] = 1.0f;
        projection.m[3][2] = -zn * zf / (zf - zn);
        d_.SetTransform(D3DTS_VIEW, &view);
        d_.SetTransform(D3DTS_PROJECTION, &projection);
        render::SetSceneEnvironment(state);
        render::DrawSky();
        check(d_.EndScene(), "EndScene");
        check(d_.GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        Picture picture;
        picture.bgra.resize(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&picture.bgra[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch,
                        kSize * 4);
        readback_->UnlockRect();
        check(d_.Present(), "Present");
        return picture;
    }

  private:
    Device &d_;
    shim::Surface *readback_ = nullptr, *back_ = nullptr;
};

environment::State Day(float clouds, float rain_clouds = 0.0f)
{
    environment::State state = SunlitEnvironment(glm::normalize(glm::vec3(0.4f, 0.8f, 0.2f)));
    state.light_color = glm::vec3(1.0f, 0.95f, 0.85f);
    state.ambient = glm::vec3(0.2f, 0.25f, 0.35f);
    state.clouds = clouds;
    state.rain_clouds = state.overcast = rain_clouds;
    return state;
}

environment::State Night(float clouds)
{
    environment::State state = CheckEnvironment();
    state.sun_dir = glm::normalize(glm::vec3(0.3f, -0.6f, 0.2f));
    state.moon_dir = state.light_dir = glm::normalize(glm::vec3(-0.5f, 0.5f, 0.3f));
    state.light_color = glm::vec3(0.08f, 0.09f, 0.12f);
    state.ambient = glm::vec3(0.01f, 0.012f, 0.02f);
    state.fog_color = glm::vec3(0.004f, 0.005f, 0.008f);
    state.twilight = 0.0f;
    state.clouds = clouds;
    return state;
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
    SDL_Window *window = SDL_CreateWindow("sky_clouds_check", 0, 0, kSize, kSize, 0);
    if (!window || !shim::Context::instance().init(window, kSize, kSize, false))
    {
        std::fprintf(stderr, "FAIL: no window: %s\n", SDL_GetError());
        return 1;
    }
    bool ok = true;
    const auto expect = [&ok](bool condition, const char *what) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
        ok = ok && condition;
    };
    {
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        auto *device = new Device(window, params);
        {
            Harness harness(*device);
            harness.render(Day(0.0f)); // the sky's first panorama
            const Picture clear = harness.render(Day(0.0f));
            const Picture few = harness.render(Day(0.25f));
            const Picture half = harness.render(Day(0.5f));
            const Picture most = harness.render(Day(0.9f));
            std::printf("rough share: clear %.3f, cover 0.25 %.3f, 0.5 %.3f, 0.9 %.3f\n", clear.rough(0.01f),
                        few.rough(0.01f), half.rough(0.01f), most.rough(0.01f));
            expect(clear.rough(0.01f) < 0.005f, "a clear sky is smooth");
            expect(half.rough(0.01f) > 0.03f, "clouds break the sky up");
            expect(clear.difference(few) < clear.difference(half) && clear.difference(half) < clear.difference(most),
                   "the larger the cover, the more of the sky the clouds hide");

            environment::State moved = Day(0.5f);
            moved.cloud_drift = {300.0f, 0.0f};
            const Picture drifted = harness.render(moved);
            const Picture again = harness.render(Day(0.5f));
            std::printf("drift difference %.4f, same drift %.4f\n", half.difference(drifted), half.difference(again));
            expect(half.difference(drifted) > 0.01f, "the drift moves the clouds");
            expect(half.difference(again) < 0.001f, "the same drift draws the same clouds");

            effects::Host::instance().settings_.cloud_octaves = 3;
            const Picture shaded = harness.render(Day(0.5f));
            effects::Host::instance().settings_.cloud_octaves = 4;
            const Picture detailed = harness.render(Day(0.5f));
            std::printf("three octaves against two: %.4f, four against three: %.4f\n", half.difference(shaded),
                        shaded.difference(detailed));
            expect(half.difference(shaded) > 0.005f, "from three octaves the clouds are shaded towards the light");
            expect(shaded.difference(detailed) > 0.002f, "the fourth octave adds detail");
            effects::Host::instance().settings_.cloud_octaves = 2;

            // The rain clouds' panorama (overcast) is the sky's next state.
            const Picture rain = harness.render(Day(0.0f, 1.0f));
            std::printf("blueness: clear %.2f, rain clouds %.2f\n", clear.blueness(), rain.blueness());
            expect(rain.blueness() < clear.blueness() - 0.3f && rain.blueness() < 1.25f,
                   "the rain clouds' full cover is grey");
            expect(rain.rough(0.03f) < 0.02f, "the rain clouds' full cover is even");

            // The night's panorama is computed on a thread of its own: a few frames until the stars come out.
            int starry = 0;
            for (int frame = 0; frame < 100 && starry <= 10; ++frame)
            {
                starry = harness.render(Night(0.0f), 3.0f).points();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const int cloudy = harness.render(Night(1.0f), 3.0f).points();
            std::printf("stars: clear %d, clouded %d\n", starry, cloudy);
            expect(starry > 10 && cloudy * 3 < starry, "clouds hide the stars");
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
