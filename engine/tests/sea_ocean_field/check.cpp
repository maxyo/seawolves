// The sea's wave spectrum and field (src/libs/sea/src/ocean_*): the physics of a single wave, the spectrum's
// normalisation and bands, the transform's scale, the height query over choppy waves and determinism.
#include "ocean_driver.hpp"
#include "ocean_field.hpp"

#include <catch2/catch.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <vector>

#include <glm/gtc/packing.hpp>
#include <numbers>

using namespace storm::sea::ocean;

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

SeaState Weather(float amp1, float scale1, float amp2, float scale2, float pos_shift, float cap = 200.0f)
{
    return MapSea2({amp1, scale1, amp2, scale2, pos_shift, cap, 0.7f});
}

double FieldVariance(const Field &field)
{
    double variance = 0.0;
    for (size_t c = 0; c < field.Layout().size(); c++)
    {
        double sum = 0.0;
        for (const Displacement &d : field.Samples(c))
            sum += static_cast<double>(d.h) * d.h;
        variance += sum / static_cast<double>(field.Samples(c).size());
    }
    return variance;
}
} // namespace

TEST_CASE("a single wave travels along k at the deep-water speed and leans towards its crest")
{
    // One wave of 16 m and amplitude 2 m along +x on a 64 m patch: h = 2 cos(k x - w t), w = sqrt(g k).
    constexpr int n = 64;
    const float k = 2.0f * kPi / 16.0f, omega = std::sqrt(kGravity * k), period = 2.0f * kPi / omega;
    CascadeSpectrum spectrum;
    spectrum.amplitude.assign(n * n, {});
    spectrum.omega.assign(n * n, 0.0f);
    spectrum.amplitude[4] = 1.0f; // a(k) = amplitude / 2: the opposite sample adds the conjugate
    spectrum.omega[4] = spectrum.omega[n - 4] = omega;
    Field field({{n, 64.0f, 0.0f, 100.0f}});
    field.SetSpectra({spectrum}, 0.5f, 0.0f);

    field.Update(0.0);
    CHECK(field.Offset(0.0f, 3.0f, 1).y == Approx(2.0f).margin(1e-4));
    CHECK(field.Offset(4.0f, 3.0f, 1).y == Approx(0.0f).margin(1e-4));
    // Gerstner's lean: the water just past a crest moves back towards it, by choppiness * amplitude * sin.
    CHECK(field.Offset(2.0f, 0.0f, 1).x == Approx(-0.5f * 2.0f * std::sin(kPi / 4.0f)).margin(1e-4));
    CHECK(field.Offset(2.0f, 0.0f, 1).z == Approx(0.0f).margin(1e-4));

    field.Update(period / 4.0f);
    CHECK(field.Offset(4.0f, 9.0f, 1).y == Approx(2.0f).margin(1e-3));
    CHECK(field.Offset(0.0f, 9.0f, 1).y == Approx(0.0f).margin(1e-3));

    // The normal on the slope halfway down (no lean: the plain height field): its tilt is the slope, 2 k.
    field.SetSpectra({spectrum}, 0.0f, 0.0f);
    field.Update(0.0);
    glm::vec3 normal;
    field.Height(4.0f, 1.0f, 1, &normal);
    CHECK(normal.x / normal.y == Approx(2.0f * k).epsilon(0.03));
    CHECK(normal.z == Approx(0.0f).margin(1e-4));
    // The pixel shader's slopes: the height's derivative along x there, -2 k sin(k x), by central differences.
    const Slope &slope = field.Slopes(0)[static_cast<size_t>(1) * n + 4];
    CHECK(slope.hx == Approx(-2.0f * k).epsilon(0.03));
    CHECK(slope.hz == Approx(0.0f).margin(1e-4));
}

