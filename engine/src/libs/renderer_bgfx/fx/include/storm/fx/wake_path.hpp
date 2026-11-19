// The tracks behind the hulls for fx::SeaWakes, without the GPU: where each hull's bow has been, and the ribbon
// along that track on which its Kelvin waves are drawn (shaders/fs_fx_wake.sc, include/fx_kelvin.sh).
//
// A track is a polyline of the bow's positions, a point every kSpacing metres of its way, newest first; the bow where
// it is now heads it. The ribbon follows it back from the bow: at each point two vertices, one to each side, as far out
// as the waves reach there (the Kelvin wedge, 19.47 degrees, plus the beam and a margin), never farther than the
// track's turning radius (so the inner side of a turn does not fold over). Each vertex carries how far behind their
// source the waves there are and its distance to the side, the unit vector pointing back along the track there, the
// wavenumber and height of the waves for the speed the hull had when it passed, and how much of them is left with age.
// The waves therefore stay in the water the hull sailed through: a turn bends the wake behind it, it does not swing the
// whole V round.
//
// Waves keep going once made. In the frame of a hull sailing steadily its Kelvin waves stand still; in the water each
// crest runs along its own normal at its phase speed: the transverse crests forward at the hull's speed, the divergent
// arms outward and forward. When the hull slows or stops, the waves it made keep doing so: the pattern runs on as if
// its source still sailed at the speed that made it (Path::wave_speed), Path::lead metres ahead of the bow by now, so
// each crest keeps moving along its own normal, and the ribbon runs on past the bow along the course. What carries
// the waves' energy goes at half their speed (deep water): ahead of the bow they reach half the lead, fading out there.
// When the hull's own waves outweigh those left (its speed against theirs, faded with the time since they parted),
// the waves left run on as a track of their own (Path::free) and the hull starts a new one; a small lead (the hull's
// speed wavering on the swell) is taken back slowly instead. Nothing jumps when it slows, stops or gets under way.
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
    float x, y, fade;           // clip position in the wake target; how much of the waves is left (0..1)
    float along, side, k0;      // metres behind the waves' source along the track, metres to the side; g / U^2 (1/m)
    float back_x, back_z, amplitude; // unit vector back along the track (world x, z); the waves' height (m)
};

class WakePaths
{
  public:
    static constexpr float kSpacing = 3.0f;    // metres of track between points
    static constexpr float kMaxLength = 360.0f; // metres of track a wake keeps
    static constexpr float kLife = 45.0f;      // seconds a point of track is kept
    static constexpr float kFadeAge = 18.0f;   // seconds over which the waves left at a point fade to 1/e
    static constexpr float kMinSpeed = 0.5f;   // m/s: the waves of slower hulls are no longer than this speed's
    static constexpr float kCuspTan = 0.35355339f; // tan(19.47 degrees)
    static constexpr float kGravity = 9.81f;
    static constexpr float kLeadReturn = 0.3f; // m/s at which a lead under kSpacing is taken back
    static constexpr uint64_t kFree = 1ull << 63; // identities of the tracks the waves left run on as

    struct Point
    {
        double x = 0.0, z = 0.0; // the bow, absolute world metres
        float speed = 0.0f;      // m/s of the waves there (the speed that made them)
        double time = 0.0;       // game seconds when they were made
    };
    struct Path
    {
        uint64_t identity = 0;
        Point head;                // the bow now
        std::deque<Point> points;  // laid track, newest first
        std::array<float, 2> forward{0.0f, 1.0f}; // the hull's heading now (x, z)
        float length = 1.0f;       // the hull's waterline (m)
        float half_beam = 0.5f;    // m
        double seen = 0.0;         // game seconds of the last update
        double lead = 0.0;         // metres the waves' source has run on ahead of the bow
        float wave_speed = 0.0f;   // m/s of the waves on the track (their source's)
        double parted = 0.0;       // game seconds since when the waves have run ahead of the hull
        double moved = 0.0;        // game seconds of the last advance of the lead
        bool free = false;         // waves the hull left, running on without it (no bow: `head` is where it was)
    };

