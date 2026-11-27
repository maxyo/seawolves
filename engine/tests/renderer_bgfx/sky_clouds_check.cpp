// The drawn sky (fx::Sky::Draw, fs_fx_sky.sc) from the scene environment, on OpenGL ES and Vulkan in an SDL window:
//   * Caribbean Legend's atmosphere, its haze band, the height fog over the sky and the sun's and the moon's discs draw
//     the values worked out from its formulas apart from this code (a pixel within 3 of 255 per channel);
//   * a clear sky is smooth; clouds break it up, the more of it the larger their cover;
//   * the rain clouds' full cover is an even grey, less blue than the clear sky, lit as the textured rain layer;
//   * the wind's drift moves the clouds; the same drift draws the same picture;
//   * at night clouds hide the stars (seen through a narrow view: a star covers a pixel);
//   * three octaves shade the clouds towards the light, a fourth adds detail;
//   * Caribbean Legend's textured layers draw its lighting's values worked out apart from this code: the base layer
//     over the upper one at the hour's opacity, the rain layer's full cover in place of the sky, its reveal growing
//     with its cover.
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

    // The sky straight overhead (or towards +x along the horizon), 90 degrees across (`zoom` times narrower), under
    // `state`.
    Picture render(const environment::State &state, float zoom = 1.0f, bool east = false)
    {
        check(d_.BeginScene(), "BeginScene");
        check(d_.Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
        D3DMATRIX view{};
        if (east) // world x to the view's z, y up, -z to the right
        {
            view.m[0][2] = 1.0f;
            view.m[1][1] = 1.0f;
            view.m[2][0] = -1.0f;
        }
        else // world y to the view's z: looking up
        {
            view.m[0][0] = 1.0f;
            view.m[1][2] = 1.0f;
            view.m[2][1] = -1.0f;
        }
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
    state.light_color = state.sun_light = glm::vec3(1.0f, 0.95f, 0.85f);
    state.fill_color = glm::vec3(1.0f);
    state.fill_intensity = 1.0f;
    state.clouds = clouds;
    state.rain_clouds = state.overcast = rain_clouds;
    return state;
}

// A clear noon of the engine's own radiances, no haze, no fog, nothing on the sun's disc.
environment::State Noon()
{
    environment::State state = SunlitEnvironment(glm::normalize(glm::vec3(0.3f, 0.95f, 0.0f)));
    state.fog_density = 0.0f;
    return state;
}

// A pixel within 3 of 255 of the expected 8-bit colour.
bool Pixel(const Picture &picture, uint32_t x, uint32_t y, glm::vec3 expected)
{
    const glm::vec3 got = picture.at(x, y) * 255.0f;
    std::printf("  pixel %u,%u: %.0f %.0f %.0f, expected %.0f %.0f %.0f\n", x, y, got.r, got.g, got.b, expected.r,
                expected.g, expected.b);
    return glm::all(glm::lessThanEqual(glm::abs(got - expected), glm::vec3(3.0f)));
}

// A cube of 4 x 4 faces, every texel `rgba`.
bgfx::TextureHandle Cube(std::array<uint8_t, 4> rgba)
{
    std::vector<uint8_t> texels(6 * 4 * 4 * 4);
    for (size_t i = 0; i < texels.size(); i += 4)
        std::copy(rgba.begin(), rgba.end(), texels.begin() + std::ptrdiff_t(i));
    return bgfx::createTextureCube(4, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                   bgfx::copy(texels.data(), uint32_t(texels.size())));
}

// A clear noon whose clouds are lit by a white sun and the day's fill, a third as bright as Caribbean Legend's, half of
// the base and upper layers shown.
environment::State CloudNoon()
{
    environment::State state = Noon();
    state.sun_light = {1.0f, 0.95f, 0.85f};
    state.fill_color = glm::vec3(1.0f);
    state.fill_intensity = 1.0f;
    state.cloud_look.intensity = 0.3f;
    state.cloud_look.opacity = 0.5f;
    state.cloud_look.twilight_tint = {0.42f, 0.51f, 0.68f};
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
            const Picture zenith = harness.render(Noon());
            expect(Pixel(zenith, 127, 127, {125.4f, 144.5f, 181.1f}) &&
                       Pixel(zenith, 128, 128, {126.2f, 145.1f, 181.5f}),
                   "the noon zenith is Caribbean Legend's atmosphere");

            environment::State hazy = Noon(); // Caribbean Legend's band, as «Каждому своё» takes it
            hazy.haze = 1.0f;
            hazy.haze_height = std::sin(glm::radians(8.65f));
            hazy.haze_sample = glm::radians(1.42f);
            hazy.haze_from_sky = true;
            hazy.haze_color = glm::vec3(1.0f);
            const Picture band = harness.render(hazy, 1.0f, true);
            expect(Pixel(band, 128, 127, {197.4f, 213.1f, 227.0f}) && Pixel(band, 128, 118, {188.2f, 209.7f, 232.2f}) &&
                       Pixel(band, 128, 90, {133.4f, 169.8f, 224.1f}),
                   "the haze band at the horizon: the sky's colour 1.42 degrees up, fading out by 8.65");

            environment::State foggy = Noon();
            foggy.fog_density = 1.0f / 500.0f;
            foggy.fog_layer = 200.0f;
            foggy.fog_end = 5000.0f;
            foggy.fog_max = 0.9f;
            foggy.fog_color = {0.2f, 0.3f, 0.4f};
            const Picture fog = harness.render(foggy, 1.0f, true);
            expect(Pixel(fog, 128, 127, {122.8f, 143.0f, 160.7f}) && Pixel(fog, 128, 100, {112.3f, 138.7f, 166.2f}) &&
                       Pixel(fog, 128, 60, {112.4f, 140.2f, 176.2f}),
                   "the height fog over the sky thins upwards and stops at its most opacity");

            // The discs: a white sprite, the sun's tinted by its light, the moon's white at night.
            const std::array<uint8_t, 4 * 4 * 4> white = [] {
                std::array<uint8_t, 4 * 4 * 4> w;
                w.fill(255);
                return w;
            }();
            const bgfx::TextureHandle sprite = bgfx::createTexture2D(4, 4, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                                                     bgfx::copy(white.data(), uint32_t(white.size())));
            render::SetSkyDiscs(sprite.idx, sprite.idx);
            environment::State sunny = Noon();
            sunny.sun_light = {0.5f, 0.4f, 0.3f};
            sunny.sun_size = 0.12f;
            const Picture sun = harness.render(sunny);
            expect(Pixel(sun, 168, 127, {195.9f, 195.4f, 208.4f}), "the sun's sprite in its light over the sky");
            environment::State moonlit = CheckEnvironment();
            moonlit.twilight = 0.0f;
            moonlit.fog_density = 0.0f;
            moonlit.moon_dir = glm::normalize(glm::vec3(-0.3f, 0.95f, 0.0f));
            moonlit.moon_size = 0.075f;
            const Picture moon = harness.render(moonlit);
            expect(Pixel(moon, 88, 127, {197.5f, 197.9f, 198.3f}), "the moon's sprite white over the night's sky");
            render::SetSkyDiscs(0xffff, 0xffff);
            bgfx::destroy(sprite);

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

            const Picture rain = harness.render(Day(0.0f, 1.0f));
            std::printf("blueness: clear %.2f, rain clouds %.2f\n", clear.blueness(), rain.blueness());
            expect(rain.blueness() < clear.blueness() - 0.3f && rain.blueness() < 1.25f,
                   "the rain clouds' full cover is grey");
            expect(rain.rough(0.03f) < 0.02f, "the rain clouds' full cover is even");
            environment::State overcast = CloudNoon();
            overcast.rain_clouds = 1.0f;
            expect(Pixel(harness.render(overcast), 128, 128, {125.6f, 125.5f, 126.0f}),
                   "without textures the rain clouds are lit as Caribbean Legend's rain layer");

            // Caribbean Legend's textured layers: cubes of one texel colour each, the base layer's 60 % opaque over
            // the upper's opaque one, the rain layer's opaque.
            const std::array<bgfx::TextureHandle, 6> cubes = {Cube({26, 77, 26, 153}), Cube({26, 89, 26, 0}),
                                                              Cube({51, 51, 51, 255}), Cube({51, 51, 51, 0}),
                                                              Cube({8, 65, 8, 255}),   Cube({8, 92, 8, 0})};
            fx::CloudLayers layers;
            layers.base = {cubes[0].idx, cubes[1].idx, 4.0f, 1.0f, false};
            layers.upper = {cubes[2].idx, cubes[3].idx, 1.0f, 1.0f, false};
            layers.rain = {cubes[4].idx, cubes[5].idx, 4.0f, 1.0f, false};
            layers.ambient_strength = 0.15f;
            render::SetSkyClouds(layers);
            const Picture layered = harness.render(CloudNoon());
            expect(Pixel(layered, 128, 128, {137.3f, 146.4f, 165.9f}),
                   "the base layer over the upper one, both shown by the hour's opacity, lit as Caribbean Legend's");
            environment::State raining = CloudNoon();
            raining.rain_clouds = 1.0f;
            expect(Pixel(harness.render(raining), 128, 128, {125.8f, 125.7f, 126.3f}),
                   "the rain layer's full cover replaces the sky");
            // Part of a cover: the reveal shows the rain layer over a share of the sky that grows with it.
            const auto rained = [&](float cover) {
                environment::State state = CloudNoon();
                state.rain_clouds = cover;
                const Picture part = harness.render(state);
                int count = 0;
                for (uint32_t y = 0; y < kSize; y += 2)
                    for (uint32_t x = 0; x < kSize; x += 2)
                        count += glm::length(part.at(x, y) - layered.at(x, y)) > 0.02f;
                return float(count) / float(kSize * kSize / 4);
            };
            const float little = rained(0.2f), half_rain = rained(0.5f);
            std::printf("rain layer share: cover 0.2 %.2f, 0.5 %.2f\n", little, half_rain);
            expect(little < half_rain && half_rain > 0.25f && half_rain < 0.85f,
                   "the rain clouds' reveal grows with their cover");
            render::SetSkyClouds({});
            for (const bgfx::TextureHandle cube : cubes)
                bgfx::destroy(cube);

            // A view narrow enough that a pixel spans fewer than four of the stars' cells.
            const int starry = harness.render(Night(0.0f), 8.0f).points();
            const int cloudy = harness.render(Night(1.0f), 8.0f).points();
            std::printf("stars: clear %d, clouded %d\n", starry, cloudy);
            expect(starry > 10 && cloudy * 3 < starry, "clouds hide the stars");
        }
        device->Release();
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