TEST_CASE("a weather change eases from the old sea to the new one")
{
    const SeaState calm = Weather(4.0f, 0.6f, 1.0f, 2.0f, 1.0f), rough = Weather(30.0f, 0.15f, 2.0f, 1.5f, 2.0f);
    Field easing(CascadeLayout(2, 64)), old_sea(CascadeLayout(2, 64)), new_sea(CascadeLayout(2, 64));
    easing.SetState(calm);
    old_sea.SetState(calm);
    new_sea.SetState(rough);
    easing.EaseTo(BuildSpectrum(rough, easing.Layout()), rough.choppiness, rough.mean_level);
    const auto same = [](const Field &a, const Field &b) {
        return std::memcmp(a.Samples(0).data(), b.Samples(0).data(), a.Samples(0).size() * sizeof(Displacement)) == 0;
    };
    // At the start the sea is the old one, halfway every height lies between, at the end it is the new one.
    easing.Update(12.0);
    old_sea.Update(12.0);
    new_sea.Update(12.0);
    CHECK(same(easing, old_sea));
    CHECK(easing.MeanLevel() == old_sea.MeanLevel());
    easing.Ease(0.5f);
    easing.Update(12.0);
    // Choppiness eases too, so the halfway field is not the plain mean of the two; the still level is.
    CHECK(easing.MeanLevel() == Approx(0.5f * (calm.mean_level + rough.mean_level)));
    double between = 0.0;
    for (size_t i = 0; i < easing.Samples(0).size(); i++)
    {
        const float a = old_sea.Samples(0)[i].h, b = new_sea.Samples(0)[i].h, h = easing.Samples(0)[i].h;
        between += std::abs(h - 0.5f * (a + b)) <= 0.05f * (std::abs(a) + std::abs(b)) + 1e-3f ? 1.0 : 0.0;
    }
    CHECK(between / static_cast<double>(easing.Samples(0).size()) > 0.99);
    easing.Ease(1.0f);
    CHECK(!easing.Easing());
    easing.Update(12.0);
    CHECK(same(easing, new_sea));
}

TEST_CASE("each wave system has the game's height and every wave lives in one cascade")
{
    const SeaState states[] = {Weather(6.0f, 0.8f, 1.0f, 2.0f, 1.8f), Weather(100.0f, 0.08f, 3.0f, 1.3f, 0.3f),
                               Weather(30.0f, 0.15f, 1.0f, 2.0f, 1.0f, 2.0f)};
    for (const auto &[count, size] : {std::pair{2, 64}, std::pair{3, 64}, std::pair{3, 128}})
    {
        const std::vector<Cascade> layout = CascadeLayout(count, size);
        for (size_t c = 0; c + 1 < layout.size(); c++)
        {
            CHECK(layout[c].k_high == layout[c + 1].k_low);
            CHECK(layout[c].patch > layout[c + 1].patch);
        }
        for (const SeaState &state : states)
        {
            const auto spectra = BuildSpectrum(state, layout);
            const double target = (state.swell.hs * state.swell.hs + state.wind.hs * state.wind.hs) / 16.0;
            CHECK(HeightVariance(spectra) == Approx(target).epsilon(1e-4));
            for (size_t c = 0; c < layout.size(); c++)
            {
                const int n = layout[c].size;
                const double dk = 2.0 * std::numbers::pi / layout[c].patch;
                for (int y = 0; y < n; y++)
                    for (int x = 0; x < n; x++)
                        if (spectra[c].amplitude[static_cast<size_t>(y) * n + x] != std::complex<float>())
                        {
                            const double length = dk * std::hypot(x < n / 2 ? x : x - n, y < n / 2 ? y : y - n);
                            REQUIRE(length >= layout[c].k_low);
                            REQUIRE(length < layout[c].k_high);
                        }
            }
        }
    }
    // The harbour's cap scales the heights and the raised still level alike.
    const SeaState open = Weather(30.0f, 0.15f, 1.0f, 2.0f, 1.0f),
                   harbour = Weather(30.0f, 0.15f, 1.0f, 2.0f, 1.0f, 2.0f);
    CHECK(harbour.swell.hs / open.swell.hs == Approx(2.0f / 31.0f));
    CHECK(harbour.mean_level / open.mean_level == Approx(2.0f / 31.0f));
    CHECK(Weather(6.0f, 0.1f, 1.0f, 2.0f, 1.0f).swell.peak_wavelength >
          Weather(6.0f, 0.8f, 1.0f, 2.0f, 1.0f).swell.peak_wavelength);
}