    // The waves' height for a hull of waterline `length` (m) at `speed` (m/s): grows with U^2 / g, the scale of
    // a ship's waves, and with its Froude number up to the hump at about 0.4.
    static float Amplitude(float speed, float length)
    {
        const float u = std::max(std::fabs(speed), 0.0f);
        const float froude = u / std::sqrt(kGravity * std::max(length, 1.0f));
        return 0.08f * u * u / kGravity * std::clamp(froude * 2.5f, 0.3f, 1.0f);
    }
    static float Wavenumber(float speed)
    {
        const float u = std::max(std::fabs(speed), kMinSpeed);
        return kGravity / (u * u);
    }

    // This frame's hulls (WaveSource::Kind::Hull with an identity; others are ignored) at game time `now`.
    void Update(std::span<const WaveSource> sources, double now)
    {
        for (const WaveSource &s : sources)
        {
            if (s.kind != WaveSource::Kind::Hull || s.identity == 0)
                continue;
            Path *path = Find(s.identity);
            const float speed = std::fabs(s.speed);
            const Point bow{s.position[0] + s.forward[0] * s.stem, s.position[1] + s.forward[1] * s.stem, speed, now};
            if (path && Distance(path->head, bow) > 50.0)
                *path = {}; // a jump (teleport, a new scene): the old track is somewhere else
            if (!path || path->points.empty())
            {
                if (!path)
                {
                    paths_.push_back({});
                    path = &paths_.back();
                }
                path->identity = s.identity;
                Restart(*path, bow, now);
            }
            else
                Advance(*path, now, speed, bow);
            path->head = bow;
            path->forward = s.forward;
            path->length = std::max(s.stem - s.stern, 1.0f);
            path->half_beam = std::max(s.half_width, 0.5f);
            path->seen = now;
            // A point every kSpacing metres of the bow's way, with the waves on the track there: the hull's own, or
            // those it made faster that have run on past it (higher: Amplitude), made when they parted.
            if (Distance(path->points.front(), bow) >= kSpacing)
                path->points.push_front({bow.x, bow.z, path->wave_speed, path->parted});
        }
        // The waves left by hulls not seen in this frame run on without them.
        for (size_t i = 0; i < paths_.size(); ++i)
            if (paths_[i].moved < now)
                Advance(paths_[i], now, 0.0f, paths_[i].head);
        for (Path &path : parted_)
            paths_.push_back(std::move(path));
        parted_.clear();
        // Old track, and hulls no longer seen once their waves have gone.
        for (Path &path : paths_)
        {
            float length = 0.0f;
            const Point *previous = &path.head;
            size_t keep = 0;
            for (const Point &p : path.points)
            {
                length += static_cast<float>(Distance(*previous, p));
                previous = &p;
                ++keep;
                if (length > kMaxLength || now - p.time > kLife)
                    break;
            }
            path.points.resize(keep);
        }
        std::erase_if(paths_, [&](const Path &p) { return now - p.seen > kLife; });
    }

    void Reset() { paths_.clear(); }
    const std::vector<Path> &Paths() const { return paths_; }

    // The highest waves (m) left on a track at `now`: whether it is worth a draw.
    static float Height(const Path &path, double now)
    {
        float height = path.free ? 0.0f : Amplitude(path.head.speed, path.length);
        for (const Point &p : path.points)
            height = std::max(height, Amplitude(p.speed, path.length) * std::exp(-static_cast<float>(now - p.time) / kFadeAge));
        return height;
    }

