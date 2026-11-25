// The arithmetic of the hulls' wakes in the Sea2 pixel shader (shaders/include/sea_wakes_math.sh, the file
// sea_wakes.sh includes): the wakes' reach, the lace's two readings of the foam texture, its cover and its light.
#include "../../tests/check.hpp"
#include "shader_scalars.hpp"

#include <cmath>
#include <cstdio>

namespace shader
{
#include "../../shaders/include/sea_wakes_math.sh"
} // namespace shader

int main()
{
    // The wakes' reach: everything around the camera, nothing at the target's border or beyond it in any
    // direction, by the distance from the centre alone.
    CHECK(shader::seaWakeReach(0.5f, 0.5f) == 1.0f && shader::seaWakeReach(0.5f, 0.9f) == 1.0f);
    CHECK(shader::seaWakeReach(0.0f, 0.5f) == 0.0f && shader::seaWakeReach(0.5f, 1.0f) == 0.0f);
    CHECK(shader::seaWakeReach(0.1f, 0.1f) == 0.0f && shader::seaWakeReach(0.87f, 0.87f) == 0.0f);
    CHECK(shader::seaWakeReach(0.5f, 0.97f) > 0.0f && shader::seaWakeReach(0.5f, 0.97f) < 1.0f);
    CHECK(std::fabs(shader::seaWakeReach(0.5f, 0.97f) - shader::seaWakeReach(0.97f, 0.5f)) < 1e-5f);
    CHECK(std::fabs(shader::seaWakeReach(0.5f, 0.97f) -
                    shader::seaWakeReach(0.5f + 0.47f * 0.7071068f, 0.5f + 0.47f * 0.7071068f)) < 1e-4f);

    // The foam's light: without the sun never more than the sky that lights it, in any channel; with the sun,
    // brighter; never over the albedo.
    for (const float sky : {0.05f, 0.35f, 0.9f})
        for (const float channel : {0.6f, 1.0f, 1.3f})
        {
            const float lit = shader::seaFoamLight(sky * channel, sky, 0.0f);
            CHECK(lit <= std::max(sky, sky * channel) && lit > 0.0f);
            CHECK(shader::seaFoamLight(sky * channel, sky, 0.6f) > lit || lit >= kSeaFoamAlbedo - 1e-6f);
            CHECK(shader::seaFoamLight(sky * channel, sky, 2.0f) <= kSeaFoamAlbedo + 1e-6f);
        }

    // The finer reading of the foam texture is the same pattern when the first reading's coordinate moves by whole
    // repeats (the sea's vertex origin passing a repeat of the texture).
    for (const float u : {0.0f, 0.13f, 0.5f, 0.999f})
        for (const float repeats : {1.0f, -1.0f, 37.0f})
        {
            const float a = shader::seaFoamFine(u, 0.37f), b = shader::seaFoamFine(u + repeats, 0.37f);
            CHECK(std::fabs((b - a) - std::round(b - a)) < 1e-4f);
        }

    // The wake's foam: none without foam, more with more foam and on the texture's brighter streaks, never opaque.
    for (int s = 0; s <= 10; ++s)
    {
        const float streak = static_cast<float>(s) / 10.0f;
        CHECK(shader::seaWakeLace(streak, 0.0f) == 0.0f);
        for (int f = 1; f <= 10; ++f)
        {
            const float foam = static_cast<float>(f) / 10.0f;
            CHECK(shader::seaWakeLace(streak, foam) >= shader::seaWakeLace(streak, foam - 0.1f));
            CHECK(shader::seaWakeLace(streak, foam) <= 0.8f);
            if (s > 0)
                CHECK(shader::seaWakeLace(streak, foam) >= shader::seaWakeLace(streak - 0.1f, foam));
        }
    }
    CHECK(shader::seaWakeLace(1.0f, 1.0f) == 0.8f && shader::seaWakeAerated(0.0f) == 0.0f);

    std::printf("sea wakes math checks: %s\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