TEST_CASE("the transformed field carries the spectrum's energy")
{
    Field field(CascadeLayout(3, 128));
    field.SetState(Weather(7.0f, 0.5f, 1.1f, 3.0f, 2.0f));
    const double expected = HeightVariance(BuildSpectrum(Weather(7.0f, 0.5f, 1.1f, 3.0f, 2.0f), field.Layout()));
    double mean = 0.0;
    constexpr int kTimes = 16;
    for (int i = 0; i < kTimes; i++)
    {
        field.Update(i * 7.3);
        mean += FieldVariance(field) / kTimes;
    }
    CHECK(mean == Approx(expected).epsilon(0.15));
}

TEST_CASE("the height over a point is that of the displaced water that lands there")
{
    Field field(CascadeLayout(3, 128));
    const SeaState state = Weather(25.0f, 0.17f, 1.0f, 4.0f, 2.5f);
    REQUIRE(state.choppiness == Approx(0.8f)); // the most any weather asks
    field.SetState(state);
    field.Update(41.0);
    float worst = 0.0f;
    for (int i = 0; i < 200; i++)
    {
        const float x = -3000.0f + 31.7f * i, z = 1200.0f - 17.3f * i;
        const glm::vec3 offset = field.Offset(x, z, 3);
        const float height = field.Height(x + offset.x, z + offset.z, 3);
        worst = std::max(worst, std::abs(height - (offset.y + state.mean_level)));
    }
    CHECK(worst < 0.002f);
}

TEST_CASE("equal inputs make the same field")
{
    const SeaState state = Weather(17.0f, 0.3f, 1.0f, 2.0f, 1.8f);
    Field a(CascadeLayout(3, 64)), b(CascadeLayout(3, 64));
    a.SetState(state);
    b.SetState(state);
    a.Update(1234.5);
    b.Update(1234.5);
    for (size_t c = 0; c < 3; c++)
        CHECK(std::memcmp(a.Samples(c).data(), b.Samples(c).data(), a.Samples(c).size() * sizeof(Displacement)) == 0);
    b.Update(1234.6);
    CHECK(std::memcmp(a.Samples(0).data(), b.Samples(0).data(), a.Samples(0).size() * sizeof(Displacement)) != 0);

    Field calm(CascadeLayout(2, 64));
    calm.SetState(Weather(0.0f, 0.5f, 0.0f, 2.0f, 1.0f));
    calm.Update(5.0);
    CHECK(calm.Height(10.0f, 20.0f, 2) == 0.0f);
}

TEST_CASE("the vertex texture holds exactly the samples the CPU reads")
{
    Field field(CascadeLayout(3, 64));
    field.SetState(Weather(17.0f, 0.3f, 1.0f, 2.0f, 1.8f));
    field.Update(77.7);
    for (size_t c = 0; c < 3; c++)
    {
        const std::vector<uint64_t> &halves = field.SampleHalves(c);
        REQUIRE(halves.size() == field.Samples(c).size());
        for (size_t i = 0; i < halves.size(); i++)
        {
            const glm::vec4 texel = glm::unpackHalf4x16(halves[i]);
            const Displacement &d = field.Samples(c)[i];
            REQUIRE((texel.x == d.dx && texel.y == d.h && texel.z == d.dz));
        }
    }
}