    // The ribbon of `path` as a triangle strip (two vertices per point, bow first) in a target of `extent` metres
    // centred on `centre` (absolute world x, z); `flip_y`: the target's rows run from the top (not GL). Returns the
    // vertices appended to `out` (0: nothing to draw).
    static size_t Ribbon(const Path &path, double now, std::array<double, 2> centre, float extent, bool flip_y,
                         std::vector<WakeVertex> &out)
    {
        const size_t count = path.points.size() + 1;
        if (count < 2)
            return 0;
        const auto at = [&](size_t i) -> const Point & { return i == 0 ? path.head : path.points[i - 1]; };
        const size_t first = out.size();
        const float lead = static_cast<float>(path.lead);
        float along = lead; // metres behind the waves' source: the lead, then the track behind the bow
        float limit = 1e9f; // the narrowest turning radius met so far: a wider ribbon would fold
        double last_bx = -path.forward[0], last_bz = -path.forward[1];
        const auto push = [&](double px, double pz, double bx, double bz, float width, float fade, float x, float k0,
                              float amplitude) {
            // The side (positive towards (-back z, back x), as fs_fx_wake.sc reads it).
            const double sx = -bz, sz = bx;
            for (const float d : {-width, width})
            {
                const double wx = px + sx * d - centre[0], wz = pz + sz * d - centre[1];
                const float cx = static_cast<float>(wx / (0.5 * extent));
                const float cy = static_cast<float>(wz / (0.5 * extent)) * (flip_y ? -1.0f : 1.0f);
                out.push_back({cx, cy, fade, x, d, k0, static_cast<float>(bx), static_cast<float>(bz), amplitude});
            }
        };
        // Ahead of the bow, the waves that have run on past it: their source `lead` metres on along the course (back
        // from the bow is the track's direction there, as below), their energy half as far; laid at the newest point.
        if (lead > kSpacing)
        {
            const Point &bow = path.head;
            size_t o = 1;
            while (o + 1 < count && Distance(bow, at(o)) < 0.5 * kSpacing)
                ++o;
            double bx = at(o).x - bow.x, bz = at(o).z - bow.z;
            const double norm = std::hypot(bx, bz);
            if (norm >= 0.5 * kSpacing)
                bx /= norm, bz /= norm;
            else
                bx = last_bx, bz = last_bz;
            last_bx = bx, last_bz = bz;
            const Point &src = at(1);
            const float k0 = Wavenumber(src.speed), amplitude = Amplitude(src.speed, path.length);
            const float fade = std::exp(-static_cast<float>(now - src.time) / kFadeAge);
            const float reach = 0.5f * lead;
            const int steps = static_cast<int>(std::ceil(reach / kSpacing));
            for (int k = steps; k >= 1; --k)
            {
                const float ahead = reach * static_cast<float>(k) / static_cast<float>(steps);
                const float x = lead - ahead;
                const float width = path.half_beam + x * kCuspTan * 1.3f + 3.0f / k0;
                push(bow.x - bx * ahead, bow.z - bz * ahead, bx, bz, width,
                     fade * std::clamp((reach - ahead) / (0.3f * reach), 0.0f, 1.0f), x, k0, amplitude);
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            const Point &p = at(i);
            if (i > 0)
                along += static_cast<float>(Distance(at(i - 1), p));
            // The waves at the bow of a hull they ran on past are those laid at the newest point.
            const Point &src = i == 0 && lead > 0.0f && count > 1 ? at(1) : p;
            // Back along the track: from the newer neighbour to the older one. At the bow, towards the first point at
            // least half a spacing behind it (the bow sits on a point when one is laid, or wanders round a stopped
            // hull's last one); none: back along the hull.
            const Point &newer = at(i > 0 ? i - 1 : 0);
            size_t o = i + 1 < count ? i + 1 : count - 1;
            if (i == 0)
                while (o + 1 < count && Distance(p, at(o)) < 0.5 * kSpacing)
                    ++o;
            const Point &older = at(o);
            double bx = older.x - newer.x, bz = older.z - newer.z;
            const double norm = std::hypot(bx, bz);
            if (norm < (i == 0 ? 0.5 * kSpacing : 1e-6))
            {
                bx = last_bx;
                bz = last_bz;
            }
            else
            {
                bx /= norm;
                bz /= norm;
            }
            last_bx = bx, last_bz = bz;
            const float x = along;
            const float k0 = Wavenumber(src.speed);
            float width = path.half_beam + x * kCuspTan * 1.3f + 3.0f / k0;
            // The radius of the turn at this laid point, from the laid points about the ribbon's width before and after
            // it (not the bow: it may be any fraction of a spacing past the last point, where a centimetre aside is a
            // sharp turn; nor the next points alone: the bow swings with the hull's yaw on the swell, a wiggle of
            // centimetres that only a turn as long as the ribbon is wide could fold).
            const size_t reach = std::clamp<size_t>(static_cast<size_t>(width / kSpacing), 1, 12);
            if (i > reach && i + reach < count)
            {
                const Point &a = at(i - reach), &c = at(i + reach);
                const double ab = Distance(a, p), bc = Distance(p, c), ac = Distance(a, c);
                const double area2 = std::fabs((p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z));
                if (area2 > 1e-9)
                    limit = std::min(limit, static_cast<float>(ab * bc * ac / (2.0 * area2)) * 0.9f);
            }
            width = std::max(std::min(width, limit), path.half_beam);
            const float age = static_cast<float>(now - src.time);
            float fade = std::exp(-age / kFadeAge);
            // The far end and the oldest points fade out.
            fade *= std::clamp((kMaxLength - (along - lead)) / (0.25f * kMaxLength), 0.0f, 1.0f);
            fade *= std::clamp((kLife - age) / (0.25f * kLife), 0.0f, 1.0f);
            if (i + 1 == count)
                fade = 0.0f;
            push(p.x, p.z, bx, bz, width, fade, x, k0, Amplitude(src.speed, path.length));
        }
        return out.size() - first;
    }

  private:
    static double Distance(const Point &a, const Point &b) { return std::hypot(a.x - b.x, a.z - b.z); }
    static void Restart(Path &path, const Point &bow, double now)
    {
        path.points.assign(1, bow);
        path.lead = 0.0;
        path.wave_speed = bow.speed;
        path.parted = path.moved = now;
    }
    // The waves' source runs on at their speed, the hull at `speed`; `bow`: where the hull's bow is now.
    void Advance(Path &path, double now, float speed, const Point &bow)
    {
        const double dt = std::max(now - path.moved, 0.0);
        path.moved = now;
        if (speed >= path.wave_speed)
        {
            // The hull makes the waves (as fast as those on the track or faster).
            path.wave_speed = speed;
            path.lead = std::max(path.lead - kLeadReturn * dt, 0.0);
            path.parted = now;
            return;
        }
        path.lead += (path.wave_speed - speed) * dt;
        if (path.free)
            return;
        // Whether the hull's own waves outweigh those left (heights as the speed squared, those left faded since).
        const float left = path.wave_speed * std::exp(-0.5f * static_cast<float>(now - path.parted) / kFadeAge);
        if (speed < left || speed < kMinSpeed)
            return;
        if (path.lead < kSpacing)
        {
            // The speed wavering: the lead is taken back.
            path.wave_speed = speed;
            path.lead = std::max(path.lead - kLeadReturn * dt, 0.0);
            path.parted = now;
            return;
        }
        Path left_waves = path;
        left_waves.free = true;
        left_waves.identity = kFree | ++parted_count_;
        parted_.push_back(std::move(left_waves));
        Restart(path, bow, now);
    }
    Path *Find(uint64_t identity)
    {
        for (Path &p : paths_)
            if (p.identity == identity)
                return &p;
        return nullptr;
    }
    std::vector<Path> paths_;
    std::vector<Path> parted_; // tracks parted in this update
    uint64_t parted_count_ = 0;
};

} // namespace storm::fx
