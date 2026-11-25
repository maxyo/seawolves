// The ship wakes without the GPU: the track of fx::WakeTrails (wake_trail.hpp) driven as the renderer drives it, a
// frame at a time in game seconds, and the foam the pixel shader makes of it (shaders/include/fx_wake_trail.sh, the
// file fs_fx_wake.sc includes).
#include "storm/fx/wake_trail.hpp"

#include "../../tests/check.hpp"
#include "shader_scalars.hpp"

#include <array>
#include <cstdio>
#include <vector>

namespace shader
{
#include "../shaders/include/fx_wake_trail.sh"
} // namespace shader

using storm::fx::WakeTrails;
using storm::fx::WakeVertex;
using storm::fx::WaveSource;

namespace
{
constexpr float kExtent = 512.0f;
constexpr float kFoamLife = WakeTrails::kFoamLife;

// A hull 32 m long and 8 m wide at (x, z), heading `heading` radians from +z towards +x.
WaveSource Hull(double x, double z, float speed, uint64_t identity = 7, float heading = 0.0f)
{
    WaveSource s;
    s.identity = identity;
    s.position = {x, z};
    s.forward = {std::sin(heading), std::cos(heading)};
    s.half_length = 16.0f;
    s.half_width = 4.0f;
    s.stern = -16.0f;
    s.stem = 16.0f;
    s.station_half.fill(3.0f);
    s.speed = speed;
    return s;
}

// The foam on the centre line of a trail at each of its sections.
std::vector<float> Foam(const std::vector<WakeVertex> &ribbon)
{
    std::vector<float> foam;
    for (size_t i = 0; i < ribbon.size(); i += 2)
    {
        const WakeVertex &v = ribbon[i];
        foam.push_back(shader::fxWakeTrailFoam(1.0f, 0.0f, v.strength, v.age, 1.0f, kFoamLife));
    }
    return foam;
}

struct Sailing
{
    WakeTrails trails;
    double now = 0.0, x = 0.0, z = 0.0;
    float speed = 0.0f, heading = 0.0f, turn = 0.0f; // m/s, radians, radians a second
    // The waterline as the game reports it: solved every tenth of a second, the stem and the beam moving with the
    // hull's pitch and heel by up to `pitch` metres.
    float pitch = 0.0f;
    // One frame of `dt` game seconds.
    void Frame(float dt, bool reported = true)
    {
        now += dt;
        heading += turn * dt;
        x += speed * std::sin(heading) * dt;
        z += speed * std::cos(heading) * dt;
        std::vector<WaveSource> sources;
        if (reported)
        {
            WaveSource hull = Hull(x, z, speed, 7, heading);
            const float solved = std::floor(static_cast<float>(now) * 10.0f);
            hull.stem += pitch * std::sin(1.7f * solved);
            hull.stern -= 0.5f * pitch * std::sin(2.3f * solved);
            for (float &half : hull.station_half)
                half += 0.3f * pitch * std::sin(1.1f * solved);
            sources.push_back(hull);
        }
        trails.Update(sources, now, false);
    }
    std::vector<WakeVertex> Ribbon(double centre_z) const
    {
        std::vector<WakeVertex> out;
        if (!trails.Trails().empty())
            WakeTrails::Ribbon(trails.Trails()[0], now, {0.0, centre_z}, kExtent, false, out);
        return out;
    }
    std::vector<WakeVertex> Bow(std::array<double, 2> centre) const
    {
        std::vector<WakeVertex> out;
        if (!trails.Trails().empty())
            WakeTrails::BowBand(trails.Trails()[0], now, centre, kExtent, false, out);
        return out;
    }
};

// The foam of a bow band at the world point (x, z), as the target holds it: the band's sections interpolated along
// the hull (`forward`), the pixel shader's functions at the point. `centre`: the centre the band was made for.
float BowFoam(const std::vector<WakeVertex> &band, std::array<double, 2> centre, std::array<float, 2> forward, double x,
              double z)
{
    struct Section
    {
        double cx, cz, ax, az, along; // the centre line's point, the unit vector across, metres along `forward`
        float half, inside, age, strength;
    };
    std::vector<Section> sections;
    for (size_t i = 0; i + 1 < band.size(); i += 2)
    {
        const WakeVertex &a = band[i], &b = band[i + 1];
        const double ax = a.x * 0.5 * kExtent + centre[0], az = a.y * 0.5 * kExtent + centre[1];
        const double bx = b.x * 0.5 * kExtent + centre[0], bz = b.y * 0.5 * kExtent + centre[1];
        const double width = std::hypot(bx - ax, bz - az);
        const double cx = 0.5 * (ax + bx), cz = 0.5 * (az + bz);
        sections.push_back({cx, cz, (bx - ax) / width, (bz - az) / width, cx * forward[0] + cz * forward[1], a.half,
                            a.inside, a.age, a.strength});
    }
    const double along = x * forward[0] + z * forward[1];
    for (size_t i = 0; i + 1 < sections.size(); ++i)
    {
        const Section &front = sections[i], &back = sections[i + 1];
        if (along > front.along || along < back.along)
            continue;
        const auto t = static_cast<float>((front.along - along) / std::max(front.along - back.along, 1e-6));
        const double cx = front.cx + (back.cx - front.cx) * t, cz = front.cz + (back.cz - front.cz) * t;
        const auto side = static_cast<float>((x - cx) * front.ax + (z - cz) * front.az);
        const float band_here = shader::fxWakeBand(side, shader::mix(front.half, back.half, t),
                                                   shader::mix(front.inside, back.inside, t), 0.5f);
        return shader::fxWakeBowFoam(band_here, shader::mix(front.strength, back.strength, t),
                                     shader::mix(front.age, back.age, t));
    }
    return 0.0f;
}
} // namespace

