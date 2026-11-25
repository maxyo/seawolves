// The churned water of the hulls for fx::SeaWakes, without the GPU: the track each stern has laid and the ribbons
// its foam is drawn on (shaders/fs_fx_wake.sc, include/fx_wake_trail.sh). Design: fx/README.md, "Ship wakes".
#pragma once

#include "storm/fx/wave_source.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace storm::fx
{

struct WakeVertex
{
    float x, y, age;            // clip position in the wake target; seconds since the hull churned the water there
    float side, strength, half; // metres off the band's centre line; 0..1 how hard it was churned; the band's half
    float world_x, world_z,
        inside; // metres from WakeTrails::PatternBase; the foam is whole this far off the centre line
};

class WakeTrails
{
  public:
    static constexpr float kSpacing = 2.0f;          // metres of track between points
    static constexpr float kLife = 60.0f;            // seconds a point of track is kept (the calmed band's life)
    static constexpr float kFoamLife = 14.0f;        // seconds the trail's lasting foam takes to fall to 1/e
    static constexpr float kSlowest = 1.0f;          // m/s: a slower hull churns nothing
    static constexpr float kFull = 7.5f;             // m/s: from this speed the wake is at its whitest
    static constexpr float kSpread = 0.55f;          // m / sqrt(s): the band widens by this times the root of its age
    static constexpr float kTrailBeam = 0.85f;       // the trail starts this share of the hull's beam wide
    static constexpr float kTrailWhole = 0.55f;      // the trail's foam is whole over this share of its half width
    static constexpr float kSternInset = 0.12f;      // the trail starts this share of the waterline ahead of the stern
    static constexpr float kUneven = 0.3f;           // a ribbon reaches this share of its band's soft edge farther out
    static constexpr float kRamp = 2.5f;             // metres over which a band's ends fade in: over two target texels
    static constexpr float kSettle = 0.3f;           // seconds a hull's waterline takes to follow what is reported
    static constexpr float kGone = 0.5f;             // seconds without a report after which a hull is gone
    static constexpr float kJump = 50.0f;            // metres in one update: a teleport, not sailing
    static constexpr double kPatternTile = 1024.0;   // metres: the period of the foam pattern's coordinates
    static constexpr float kFaintest = 1.0f / 64.0f; // foam strength under which a band is not worth a draw
    // The bow wave's foam beside the hull: this share of the half beam wide at the stem, within these metres; its
    // outer kBowSoft metres fade out; a hull of kBowFullLength metres or more has all of it, half as long half.
    static constexpr float kBowBeam = 0.8f, kBowLeast = 2.5f, kBowMost = 4.0f, kBowSoft = 2.0f;
    static constexpr float kBowFullLength = 30.0f;

    struct Point
    {
        double x = 0.0, z = 0.0; // the stern, absolute world metres
        float strength = 0.0f;
        double time = 0.0;          // game seconds when it was laid
        float tx = 0.0f, tz = 1.0f; // the track's direction there (unit x, z), fixed when it is laid
    };
    // A hull as its bands are drawn: where it is now, and its waterline, which follows the reported one within
    // kSettle seconds (the game solves the waterline ten times a second: drawn as reported, the ends of the bands
    // would move in those steps as the hull pitches).
    struct Hull
    {
        std::array<double, 2> position{};
        std::array<float, 2> forward{0.0f, 1.0f};
        float speed = 0.0f;
        float stern = -0.5f, stem = 0.5f, half_beam = 0.5f;
        std::array<float, WaveSource::kHullStations> station_side{}, station_half{};
    };
    struct Trail
    {
        uint64_t identity = 0;
        Hull hull;
        bool settled = false;     // `hull` holds a waterline
        Point head;               // the stern now
        std::deque<Point> points; // laid track, newest first
        double seen = 0.0;        // game seconds of the hull's last report
    };

    // How hard a hull churns the water at `speed` (m/s along its heading): nothing at rest, drifting or going astern.
    static float Strength(float speed)
    {
        const float s = std::clamp((speed - kSlowest) / (kFull - kSlowest), 0.0f, 1.0f);
        return s * std::sqrt(s);
    }
    // The band's half width (m) `age` seconds after a hull of `half_beam` passed.
    static float HalfWidth(float half_beam, float age)
    {
        return kTrailBeam * half_beam + kSpread * std::sqrt(std::max(age, 0.0f));
    }
    // How white the bow wave of `hull` is: by its speed as the trail, less for a short hull.
    static float BowStrength(const Hull &hull)
    {
        return Strength(hull.speed) * std::clamp((hull.stem - hull.stern) / kBowFullLength, 0.5f, 1.0f);
    }

    // This frame's hulls (those with an identity) at game time `now`. `resumed`:
    // the first update after frames without the sea (a loading screen, another scene): the trails of hulls that are
    // not in it belong to the scene before and are dropped.
    void Update(std::span<const WaveSource> sources, double now, bool resumed)
    {
        if (resumed)
            std::erase_if(trails_, [&](const Trail &t) {
                return std::none_of(sources.begin(), sources.end(),
                                    [&](const WaveSource &s) { return s.identity == t.identity; });
            });
        for (const WaveSource &s : sources)
        {
            if (s.identity == 0)
                continue;
            Trail *trail = Find(s.identity);
            if (!trail)
            {
                trails_.push_back({});
                trail = &trails_.back();
                trail->identity = s.identity;
            }
            Hull &hull = trail->hull;
            // The waterline settles towards the reported one; the first report, or one after the hull was gone, is
            // taken as it is.
            const double since = now - trail->seen;
            const float k =
                trail->settled && since <= kGone ? 1.0f - std::exp(-static_cast<float>(since) / kSettle) : 1.0f;
            const auto settle = [k](float &value, float reported) { value += k * (reported - value); };
            settle(hull.stern, s.stern);
            settle(hull.stem, s.stem);
            settle(hull.half_beam, std::max(s.half_width, 0.5f));
            for (int i = 0; i < WaveSource::kHullStations; ++i)
            {
                settle(hull.station_side[i], s.station_side[i]);
                settle(hull.station_half[i], s.station_half[i]);
            }
            trail->settled = true;
            hull.position = s.position;
            hull.forward = s.forward;
            hull.speed = s.speed;
            // A little ahead of the stern: the band starts under the hull's quarter, not behind a gap.
            const float at = hull.stern + kSternInset * (hull.stem - hull.stern);
            Point stern{s.position[0] + s.forward[0] * at,
                        s.position[1] + s.forward[1] * at,
                        Strength(s.speed),
                        now,
                        s.forward[0],
                        s.forward[1]};
            if (!trail->points.empty() && Distance(trail->head, stern) > kJump)
                trail->points.clear();
            // The track's direction at the stern: from the last laid point, once the stern is clear of it (a hull
            // lying still wanders about it: no direction); until then that point's.
            if (!trail->points.empty())
            {
                const Point &last = trail->points.front();
                const double dx = stern.x - last.x, dz = stern.z - last.z, d = std::hypot(dx, dz);
                if (d > 0.25 * kSpacing)
                    stern.tx = static_cast<float>(dx / d), stern.tz = static_cast<float>(dz / d);
                else
                    stern.tx = last.tx, stern.tz = last.tz;
            }
            if (trail->points.empty() || Distance(trail->points.front(), stern) >= kSpacing)
                trail->points.push_front(stern);
            trail->head = stern;
            trail->seen = now;
        }
        for (Trail &trail : trails_)
        {
            while (!trail.points.empty() && now - trail.points.back().time > kLife)
                trail.points.pop_back();
            // A hull no longer reported (sunk, gone) heads its trail no more: it ends at the last point it laid.
            if (now - trail.seen > kGone && !trail.points.empty())
                trail.head = trail.points.front();
        }
        std::erase_if(trails_, [&](const Trail &t) { return now - t.seen > kGone && t.points.empty(); });
    }

    void Reset()
    {
        trails_.clear();
    }
    const std::vector<Trail> &Trails() const
    {
        return trails_;
    }

    // The origin of the foam pattern's coordinates for a target centred on `centre`: whole tiles, so the pattern
    // (periodic over a tile) stays in the water when the origin moves.
    static std::array<double, 2> PatternBase(std::array<double, 2> centre)
    {
        return {std::floor(centre[0] / kPatternTile) * kPatternTile,
                std::floor(centre[1] / kPatternTile) * kPatternTile};
    }

    // The trail of `trail` as a triangle strip (two vertices per section, the stern first) in a target of `extent`
    // metres centred on `centre` (absolute world x, z); `flip_y`: the target's rows run from the top. Returns the
    // vertices appended to `out`: 0 when the trail holds no foam or calmed water inside the target.
    static size_t Ribbon(const Trail &trail, double now, std::array<double, 2> centre, float extent, bool flip_y,
                         std::vector<WakeVertex> &out)
    {
        const size_t count = trail.points.size() + 1;
        if (count < 3)
            return 0;
        const auto at = [&](size_t i) -> const Point & { return i == 0 ? trail.head : trail.points[i - 1]; };
        const size_t first = out.size();
        const Emit emit{centre, PatternBase(centre), extent, flip_y};
        // Ahead of the stern the band fades in over kRamp metres (under the hull): no band begins at an edge, which
        // the target's texels would move in steps.
        const Point &head = trail.head;
        const float start = HalfWidth(trail.hull.half_beam, 0.0f);
        emit.Section(out, head.x + head.tx * kRamp, head.z + head.tz * kRamp, -head.tz, head.tx, 0.0f, start, 0.0f,
                     0.0f, kTrailWhole * start);
        bool visible = false;
        for (size_t i = 0; i < count; ++i)
        {
            // Each section lies across the track's direction at its point, fixed when the point was laid: a section
            // once laid never turns or moves, whatever is laid after it.
            const Point &p = at(i);
            const float age = static_cast<float>(now - p.time);
            const float half = HalfWidth(trail.hull.half_beam, age);
            visible |= p.strength >= kFaintest && emit.Inside(p.x, p.z, half);
            emit.Section(out, p.x, p.z, -p.tz, p.tx, 0.0f, half, age, p.strength, kTrailWhole * half);
        }
        if (!visible)
            out.resize(first);
        return out.size() - first;
    }

    // The bow wave's foam of the hull of `trail` as a triangle strip: a band around its waterline, fading in over
    // kRamp metres ahead of the stem and out over as much astern, as old at each section as the water there is
    // since the stem met it (the distance from the stem over the hull's speed). Returns the vertices appended to
    // `out`: 0 for a hull that makes none, was not reported at `now` or lies outside the target.
    static size_t BowBand(const Trail &trail, double now, std::array<double, 2> centre, float extent, bool flip_y,
                          std::vector<WakeVertex> &out)
    {
        const Hull &hull = trail.hull;
        const float strength = BowStrength(hull);
        const Emit emit{centre, PatternBase(centre), extent, flip_y};
        const float length = hull.stem - hull.stern;
        if (trail.seen != now || strength < kFaintest ||
            !emit.Inside(hull.position[0], hull.position[1], length + hull.half_beam))
            return 0;
        const float margin = std::clamp(kBowBeam * hull.half_beam, kBowLeast, kBowMost);
        const double fx = hull.forward[0], fz = hull.forward[1];
        const size_t first = out.size();
        const auto section = [&](float along, float centre_side, float hull_half, float share) {
            const float behind = hull.stem - along; // metres behind the stem
            // Widest where the bow wave breaks, six tenths as wide at the stern.
            const float beside = margin * (1.0f - 0.4f * std::clamp(behind / length, 0.0f, 1.0f));
            emit.Section(out, hull.position[0] + fx * along, hull.position[1] + fz * along, -fz, fx, centre_side,
                         hull_half + beside, std::max(behind, 0.0f) / hull.speed, share * strength,
                         hull_half + std::max(beside - kBowSoft, 0.0f));
        };
        constexpr int kStations = WaveSource::kHullStations;
        section(hull.stem + kRamp, hull.station_side[kStations - 1], 0.0f, 0.0f);
        section(hull.stem, hull.station_side[kStations - 1], 0.0f, 1.0f);
        for (int k = kStations - 1; k >= 0; --k)
            section(hull.stern + (static_cast<float>(k) + 0.5f) / kStations * length, hull.station_side[k],
                    hull.station_half[k], 1.0f);
        section(hull.stern, hull.station_side[0], 0.0f, 1.0f);
        section(hull.stern - kRamp, hull.station_side[0], 0.0f, 0.0f);
        return out.size() - first;
    }

  private:
    struct Emit
    {
        std::array<double, 2> centre, base;
        float extent;
        bool flip_y;
        // Whether a disc of `radius` metres at world (x, z) reaches into the target.
        bool Inside(double x, double z, float radius) const
        {
            const double reach = 0.5 * extent + radius;
            return std::fabs(x - centre[0]) < reach && std::fabs(z - centre[1]) < reach;
        }
        // A cross-section at world (x, z) of a band `half` metres to each side of its centre line, which lies
        // `centre_side` metres along the unit vector (sx, sz), whole to `inside` metres: two vertices, beyond the
        // band by kUneven of its soft edge (the pattern pushes the edge out by less: fx_wake_trail.sh).
        void Section(std::vector<WakeVertex> &out, double x, double z, double sx, double sz, float centre_side,
                     float half, float age, float strength, float inside) const
        {
            const float reach = half + kUneven * (half - inside);
            for (const float side : {-reach, reach})
            {
                const double wx = x + sx * (centre_side + side), wz = z + sz * (centre_side + side);
                const float cx = static_cast<float>((wx - centre[0]) / (0.5 * extent));
                const float cy = static_cast<float>((wz - centre[1]) / (0.5 * extent)) * (flip_y ? -1.0f : 1.0f);
                out.push_back({cx, cy, age, side, strength, half, static_cast<float>(wx - base[0]),
                               static_cast<float>(wz - base[1]), inside});
            }
        }
    };
    static double Distance(const Point &a, const Point &b)
    {
        return std::hypot(a.x - b.x, a.z - b.z);
    }
    Trail *Find(uint64_t identity)
    {
        for (Trail &t : trails_)
            if (t.identity == identity)
                return &t;
        return nullptr;
    }
    std::vector<Trail> trails_;
};

} // namespace storm::fx
