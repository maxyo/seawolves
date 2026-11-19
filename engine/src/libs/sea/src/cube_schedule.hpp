// The faces of the sea's environment and sun-road cubes drawn in a frame (SEA::EnvMap_Render, SEA::SunRoad_Render),
// without the renderer: host-checked by tests/sea_cube_schedule.
//
// Both cubes are drawn from the camera mirrored in the sea (its height negated) and the Sea2 pixel shader reads them
// in the reflected ray (fs_port_sea2.sc). A face drawn from where the camera is shows the reflections where the view
// expects them. A face drawn earlier shows each reflected thing displaced by how far the camera has moved relative to
// it since, over its distance: a camera orbiting a ship 30 m off at 40 m/s is 0.7 m off after one frame (1.3 degrees);
// a camera that follows a ship sailing straight keeps the ship's reflection exact in a face of any age, and the world
// around moves by the ship's way over its distance. So the faces go by need, within the budget of kBudgetPerSecond
// faces (the previous schedule's rate while the camera moved, all faces in turn: never more faces than it drew), the
// most needed first:
// - a face the view reads (ReadFaces) that comes into view older than kReadAge;
// - a face the view reads whose reflections are off by more than kError (Error: the hulls it holds, and the still
//   world kStillDistance away), those the calm sea shows (Read::direct) before those only its tilt reaches, the
//   farthest off first: a dragged camera's reflections stay with it;
// - the read faces in turn by the clock, each kRefreshesPerSecond (what moves within a ship, the clouds); a face drawn
//   for the above counts towards that turn;
// - the faces not read, kUnreadFacesPerSecond (the sun's face of the glint's colour, the sky straight up of the foam,
//   a face only a steep bump reaches).
// The faces the view does not read cost nothing else, so a standing or following camera costs less than before.
// Every face at once: at the start, after a jump (kJump), on a switch above / below the surface, when the sea comes
// back into view and in the first kLoadingFullFrames frames of a load. Under the surface and while a place loads (the
// view is not the player's): every face in turn by the clock. None while no sea is in view (nothing reads the cubes).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace storm::sea
{
class CubeSchedule
{
  public:
    // D3DCUBEMAP_FACES: +X, -X, +Y, -Y, +Z, -Z.
    static constexpr int kFaces = 6;
    static constexpr int kNegativeY = 3;
    static constexpr uint32_t kAll = (1u << kFaces) - 1;
    // The angle (radians) by which a reflection may be off before its face is redrawn: about two pixels of a phone's
    // sea view (1100 rows over its 0.6 radians).
    static constexpr float kError = 1e-3f;
    static constexpr float kStillDistance = 200.0f;  // metres to the nearest still thing reflected (islands, forts)
    static constexpr float kNear = 5.0f;              // metres: a hull's nearest reflection is no nearer
    static constexpr int kMaxObjects = 16;            // hulls kept per face, nearest first
    static constexpr float kJump = 10.0f;             // metres in one frame: every face
    static constexpr float kFacesPerSecond = 60.0f;   // every face in turn under the surface and while loading
    static constexpr float kBudgetPerSecond = 150.0f; // faces drawn at most, by need
    static constexpr float kBudgetBurst = 4.0f;       // faces the budget saves up at most (a long frame for short ones)
    static constexpr float kRefreshesPerSecond = 12.0f; // each read face by the clock (as five faces at 60 a second)
    static constexpr float kUnreadFacesPerSecond = 4.0f;
    static constexpr float kReadAge = 0.1f;           // seconds: a face coming into view older than this is redrawn
    static constexpr uint32_t kLoadingFullFrames = 2;
    // The reflected rays a view reads: its frustum's rays below the horizon mirrored in the flat sea, each widened by
    // this angle (radians) for the bumps' and the swell's tilt of the water (a tilt turns the reflection by twice it).
    static constexpr float kMargin = 0.17f;

    // A thing in the sea the faces reflect (a hull): where it is now.
    struct Object
    {
        uint64_t identity = 0;
        std::array<float, 3> position{};
        float radius = 0.0f; // metres around `position`
    };
    struct Frame
    {
        float dt = 0.0f;              // seconds since the previous frame
        std::array<float, 3> eye{};   // the camera
        const Object *objects = nullptr;
        size_t object_count = 0;
        uint32_t read = kAll;         // ReadFaces of the view: Read::faces
        uint32_t direct = kAll;       // ... Read::direct
        bool under_sea = false;
        bool loading = false;
        bool sea_in_view = true;      // the sea was drawn in the previous frame
    };

    // The cube faces (bit per face) the rays of a view read in the reflection: `faces` all of them, `direct` those the
    // calm sea shows (its rays mirrored as they are; the others only the bumps and the swell reach).
    struct Read
    {
        uint32_t faces = 0, direct = 0;
    };
    // `right`, `up`, `forward`: the view's world axes, `tan_x`, `tan_y` the tangents of its half angles. Rays above the
    // horizon see the sky, not the sea; the shader clamps the reflected ray to the horizon (no -Y).
    static Read ReadFaces(const std::array<float, 3> &right, const std::array<float, 3> &up,
                          const std::array<float, 3> &forward, float tan_x, float tan_y, float margin = kMargin)
    {
        constexpr int kColumns = 13, kRows = 9, kRing = 8;
        const float cm = std::cos(margin), sm = std::sin(margin);
        Read read;
        for (int row = 0; row < kRows; ++row)
            for (int column = 0; column < kColumns; ++column)
            {
                const float x = (2.0f * column / (kColumns - 1) - 1.0f) * tan_x;
                const float y = (2.0f * row / (kRows - 1) - 1.0f) * tan_y;
                std::array<float, 3> d{};
                for (int i = 0; i < 3; ++i)
                    d[i] = forward[i] + x * right[i] + y * up[i];
                if (d[1] >= 0.0f)
                    continue;
                const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                const std::array<float, 3> m{d[0] / length, -d[1] / length, d[2] / length};
                read.direct |= 1u << Face(m);
                // Two axes across the mirrored ray: the ring of rays `margin` off it.
                std::array<float, 3> a{m[2], 0.0f, -m[0]};
                float la = std::sqrt(a[0] * a[0] + a[2] * a[2]);
                if (la < 1e-6f)
                    a = {1.0f, 0.0f, 0.0f}, la = 1.0f;
                a = {a[0] / la, 0.0f, a[2] / la};
                const std::array<float, 3> b{m[1] * a[2] - m[2] * a[1], m[2] * a[0] - m[0] * a[2], m[0] * a[1] - m[1] * a[0]};
                for (int k = 0; k < kRing; ++k)
                {
                    const float angle = 6.2831853f * k / kRing, ca = std::cos(angle), sa = std::sin(angle);
                    std::array<float, 3> r{};
                    for (int i = 0; i < 3; ++i)
                        r[i] = m[i] * cm + sm * (ca * a[i] + sa * b[i]);
                    r[1] = std::max(r[1], 0.0f);
                    read.faces |= 1u << Face(r);
                }
            }
        read.faces |= read.direct;
        return read;
    }

    // The faces to draw in this frame.
    uint32_t Due(const Frame &frame)
    {
        if (frame.loading)
        {
            if (loading_frames_++ < kLoadingFullFrames)
                refresh_all_ = true;
        }
        else
            loading_frames_ = 0;
        if (started_ && (under_sea_ != frame.under_sea || Distance(frame.eye, eye_) > kJump))
            refresh_all_ = true;
        started_ = true;
        under_sea_ = frame.under_sea;
        eye_ = frame.eye;
        for (float &age : age_)
            age += frame.dt;
        if (!frame.sea_in_view && !frame.loading)
        {
            refresh_all_ = true;
            read_ = 0;
            return 0;
        }
        uint32_t due = 0;
        if (refresh_all_)
        {
            refresh_all_ = false;
            owed_ = unread_owed_ = 0.0f;
            budget_ = kBudgetBurst;
            due = kAll;
        }
        else if (frame.loading || frame.under_sea)
        {
            const uint32_t faces = frame.under_sea ? kAll : kAll & ~(1u << kNegativeY);
            owed_ = std::min(owed_ + frame.dt * kFacesPerSecond, static_cast<float>(Count(faces)));
            for (; owed_ >= 1.0f; owed_ -= 1.0f)
                due |= Oldest(faces & ~due);
        }
        else
        {
            const uint32_t read = frame.read & ~(1u << kNegativeY);
            budget_ = std::min(budget_ + frame.dt * kBudgetPerSecond, kBudgetBurst);
            const auto draw = [&](uint32_t bit) {
                if (!bit || budget_ < 1.0f)
                    return false;
                due |= bit;
                budget_ -= 1.0f;
                return true;
            };
            // Coming into view stale, then the farthest off.
            for (int f = 0; f < kFaces; ++f)
                if ((read & (1u << f)) && !(read_ & (1u << f)) && age_[f] > kReadAge)
                    draw(1u << f);
            std::array<std::pair<float, int>, kFaces> off{};
            int count = 0;
            for (int f = 0; f < kFaces; ++f)
                if ((read & ~due & (1u << f)))
                    if (const float error = Error(f, frame); error > kError)
                        off[count++] = {error, f};
            const auto rank = [&](const std::pair<float, int> &e) {
                return std::pair((frame.direct >> e.second) & 1u, e.first);
            };
            std::sort(off.begin(), off.begin() + count, [&](const auto &a, const auto &b) { return rank(a) > rank(b); });
            for (int i = 0; i < count && draw(1u << off[i].second); ++i)
            {
            }
            // The clock's turn for what moves in the read faces.
            owed_ = std::clamp(owed_ + frame.dt * kRefreshesPerSecond * static_cast<float>(Count(read)) -
                                   static_cast<float>(Count(due)),
                               0.0f, static_cast<float>(Count(read)));
            for (; owed_ >= 1.0f && draw(Oldest(read & ~due)); owed_ -= 1.0f)
            {
            }
            const uint32_t unread = kAll & ~read & ~(1u << kNegativeY);
            unread_owed_ = std::min(unread_owed_ + frame.dt * kUnreadFacesPerSecond, 1.0f);
            if (unread_owed_ >= 1.0f && draw(Oldest(unread)))
                unread_owed_ -= 1.0f;
        }
        read_ = frame.read;
        if (due)
            Snapshot(frame);
        for (int f = 0; f < kFaces; ++f)
            if (due & (1u << f))
            {
                captured_[f] = Mirror(frame.eye);
                age_[f] = 0.0f;
                face_objects_[f] = snapshot_;
            }
        return due;
    }

    // How far off (radians) face `f` shows the reflections now: the largest displacement of the mirrored camera since
    // the face was drawn, relative to a hull it holds (then and now) or to the still world, over that thing's distance.
    float Error(int f, const Frame &frame) const
    {
        const std::array<float, 3> eye = Mirror(frame.eye);
        std::array<float, 3> moved{};
        for (int i = 0; i < 3; ++i)
            moved[i] = eye[i] - captured_[f][i];
        float error = Length(moved) / kStillDistance;
        for (size_t k = 0; k < frame.object_count; ++k)
        {
            const Object &now = frame.objects[k];
            if (!Covers(f, now, eye))
                continue;
            for (const Object &then : face_objects_[f])
                if (then.identity == now.identity && then.identity != 0)
                {
                    std::array<float, 3> relative{}, to{};
                    for (int i = 0; i < 3; ++i)
                    {
                        relative[i] = moved[i] - (now.position[i] - then.position[i]);
                        to[i] = now.position[i] - eye[i];
                    }
                    error = std::max(error, Length(relative) / std::max(Length(to) - now.radius, kNear));
                    break;
                }
        }
        return error;
    }

    // The targets were made again (they hold nothing): every face in the next frame.
    void RefreshAll() { refresh_all_ = true; }

    // Where face `f` was drawn from (the mirrored camera then).
    const std::array<float, 3> &Captured(int f) const { return captured_[f]; }

  private:
    static int Face(const std::array<float, 3> &v)
    {
        const float ax = std::fabs(v[0]), ay = std::fabs(v[1]), az = std::fabs(v[2]);
        if (ax >= ay && ax >= az)
            return v[0] >= 0.0f ? 0 : 1;
        if (ay >= az)
            return v[1] >= 0.0f ? 2 : 3;
        return v[2] >= 0.0f ? 4 : 5;
    }
    static float Length(const std::array<float, 3> &v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
    static float Distance(const std::array<float, 3> &a, const std::array<float, 3> &b)
    {
        return Length({a[0] - b[0], a[1] - b[1], a[2] - b[2]});
    }
    static std::array<float, 3> Mirror(const std::array<float, 3> &v) { return {v[0], -v[1], v[2]}; }
    // Whether face `f` seen from `eye` holds part of `object`: its centre, its ends across and along the sea, and its
    // masts (twice its radius up).
    static bool Covers(int f, const Object &object, const std::array<float, 3> &eye)
    {
        const float r = object.radius;
        const std::array<std::array<float, 3>, 6> offsets{{{0.0f, 0.0f, 0.0f}, {r, 0.0f, 0.0f}, {-r, 0.0f, 0.0f},
                                                           {0.0f, 0.0f, r}, {0.0f, 0.0f, -r}, {0.0f, 2.0f * r, 0.0f}}};
        for (const auto &o : offsets)
            if (Face({object.position[0] + o[0] - eye[0], object.position[1] + o[1] - eye[1],
                      object.position[2] + o[2] - eye[2]}) == f)
                return true;
        return false;
    }
    // The frame's hulls nearest the camera, as the faces drawn now hold them.
    void Snapshot(const Frame &frame)
    {
        snapshot_.clear();
        for (size_t k = 0; k < frame.object_count; ++k)
            snapshot_.push_back(frame.objects[k]);
        const auto distance = [&](const Object &o) { return Distance(o.position, frame.eye); };
        if (snapshot_.size() > static_cast<size_t>(kMaxObjects))
        {
            std::nth_element(snapshot_.begin(), snapshot_.begin() + kMaxObjects, snapshot_.end(),
                             [&](const Object &a, const Object &b) { return distance(a) < distance(b); });
            snapshot_.resize(kMaxObjects);
        }
    }
    static int Count(uint32_t faces)
    {
        int n = 0;
        for (; faces; faces &= faces - 1)
            ++n;
        return n;
    }
    // The face of `faces` drawn longest ago (0: none).
    uint32_t Oldest(uint32_t faces) const
    {
        int oldest = -1;
        for (int f = 0; f < kFaces; ++f)
            if ((faces & (1u << f)) && (oldest < 0 || age_[f] > age_[oldest]))
                oldest = f;
        return oldest < 0 ? 0u : 1u << oldest;
    }

    std::array<std::array<float, 3>, kFaces> captured_{}; // the mirrored camera each face was drawn from
    std::array<std::vector<Object>, kFaces> face_objects_; // the hulls as each face holds them
    std::vector<Object> snapshot_;
    std::array<float, kFaces> age_{};
    std::array<float, 3> eye_{};
    uint32_t read_ = 0, loading_frames_ = 0;
    float owed_ = 0.0f, unread_owed_ = 0.0f, budget_ = kBudgetBurst;
    bool started_ = false, refresh_all_ = true, under_sea_ = false;
};
} // namespace storm::sea
