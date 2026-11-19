// The ship wakes over time, without the GPU: a hull that gets under way, sails, stops (its sails struck: it coasts to
// rest), lies still and sails again, at 60 frames a second, through the path of the game: SHIP::AddWaveSource's
// smoothed speed, WakePaths::Update, the ribbon of WakePaths::Ribbon rasterised on the CPU, the pixel shader
// fs_fx_wake.sc with the Kelvin waves of include/fx_kelvin.sh, blended as the target is. The hull's position jitters
// by millimetres from frame to frame and its heading sways on the swell, under way and at rest. In every frame the
// wave slope the sea reads is sampled near the hull and astern. Checks:
// - no flicker: while the hull sails steadily, the waves near it and astern never drop for a frame;
// - no pop: the waves never change much from one frame to the next, while the hull stops and starts too;
// - the waves made keep going after the hull has stopped, each crest along its own normal at the speed that made
//   them: the transverse crests forwards along the course (astern of the hull and on past it: waves run on ahead of
//   the stopped hull, none ahead of a sailing one), the divergent arms outward and forward (where an arm crosses a
//   line across the track it moves out at tan 19.47 degrees of the speed it crosses a line along the track forwards),
//   and they die away;
// - a hull lying still makes no waves.
#include "storm/fx/wake_path.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace kelvin
{
struct vec4
{
    float x, y, z, w;
    vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
inline float abs(float v) { return std::fabs(v); }
inline float sqrt(float v) { return std::sqrt(v); }
inline float pow(float a, float b) { return std::pow(a, b); }
inline float exp(float v) { return std::exp(v); }
inline float sin(float v) { return std::sin(v); }
inline float cos(float v) { return std::cos(v); }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
inline float smoothstep(float a, float b, float v)
{
    const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
#include "../shaders/include/fx_kelvin.sh"
} // namespace kelvin

namespace
{
int failures = 0;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::printf("FAILED line %d: %s\n", __LINE__, #x);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

using storm::fx::WakePaths;
using storm::fx::WakeVertex;
using storm::fx::WaveSource;
using Point2 = std::array<double, 2>;

constexpr float kExtent = 512.0f, kTexel = 1.0f; // fx::SeaWakes
constexpr float kStem = 20.0f, kStern = -18.0f, kHalfBeam = 5.0f;

// fs_fx_wake.sc for one fragment: the world slope (x, z) and the blend weight.
struct Fragment
{
    float sx, sz, weight;
};
Fragment Shade(const WakeVertex &v, float hull_length)
{
    const float shortest = std::max(3.5f * kTexel, 6.2831853f / (4.0f * v.k0));
    const auto bow = kelvin::fxKelvin(v.along, v.side, v.k0, 0.5f * hull_length, shortest, 0.45f, 1.0f);
    const auto stern = kelvin::fxKelvin(v.along - hull_length, v.side, v.k0, 0.5f * hull_length, shortest, 0.45f, 1.0f);
    const float slope_along = (bow.y - 0.6f * stern.y) * v.amplitude, slope_side = (bow.z - 0.6f * stern.z) * v.amplitude;
    const float t = std::clamp(bow.w / 0.15f, 0.0f, 1.0f);
    return {slope_along * v.back_x - slope_side * v.back_z, slope_along * v.back_z + slope_side * v.back_x,
            v.fade * t * t * (3.0f - 2.0f * t)};
}

// The ribbon (a triangle strip, clip space around `centre`) rasterised at world points: per point the slope the sea
// reads after every triangle covering it is blended over the cleared target in order (source alpha).
void Raster(const std::vector<WakeVertex> &strip, float hull_length, Point2 centre, const std::vector<Point2> &points,
            std::vector<std::array<float, 2>> &slopes)
{
    slopes.assign(points.size(), {0.0f, 0.0f});
    for (size_t t = 0; t + 2 < strip.size(); ++t)
    {
        const WakeVertex &a = strip[t], &b = strip[t + 1], &c = strip[t + 2];
        const float det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::fabs(det) < 1e-12f)
            continue;
        const float x0 = std::min({a.x, b.x, c.x}), x1 = std::max({a.x, b.x, c.x});
        const float y0 = std::min({a.y, b.y, c.y}), y1 = std::max({a.y, b.y, c.y});
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float px = static_cast<float>((points[i][0] - centre[0]) / (0.5 * kExtent));
            const float py = static_cast<float>((points[i][1] - centre[1]) / (0.5 * kExtent));
            if (px < x0 || px > x1 || py < y0 || py > y1)
                continue;
            const float wb = ((px - a.x) * (c.y - a.y) - (c.x - a.x) * (py - a.y)) / det;
            const float wc = ((b.x - a.x) * (py - a.y) - (px - a.x) * (b.y - a.y)) / det;
            const float wa = 1.0f - wb - wc;
            if (wa < 0.0f || wb < 0.0f || wc < 0.0f)
                continue;
            WakeVertex v{};
            const auto mix = [&](float WakeVertex::*m) { return wa * a.*m + wb * b.*m + wc * c.*m; };
            v.fade = mix(&WakeVertex::fade);
            v.along = mix(&WakeVertex::along);
            v.side = mix(&WakeVertex::side);
            v.k0 = mix(&WakeVertex::k0);
            v.back_x = mix(&WakeVertex::back_x);
            v.back_z = mix(&WakeVertex::back_z);
            v.amplitude = mix(&WakeVertex::amplitude);
            const Fragment f = Shade(v, hull_length);
            slopes[i][0] += (f.sx - slopes[i][0]) * f.weight;
            slopes[i][1] += (f.sz - slopes[i][1]) * f.weight;
        }
    }
}

struct Phase
{
    const char *name;
    double until;  // seconds
    float target;  // m/s the hull approaches
};

uint32_t random_state = 12345u;
float Jitter() // -1..1, a fixed sequence
{
    random_state = random_state * 1664525u + 1013904223u;
    return static_cast<float>(random_state >> 8) / static_cast<float>(1u << 23) - 1.0f;
}
} // namespace

int main()
{
    const Phase phases[] = {{"under way", 30.0, 5.0f}, {"sailing", 60.0, 5.0f},        {"stopping", 95.0, 0.0f},
                            {"lying still", 150.0, 0.0f}, {"under way again", 185.0, 5.0f}};
    constexpr double kDt = 1.0 / 60.0;
    constexpr float kAccel = 0.25f;   // m/s^2 under sail; struck, it coasts at 0.2
    constexpr float kJitter = 0.003f; // metres of position jitter a frame
    constexpr float kSway = 0.0087f;  // radians of heading sway on the swell (half a degree)
    WakePaths paths;
    double x = 0.0, z = 0.0;
    float speed = 0.0f, wave_speed = 0.0f;
    double last_x = 0.0, last_z = 0.0;
    bool last = false;
    // Sample grids in the hull's frame: near it (the bow wave and the first wake) and astern.
    struct Region
    {
        float along0, along1, side, step;
    };
    const Region near{-5.0f, 45.0f, 25.0f, 2.0f}, astern{45.0f, 220.0f, 80.0f, 4.0f};
    std::vector<Point2> points, line;
    std::vector<std::array<float, 2>> slopes, line_slopes;
    std::vector<WakeVertex> strip;
    double line_phase = 0.0;
    float sailing_near = 0.0f, sailing_astern = 0.0f; // the steady energies
    float worst_drop = 1.0f, worst_change = 0.0f;
    double worst_change_t = 0.0, stop_time = -1.0, line_time = -1.0;
    const char *worst_change_phase = "";
    float energy_stop = 0.0f, energy_after_30 = -1.0f, near_still = -1.0f, crest_speed = 0.0f;
    // Ahead of the bow (in the hull's frame, 5..60 m ahead, 15 m to each side): the waves while sailing, and 10 s after
    // the stop (mean slope, as the regions above).
    std::vector<Point2> ahead_points;
    float ahead_sailing = 0.0f, ahead_after_10 = -1.0f;
    // An arm against two lines fixed in the water at the stop: across the track 80 m astern of the bow (to one side),
    // and along it 30 m to that side; where the arm crosses each, 3 and 5 s after the stop.
    std::vector<Point2> across_line, along_line;
    float across_at_3 = 0.0f, along_at_3 = 0.0f, arm_out = 0.0f, arm_forward = 0.0f;
    float previous_near = -1.0f, previous_astern = -1.0f;
    double now = 0.0;
    int frame = 0;
    for (const Phase &phase : phases)
    {
        float phase_min = 1e9f, phase_max = 0.0f;
        for (; now < phase.until; now += kDt, ++frame)
        {
            if (phase.target > speed)
                speed = std::min(phase.target, speed + kAccel * static_cast<float>(kDt));
            else
                speed = std::max(phase.target, speed - 0.8f * kAccel * static_cast<float>(kDt));
            const float heading = kSway * std::sin(static_cast<float>(now) * 6.2831853f / 7.0f);
            x += std::sin(heading) * speed * kDt;
            z += std::cos(heading) * speed * kDt;
            const double jx = x + kJitter * Jitter(), jz = z + kJitter * Jitter();
            // SHIP::AddWaveSource: the speed along the heading from the motion, smoothed over about half a second.
            if (last)
            {
                const float k = std::min(1.0f, static_cast<float>(kDt) * 2.0f);
                const float along = static_cast<float>(((jx - last_x) * std::sin(heading) + (jz - last_z) * std::cos(heading)) / kDt);
                wave_speed += k * (along - wave_speed);
            }
            last_x = jx, last_z = jz, last = true;
            WaveSource s;
            s.kind = WaveSource::Kind::Hull;
            s.identity = 3;
            s.position = {jx, jz};
            s.forward = {std::sin(heading), std::cos(heading)};
            s.stern = kStern;
            s.stem = kStem;
            s.half_width = kHalfBeam;
            s.speed = wave_speed;
            paths.Update(std::span<const WaveSource>(&s, 1), now);
            const WakePaths::Path &path = paths.Paths().front();
            const Point2 centre{std::floor(x / kTexel + 0.5) * kTexel, std::floor(z / kTexel + 0.5) * kTexel};
            strip.clear();
            WakePaths::Ribbon(path, now, centre, kExtent, false, strip);
            // The grids in the hull's frame (bow at the origin, `a` metres behind it, `sd` to the side).
            const double bx = x + std::sin(heading) * kStem, bz = z + std::cos(heading) * kStem;
            points.clear();
            size_t near_count = 0;
            for (const Region *r : {&near, &astern})
            {
                for (float a = r->along0; a <= r->along1; a += r->step)
                    for (float sd = -r->side; sd <= r->side; sd += r->step)
                        points.push_back({bx - std::sin(heading) * a - std::cos(heading) * sd,
                                          bz - std::cos(heading) * a + std::sin(heading) * sd});
                if (r == &near)
                    near_count = points.size();
            }
            Raster(strip, path.length, centre, points, slopes);
            float e_near = 0.0f, e_astern = 0.0f;
            for (size_t i = 0; i < points.size(); ++i)
                (i < near_count ? e_near : e_astern) += std::hypot(slopes[i][0], slopes[i][1]);
            e_near /= static_cast<float>(near_count);
            e_astern /= static_cast<float>(points.size() - near_count);
            const float total = e_near + e_astern;
            const auto mean_slope = [&](const std::vector<Point2> &at) {
                std::vector<std::array<float, 2>> s;
                Raster(strip, path.length, centre, at, s);
                float sum = 0.0f;
                for (const auto &v : s)
                    sum += std::hypot(v[0], v[1]);
                return sum / static_cast<float>(at.size());
            };
            const auto hull_frame = [&](float a, float sd) -> Point2 {
                return {bx - std::sin(heading) * a - std::cos(heading) * sd, bz - std::cos(heading) * a + std::sin(heading) * sd};
            };
            if (phase.name == phases[1].name && frame % 30 == 0)
            {
                ahead_points.clear();
                for (float a = -60.0f; a <= -5.0f; a += 2.0f)
                    for (float sd = -15.0f; sd <= 15.0f; sd += 2.0f)
                        ahead_points.push_back(hull_frame(a, sd));
                ahead_sailing = std::max(ahead_sailing, mean_slope(ahead_points));
            }
            phase_min = std::min(phase_min, total), phase_max = std::max(phase_max, total);
            if (phase.name == phases[1].name)
            {
                sailing_near = std::max(sailing_near, e_near);
                sailing_astern = std::max(sailing_astern, e_astern);
                if (previous_near > 0.0f)
                    worst_drop = std::min({worst_drop, e_near / previous_near, e_astern / previous_astern});
            }
            // Pops: the largest change from one frame to the next, against the steady sailing level.
            if (sailing_near > 0.0f && previous_near >= 0.0f)
            {
                const float change = std::fabs(total - (previous_near + previous_astern)) / (sailing_near + sailing_astern);
                if (change > worst_change)
                    worst_change = change, worst_change_t = now, worst_change_phase = phase.name;
            }
            previous_near = e_near, previous_astern = e_astern;
            if (stop_time < 0.0 && phase.target == 0.0f && speed == 0.0f)
            {
                stop_time = now;
                energy_stop = total;
                // A line along the track astern, fixed in the water: where the transverse crests are.
                for (double a = 30.0; a <= 150.0; a += 0.25)
                    line.push_back({bx - std::sin(heading) * a, bz - std::cos(heading) * a});
                ahead_points.clear();
                for (float a = -60.0f; a <= -5.0f; a += 2.0f)
                    for (float sd = -15.0f; sd <= 15.0f; sd += 2.0f)
                        ahead_points.push_back(hull_frame(a, sd));
                for (float sd = 0.0f; sd <= 100.0f; sd += 0.5f)
                    across_line.push_back(hull_frame(80.0f, sd));
                for (float a = -60.0f; a <= 200.0f; a += 0.5f)
                    along_line.push_back(hull_frame(a, 30.0f));
            }
            if (stop_time >= 0.0 && phase.target == 0.0f)
            {
                const double since = now - stop_time;
                if (energy_after_30 < 0.0f && since >= 30.0)
                    energy_after_30 = total;
                if (near_still < 0.0f && since >= 50.0)
                    near_still = e_near;
                if (ahead_after_10 < 0.0f && since >= 10.0)
                    ahead_after_10 = mean_slope(ahead_points);
                // Where the arm crosses a line: the end of the stretch whose slope, averaged over a wavelength of
                // the waves made at full speed (16 m), is at least half its largest; across the track the outer end,
                // along it the forward one (the line's first samples).
                const auto crossing = [&](const std::vector<Point2> &at, bool outer) {
                    std::vector<std::array<float, 2>> s;
                    Raster(strip, path.length, centre, at, s);
                    std::vector<float> smooth(s.size(), 0.0f);
                    float top = 0.0f;
                    for (size_t i = 0; i < s.size(); ++i)
                    {
                        float sum = 0.0f;
                        int n = 0;
                        for (size_t j = i >= 16 ? i - 16 : 0; j < std::min(s.size(), i + 17); ++j, ++n)
                            sum += std::hypot(s[j][0], s[j][1]);
                        smooth[i] = sum / static_cast<float>(n);
                        top = std::max(top, smooth[i]);
                    }
                    size_t edge = 0;
                    for (size_t i = 0; i < s.size(); ++i)
                        if (smooth[i] >= 0.5f * top)
                        {
                            edge = i;
                            if (!outer)
                                break;
                        }
                    return 0.5f * static_cast<float>(edge); // metres along the line from its start
                };
                if (across_at_3 == 0.0f && since >= 3.0)
                {
                    across_at_3 = crossing(across_line, true);
                    along_at_3 = crossing(along_line, false);
                }
                else if (arm_out == 0.0f && since >= 5.0)
                {
                    arm_out = (crossing(across_line, true) - across_at_3) / 2.0f;
                    arm_forward = (along_at_3 - crossing(along_line, false)) / 2.0f;
                }
                // The crests' motion along the line over a second, from 3 s after the stop: the phase of the
                // transverse waves made at full speed (wavenumber g / 25) along the track, positive forwards.
                const auto phase = [&]() {
                    Raster(strip, path.length, centre, line, line_slopes);
                    double re = 0.0, im = 0.0;
                    for (size_t i = 0; i < line.size(); ++i)
                    {
                        const double a = 30.0 + 0.25 * static_cast<double>(i); // metres behind the stop
                        const double v = line_slopes[i][0] * std::sin(heading) + line_slopes[i][1] * std::cos(heading);
                        re += v * std::cos(9.81 / 25.0 * a), im += v * std::sin(9.81 / 25.0 * a);
                    }
                    return std::atan2(im, re);
                };
                if (line_time < 0.0 && since >= 3.0)
                {
                    line_phase = phase();
                    line_time = now;
                }
                else if (line_time >= 0.0 && crest_speed == 0.0f && now - line_time >= 1.0)
                {
                    double turn = phase() - line_phase;
                    turn -= 6.2831853 * std::floor(turn / 6.2831853 + 0.5);
                    // The waves go as cos(k (a + way)), a metres behind the stop: the projection's phase is -k way.
                    crest_speed = static_cast<float>(-turn / (9.81 / 25.0) / (now - line_time));
                }
            }
            if (frame % 600 == 0)
                std::printf("t %6.1f s %-16s speed %.2f m/s (waves' %.2f): track %3zu points, waves near %.5f astern %.5f\n",
                            now, phase.name, speed, wave_speed, path.points.size(), e_near, e_astern);
        }
        std::printf("%-16s waves %.5f..%.5f\n", phase.name, phase_min, phase_max);
    }
    std::printf("sailing: the worst one-frame drop leaves %.3f of the waves (1: none)\n", worst_drop);
    std::printf("the largest one-frame change: %.3f of the sailing waves at %.2f s (%s)\n", worst_change, worst_change_t,
                worst_change_phase);
    std::printf("stopped at %.1f s: waves %.5f, 30 s later %.5f; near the hull 50 s later %.6f (sailing %.5f)\n",
                stop_time, energy_stop, energy_after_30, near_still, sailing_near);
    std::printf("3 s after the stop the crests astern run forwards at %.2f m/s\n", crest_speed);
    std::printf("ahead of the bow: waves %.5f while sailing, %.5f 10 s after the stop (near the hull sailing %.5f)\n",
                ahead_sailing, ahead_after_10, sailing_near);
    std::printf("3..5 s after the stop an arm moves out %.2f m/s across the track, forwards %.2f m/s along it (ratio %.3f, "
                "tan 19.47 degrees 0.354)\n", arm_out, arm_forward, arm_forward > 0.0f ? arm_out / arm_forward : 0.0f);
    CHECK(worst_drop > 0.9f);
    CHECK(worst_change < 0.08f);
    // The waves made at full speed (5 m/s) run on at it.
    CHECK(crest_speed > 4.0f && crest_speed < 6.0f);
    CHECK(ahead_sailing < 0.05f * sailing_near);
    CHECK(ahead_after_10 > 0.2f * sailing_near);
    CHECK(arm_forward > 3.5f && arm_forward < 6.5f);
    CHECK(arm_out > 0.25f * arm_forward && arm_out < 0.5f * arm_forward);
    CHECK(energy_after_30 >= 0.0f && energy_after_30 < 0.3f * energy_stop);
    CHECK(near_still >= 0.0f && near_still < 0.01f * sailing_near);
    std::printf("wake motion checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