int main()
{
    // Under way: a point every kSpacing metres, each as strong as the hull churned the water there, older and
    // wider the farther astern.
    Sailing s;
    s.speed = 6.0f;
    for (int i = 0; i < 20 * 60; ++i)
        s.Frame(1.0f / 60.0f);
    const auto &trail = s.trails.Trails()[0];
    CHECK(trail.points.size() >= 59 && trail.points.size() <= 61);
    auto ribbon = s.Ribbon(s.z);
    // A section ahead of the stern where the band fades in, the stern, then the laid points.
    CHECK(ribbon.size() == 2 * (trail.points.size() + 2));
    CHECK(ribbon[0].strength == 0.0f && ribbon[2].strength == WakeTrails::Strength(6.0f));
    for (size_t i = 2; i < ribbon.size(); i += 2)
    {
        CHECK(ribbon[i].age >= ribbon[i - 2].age && ribbon[i].half >= ribbon[i - 2].half);
        CHECK(ribbon[i].strength == WakeTrails::Strength(6.0f));
    }
    // Ahead of the stern by kRamp metres.
    CHECK(std::fabs((ribbon[0].y - ribbon[2].y) * 0.5f * kExtent - WakeTrails::kRamp) < 1e-3f);
    CHECK(ribbon.front().age == 0.0f && ribbon[ribbon.size() - 2].age > 19.0f);
    // The two vertices of a section lie to the two sides of the track, beyond the band's edge.
    CHECK(ribbon[0].side < -ribbon[0].half && ribbon[1].side == -ribbon[0].side);
    CHECK(ribbon[0].x * ribbon[1].x < 0.0f);

    // A section once laid stays as it lies: the next frames (new points ahead of it, another target centre) age and
    // widen it, and move or turn it by nothing.
    {
        const auto world = [](const WakeVertex &v, double centre_z) {
            return std::array<double, 2>{v.x * 0.5 * kExtent, v.y * 0.5 * kExtent + centre_z};
        };
        const double centre = s.z;
        const size_t points = trail.points.size();
        const auto laid = ribbon;
        s.turn = 0.35f; // the hull turns away while the new points are laid
        for (int i = 0; i < 60; ++i)
            s.Frame(1.0f / 60.0f);
        s.turn = 0.0f;
        const size_t added = s.trails.Trails()[0].points.size() - points;
        CHECK(added >= 2 && added <= 4);
        const auto later = s.Ribbon(s.z);
        // From the first laid section on (0: the fade-in ahead of the stern, 1: the stern itself).
        for (size_t i = 4; i + 2 * added < later.size() && i < laid.size(); i += 2)
        {
            const WakeVertex &a = laid[i], &b = later[i + 2 * added];
            // The band's centre line and its direction across, from the two vertices of the section.
            const auto a0 = world(a, centre), a1 = world(laid[i + 1], centre);
            const auto b0 = world(b, s.z), b1 = world(later[i + 2 * added + 1], s.z);
            CHECK(std::fabs(0.5 * (a0[0] + a1[0]) - 0.5 * (b0[0] + b1[0])) < 1e-3);
            CHECK(std::fabs(0.5 * (a0[1] + a1[1]) - 0.5 * (b0[1] + b1[1])) < 1e-3);
            CHECK(std::fabs((a1[1] - a0[1]) * (b1[0] - b0[0]) - (a1[0] - a0[0]) * (b1[1] - b0[1])) < 1e-3);
            CHECK(a.strength == b.strength && std::fabs(b.age - a.age - 1.0f) < 1e-3f);
            // The foam pattern's coordinates of the same water are the same.
            CHECK(std::fabs(0.5f * (a.world_x + laid[i + 1].world_x) -
                            0.5f * (b.world_x + later[i + 2 * added + 1].world_x)) < 1e-3f);
        }
        ribbon = later;
    }

    // The sails are struck: the hull is at rest in 5 s. No foam on the trail jumps from one frame to the next, the
    // hull lays no more of it, and what it laid ages where it lies: gone before the calmed band, both within kLife.
    std::vector<float> before = Foam(ribbon);
    float jump = 0.0f;
    for (int i = 0; i < 30 * 60; ++i)
    {
        s.speed = std::max(6.0f - 1.2f * static_cast<float>(i) / 60.0f, 0.0f);
        const size_t points = s.trails.Trails()[0].points.size();
        s.Frame(1.0f / 60.0f);
        ribbon = s.Ribbon(s.z);
        std::vector<float> after = Foam(ribbon);
        // The same sections as a frame ago (a new point becomes the second section).
        const size_t added = s.trails.Trails()[0].points.size() > points ? 1 : 0;
        for (size_t k = 1; k + added < after.size() && k < before.size(); ++k)
            jump = std::max(jump, std::fabs(after[k + added] - before[k]));
        jump = std::max(jump, std::fabs(after[0] - before[0]));
        before = after;
    }
    std::printf("stopping: the largest change of the foam on a section in one frame %.4f\n", jump);
    CHECK(jump < 0.02f);
    CHECK(s.speed == 0.0f && s.trails.Trails()[0].head.strength == 0.0f);
    const size_t at_rest = s.trails.Trails()[0].points.size();
    for (int i = 0; i < 10 * 60; ++i)
        s.Frame(1.0f / 60.0f);
    CHECK(s.trails.Trails()[0].points.size() <= at_rest); // none come
    CHECK(shader::fxWakeTrailFoam(1.0f, 0.0f, 1.0f, 40.0f, 1.0f, kFoamLife) < 0.05f);
    CHECK(shader::fxWakeCalm(1.0f, 1.0f, 40.0f, WakeTrails::kLife) > 0.3f);
    CHECK(shader::fxWakeCalm(1.0f, 1.0f, WakeTrails::kLife, WakeTrails::kLife) == 0.0f);
    for (int i = 0; i < 40 * 60; ++i)
        s.Frame(1.0f / 60.0f);
    CHECK(s.Ribbon(s.z).empty()); // a hull lying still has no wake

    // Game time: a pause (no time passes) changes nothing; eight times the speed ages the trail eight times as fast.
    Sailing a, b;
    a.speed = b.speed = 6.0f;
    for (int i = 0; i < 10 * 60; ++i)
        a.Frame(1.0f / 60.0f);
    for (int i = 0; i < 10 * 60 / 8; ++i)
        b.Frame(8.0f / 60.0f);
    CHECK(std::fabs(a.Ribbon(a.z).back().age - b.Ribbon(b.z).back().age) < 0.5f);
    CHECK(std::fabs(a.z - b.z) < 0.01 && b.trails.Trails()[0].points.size() > 20);
    const auto paused = a.Ribbon(a.z);
    for (int i = 0; i < 100; ++i)
        a.Frame(0.0f);
    const auto resumed = a.Ribbon(a.z);
    CHECK(paused.size() == resumed.size() && paused.back().age == resumed.back().age);

    // A hull no longer reported (sunk): its trail ends where it last laid it, ages and goes.
    for (int i = 0; i < 60; ++i)
        a.Frame(1.0f / 60.0f, false);
    CHECK(a.trails.Trails().size() == 1 && !a.Ribbon(a.z).empty());
    CHECK(a.trails.Trails()[0].head.time == a.trails.Trails()[0].points.front().time);
    for (int i = 0; i < 61 * 60; ++i)
        a.Frame(1.0f / 60.0f, false);
    CHECK(a.trails.Trails().empty());

    // A teleport starts the trail anew; the first sea frame after frames without the sea drops the trails of hulls
    // that are not in it (another scene), and keeps those of hulls that are.
    b.z += 500.0;
    b.Frame(1.0f / 60.0f);
    CHECK(b.trails.Trails()[0].points.size() == 1 && b.Ribbon(b.z).empty());
    for (int i = 0; i < 5 * 60; ++i)
        b.Frame(1.0f / 60.0f);
    const std::vector<WaveSource> same{Hull(0.0, b.z, 6.0f)}, other{Hull(0.0, b.z, 6.0f, 8)};
    b.trails.Update(same, b.now, true);
    CHECK(b.trails.Trails().size() == 1 && b.trails.Trails()[0].points.size() > 10);
    b.trails.Update(other, b.now, true);
    CHECK(b.trails.Trails().size() == 1 && b.trails.Trails()[0].identity == 8 &&
          b.trails.Trails()[0].points.size() == 1);

    // The target's window: a trail wholly outside it is not drawn; one that reaches into it is.
    Sailing w;
    w.speed = 6.0f;
    for (int i = 0; i < 10 * 60; ++i)
        w.Frame(1.0f / 60.0f);
    CHECK(!w.Ribbon(w.z + 200.0).empty());
    CHECK(w.Ribbon(w.z + 400.0).empty());

    // Slow, drifting and going astern: nothing is churned.
    CHECK(WakeTrails::Strength(0.0f) == 0.0f && WakeTrails::Strength(WakeTrails::kSlowest) == 0.0f);
    CHECK(WakeTrails::Strength(-6.0f) == 0.0f);
    CHECK(WakeTrails::Strength(3.0f) > 0.0f && WakeTrails::Strength(3.0f) < WakeTrails::Strength(6.0f));
    CHECK(WakeTrails::Strength(WakeTrails::kFull) == 1.0f && WakeTrails::Strength(30.0f) == 1.0f);

    // The bow wave's band: none at rest, astern, outside the target or for a hull not reported now; under way a
    // section ahead of the stem where it fades in, the stem, one per station, the stern and one astern where it
    // fades out; the water older the farther aft (the distance from the stem over the speed), whiter the faster and
    // the longer the hull; its foam whitest at the stem and never gone beside a moving hull.
    const auto band = [](float speed, std::array<double, 2> centre) {
        Sailing hull;
        hull.speed = speed;
        hull.Frame(1.0f / 60.0f);
        return hull.Bow(centre);
    };
    CHECK(band(0.0f, {0.0, 0.0}).empty() && band(-4.0f, {0.0, 0.0}).empty() && band(6.0f, {0.0, 600.0}).empty());
    const auto bow = band(6.0f, {0.0, 0.0});
    CHECK(bow.size() == 2 * (WaveSource::kHullStations + 4));
    CHECK(bow[0].age == 0.0f && bow[2].age == 0.0f && bow[0].strength == 0.0f && bow[bow.size() - 2].strength == 0.0f);
    CHECK(std::fabs(bow[bow.size() - 2].age - (32.0f + WakeTrails::kRamp) / 6.0f) < 1e-4f);
    for (size_t i = 2; i < bow.size(); i += 2)
        CHECK(bow[i].age >= bow[i - 2].age && bow[i].half > bow[i].inside);
    {
        Sailing gone;
        gone.speed = 6.0f;
        gone.Frame(1.0f / 60.0f);
        gone.Frame(1.0f / 60.0f, false);
        CHECK(gone.Bow({0.0, 0.0}).empty());
    }
    WakeTrails::Hull corvette, tartane, slow;
    corvette.stern = -16.0f, corvette.stem = 16.0f, corvette.speed = 6.0f;
    tartane = corvette, tartane.stern = -6.0f, tartane.stem = 6.0f;
    slow = corvette, slow.speed = 3.0f;
    CHECK(WakeTrails::BowStrength(tartane) < WakeTrails::BowStrength(corvette));
    CHECK(WakeTrails::BowStrength(slow) < WakeTrails::BowStrength(corvette) && WakeTrails::BowStrength(slow) > 0.0f);
    CHECK(shader::fxWakeBowFoam(1.0f, 1.0f, 0.0f) == 1.0f);
    CHECK(shader::fxWakeBowFoam(1.0f, 1.0f, 6.0f) > 0.5f && shader::fxWakeBowFoam(1.0f, 1.0f, 6.0f) < 0.65f);
    CHECK(shader::fxWakeBowFoam(1.0f, 0.0f, 0.0f) == 0.0f);

    // The foam's front follows the hull continuously. The foam at points fixed in the water, ahead of the stem,
    // beside the bow and astern of the band, never changes in one frame by more than the hull's movement in that
    // frame allows (a band that begins at an edge, or whose ends follow the waterline as the game solves it, ten
    // times a second, makes the foam appear in steps): at any frame time, time scale and speed, and in a turn.
    for (const float frame : {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f})
        for (const float scale : {1.0f, 2.0f, 8.0f})
            for (const float speed : {2.0f, 6.0f})
                for (const float turn : {0.0f, 0.2f})
                {
                    Sailing hull;
                    hull.speed = speed, hull.turn = turn, hull.pitch = 0.6f;
                    const float dt = frame * scale;
                    for (int i = 0; i < 40; ++i)
                        hull.Frame(dt);
                    // Lines of points along the course through the stem and 5 m to each side of it, from astern
                    // of the hull to well ahead of where it will be.
                    const std::array<float, 2> course{std::sin(hull.heading), std::cos(hull.heading)};
                    std::vector<std::array<double, 2>> points;
                    for (const double side : {-5.0, 0.0, 5.0})
                        for (double along = -30.0; along < 30.0 + 40.0 * speed * dt; along += 0.25)
                            points.push_back({hull.x + course[0] * along + course[1] * side,
                                              hull.z + course[1] * along - course[0] * side});
                    const auto foam = [&] {
                        std::vector<float> values;
                        const auto &state = hull.trails.Trails()[0].hull;
                        const std::array<double, 2> centre{hull.x, hull.z};
                        const auto shape = hull.Bow(centre);
                        for (const auto &p : points)
                            values.push_back(BowFoam(shape, centre, state.forward, p[0], p[1]));
                        return values;
                    };
                    auto before_frame = foam();
                    float worst = 0.0f, seen_foam = 0.0f;
                    for (int i = 0; i < 40; ++i)
                    {
                        hull.Frame(dt);
                        const auto after = foam();
                        for (size_t k = 0; k < after.size(); ++k)
                        {
                            worst = std::max(worst, std::fabs(after[k] - before_frame[k]));
                            seen_foam = std::max(seen_foam, after[k]);
                        }
                        before_frame = after;
                    }
                    // Full foam cannot come or go over less than 1.2 m of the band's way: the hull's, and in a
                    // turn its ends' way round it.
                    const float allowed = (speed + turn * 32.0f) * dt / 1.2f + 0.02f;
                    if (worst > allowed)
                        std::printf("frame %.4f s x%.0f, %.0f m/s, turn %.1f: foam changed by %.3f in a frame, %.3f "
                                    "allowed\n",
                                    frame, scale, speed, turn, worst, allowed);
                    CHECK(worst <= allowed);
                    CHECK(speed < 5.0f || seen_foam > 0.3f); // the points did see the band
                }

    // A band's cross-section: whole inside, nothing at its edge and beyond, wherever the pattern pushes the edge.
    for (const float clump : {0.0f, 0.5f, 1.0f})
    {
        CHECK(shader::fxWakeBand(0.0f, 4.0f, 2.2f, clump) == 1.0f);
        const float reach = 4.0f + WakeTrails::kUneven * (4.0f - 2.2f); // where the ribbon ends
        CHECK(shader::fxWakeBand(reach, 4.0f, 2.2f, clump) == 0.0f);
        CHECK(shader::fxWakeBand(-reach, 4.0f, 2.2f, clump) == 0.0f);
    }
    // The foam pattern is fixed in the water: its coordinates may move by whole tiles (WakeTrails::PatternBase).
    const float tile = static_cast<float>(WakeTrails::kPatternTile);
    for (const float x : {3.0f, 211.5f, 700.25f})
    {
        const float clump = shader::fxWakeClump(x, 0.37f * x);
        CHECK(clump >= 0.0f && clump <= 1.0f);
        CHECK(std::fabs(clump - shader::fxWakeClump(x - tile, 0.37f * x)) < 2e-3f);
        CHECK(std::fabs(clump - shader::fxWakeClump(x, 0.37f * x + tile)) < 2e-3f);
    }
    CHECK(WakeTrails::PatternBase({1500.0, -10.0})[0] == 1024.0 &&
          WakeTrails::PatternBase({1500.0, -10.0})[1] == -1024.0);

    std::printf("wake trail checks: %s\n", failures ? "FAILED" : "PASS");
    return failures ? 1 : 0;
}