TEST_CASE("a slope level holds the mean slope and mean square slope of the samples it covers")
{
    // 4 x 4 samples: along x the slope alternates +1 / -1 column by column; along z it is 0.5 everywhere, with the
    // choppy displacement squeezing x by half in the first row (twice as steep) and past a fold in the second
    // (steepening capped at four times).
    constexpr int n = 4;
    std::vector<Slope> slopes(n * n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            slopes[y * n + x] = {(x % 2) ? -1.0f : 1.0f, 0.5f, y == 0 ? -0.5f : y == 1 ? -2.0f : 0.0f, 0.0f};
    std::vector<uint64_t> moments;
    PackSlopeMoments(slopes, n, moments);
    REQUIRE(SlopeLevels(n) == 3);
    REQUIRE(moments.size() == SlopeLevelOffset(n, 3));
    REQUIRE(moments.size() == 16 + 4 + 1);
    const auto texel = [&](int level, int x, int y) {
        return glm::unpackHalf4x16(moments[SlopeLevelOffset(n, level) + y * (n >> level) + x]);
    };
    CHECK(texel(0, 0, 0).x == Approx(2.0f));
    CHECK(texel(0, 1, 1).x == Approx(-4.0f));
    CHECK(texel(0, 1, 1).z == Approx(16.0f));
    CHECK(texel(0, 2, 3).y == Approx(0.5f));
    CHECK(texel(0, 2, 3).w == Approx(0.25f));
    // Level 1, texel (0, 0): rows 0 and 1, columns 0 and 1: slopes 2, -2, 4, -4.
    CHECK(texel(1, 0, 0).x == Approx(0.0f).margin(1e-3));
    CHECK(texel(1, 0, 0).z == Approx((4.0f + 4.0f + 16.0f + 16.0f) / 4.0f));
    // The single top texel: the mean slope 0 along x, 0.5 along z; along z no variance hides.
    const glm::vec4 top = texel(2, 0, 0);
    CHECK(top.x == Approx(0.0f).margin(1e-3));
    CHECK(top.z == Approx((4 * 4.0f + 4 * 16.0f + 8 * 1.0f) / 16.0f));
    CHECK(top.w - top.y * top.y == Approx(0.0f).margin(1e-3));
}

TEST_CASE("the transform runs only on buffers aligned for av_tx's vector code")
{
    InverseFft2d transform(16);
    FftBuffer data(16 * 16 + 1);
    REQUIRE(reinterpret_cast<std::uintptr_t>(data.data()) % kFftAlignment == 0);
    transform.Run(data.data());
    CHECK_THROWS_AS(transform.Run(data.data() + 1), std::invalid_argument);
}

TEST_CASE("a sea deleted while its worker makes the next field waits for the worker")
{
    const Sea2Keys keys{7.0f, 0.5f, 1.1f, 3.0f, 2.0f, 200.0f, 0.7f};
    for (int i = 0; i < 8; i++)
    {
        Driver driver;
        CHECK(driver.Frame(3, 128, keys, 0.02, true)); // made at once, the worker starts on the next field
        (void)driver.Frame(3, 128, keys, 0.02, false); // the driver goes while the worker may still run
    }
}

TEST_CASE("a frame never waits for the worker: until the next field is made the frame keeps the one it drew")
{
    const Sea2Keys keys{7.0f, 0.5f, 1.1f, 3.0f, 2.0f, 200.0f, 0.7f};
    Driver driver;
    REQUIRE(driver.Frame(3, 128, keys, 0.02, true));
    const auto heights = [&] {
        std::vector<float> h;
        for (const Displacement &d : driver.Current().Samples(2))
            h.push_back(d.h);
        return h;
    };
    // The first field the worker makes is at the clock of the one made at once; the second is one frame later.
    int late = 0;
    for (int fresh = 0; fresh < 2;)
    {
        const std::vector<float> drawn = heights();
        if (driver.Frame(3, 128, keys, 0.02, false))
        {
            fresh++;
            if (fresh == 2)
                CHECK(heights() != drawn);
        }
        else
        {
            late++;
            REQUIRE(heights() == drawn);
        }
    }
    CHECK(late > 0);
}
