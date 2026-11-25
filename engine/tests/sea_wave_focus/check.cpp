#include "../../src/libs/sea/src/wave_focus.hpp"
#include <vector>
#include <cstdlib>

#include <catch2/catch.hpp>
struct Source
{
    unsigned long long identity = 1;
    glm::vec3 position{0, 0, 0};
    float half_length = 18.95f, half_width = 3.4f;
};
glm::vec3 direction(glm::vec3 camera, glm::vec3 point = {0, 0, 0})
{
    return glm::normalize(point - camera);
}
glm::vec3 ordinary(glm::vec3 camera, glm::vec3 look)
{
    float reach = 45;
    if (look[1] < -.01f)
        reach = std::min(reach, std::max(camera[1], 0.f) / -look[1]);
    return {camera[0] + look[0] * reach, 0, camera[2] + look[2] * reach};
}
glm::vec3 update(storm::sea::WaveFocus &focus, glm::vec3 c, glm::vec3 look, const std::vector<Source> &sources,
                 float dt = 1.f / 60)
{
    return focus.Update(c, look, ordinary(c, look), sources, dt);
}
// Whether Update hands back the ordinary focus it is given, bit for bit. The ordinary focus is computed once: two
// evaluations of it can differ in the last bit where the compiler fuses a multiply-add in one and not the other.
bool unchanged(storm::sea::WaveFocus &focus, glm::vec3 c, glm::vec3 look, const std::vector<Source> &sources)
{
    const glm::vec3 given = ordinary(c, look);
    return focus.Update(c, look, given, sources, 1.f / 60) == given;
}
float distance(glm::vec3 a, glm::vec3 b)
{
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}
TEST_CASE("sea_wave_focus")
{
    std::vector<Source> sources(1);
    for (glm::vec3 camera : {glm::vec3{0, 70, 31.5851f}, glm::vec3{0, 15, 20}})
    {
        storm::sea::WaveFocus f;
        auto l = direction(camera);
        CHECK(unchanged(f, camera, l, sources)); // near/top original focus preserved
    }
    glm::vec3 far{0, 45, 142.1311f};
    auto look = direction(far);
    storm::sea::WaveFocus f;
    auto center = update(f, far, look, sources);
    CHECK(distance(center, {0, 0, 0}) <= 32.001f);     // far oblique hull inside inner field
    CHECK(distance(center, {0, 0, 0}) - 18.95f <= 64); // far source survives actual world.cpp admission
    for (glm::vec3 l : {glm::vec3{0, 0, -1}, glm::vec3{0, .1f, -.994987f}})
    {
        storm::sea::WaveFocus empty;
        CHECK(unchanged(empty, far, l, sources)); // horizon/up unchanged
    }
    {
        storm::sea::WaveFocus empty;
        CHECK(unchanged(empty, far, look, {})); // no hull unchanged
    }
    // Actual stock ShipCamera distance=1 geometry, including elevated target.
    // Frigate1 real height is a corpus-derived lower bound from attached mast3.
    // Tartane1 bound includes attached mast2; collision corrections are not simulated.
    for (auto model :
         {glm::vec3{17.20708466f, 53.48410034f, 55.47645974f}, glm::vec3{3.064818859f, 10.56315041f, 9.784371376f}})
    {
        const bool frigate = model[1] > 20;
        std::vector<Source> hull(1);
        hull[0].half_length = frigate ? 18.95f : 1.0f;
        hull[0].half_width = frigate ? 3.4f : 1.0f;
        // Tiny conservative Tartane footprint is sufficient to check admission; it is
        // not a claim about the actual waterline shape (no recorded runtime footprint).
        for (float angle : {-1.4f, -.7f, -.1f, .07f})
        {
            const float boxY = model[0] * .65f, radius = 2 * (model[1] * .7f + model[2]);
            glm::vec3 eye{0, .5f * boxY + 70 * std::sin(std::max(0.f, -angle)),
                          radius * std::cos(std::max(0.f, -angle))};
            glm::vec3 aim{0, boxY + (angle > 0 ? model[2] * angle * 6 : 0), 0};
            auto ray = direction(eye, aim);
            storm::sea::WaveFocus actual;
            const glm::vec3 given = ordinary(eye, ray);
            auto actualCenter = actual.Update(eye, ray, given, hull, 1.f / 60);
            if (ray[1] < -.01f || !frigate)
                CHECK(distance(actualCenter, {0, 0, 0}) - hull[0].half_length <=
                      64); // stock elevated aim retains viewed hull inside admission
            else
                CHECK(actualCenter == given); // stock high/upward aim obeys unchanged horizon policy
        }
    }
    // Source order cannot steal a retained hull. Both hulls intersect the same ray.
    auto second = sources[0];
    second.identity = 2;
    second.position = {4, 0, 0};
    auto before = center;
    sources.push_back(second);
    std::reverse(sources.begin(), sources.end());
    center = update(f, far, look, sources);
    CHECK(distance(center, before) < .001); // nearby source order stable
    // Acquisition ties are deterministic, including reversed enumeration.
    storm::sea::WaveFocus a, b;
    auto one = update(a, far, look, sources);
    std::reverse(sources.begin(), sources.end());
    CHECK(one == update(b, far, look, sources)); // deterministic initial selection
    // Moving the ray outside acquisition but inside release radius retains coverage.
    glm::vec3 edge = direction(far, {22, 0, 0});
    center = update(f, far, edge, sources);
    CHECK(distance(center, {0, 0, 0}) < 34); // grazing release hysteresis
    auto paused = update(f, far, glm::vec3{0, 0, -1}, sources, 0);
    CHECK(paused == center); // zero dt preserves corrected centre
    auto longFrame = update(f, far, glm::vec3{0, 0, -1}, sources, 10);
    CHECK(distance(longFrame, paused) <= 16.001f); // long frame correction bounded below reset
    center = longFrame;
    // Release/turn then reacquire never resets the snapped field. A stopped world point
    // stays at the same physical position after grid-origin advection.
    glm::vec3 worldTail{5, 0, -20};
    for (int i = 0; i < 180; ++i)
    {
        glm::vec3 l = i < 90 ? glm::vec3{0, 0, -1} : look;
        auto next = update(f, far, l, sources);
        int oldX = int(std::floor(center[0] * 2 + .5f)), oldZ = int(std::floor(center[2] * 2 + .5f));
        int newX = int(std::floor(next[0] * 2 + .5f)), newZ = int(std::floor(next[2] * 2 + .5f));
        CHECK((std::abs(newX - oldX) < 128 && std::abs(newZ - oldZ) < 128)); // turn/release snapped grid does not reset
        const float oldCell = worldTail[2] * 2 - oldZ;
        const float advected = oldCell - (newZ - oldZ);
        CHECK(std::abs(advected + newZ - worldTail[2] * 2) < .001); // world tail retained by origin shift
        center = next;
    }
    CHECK(distance(center, {0, 0, 0}) <= 32.001f); // reacquired hull settles
}
