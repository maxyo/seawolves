// The sea's cube faces (cube_schedule.hpp) against the previous schedule (every face in turn by the clock: 60 faces a
// second, 150 while the camera moves), in the camera moves of the sea: dragged round the ship, following it under sail
// past another ship and an island, standing, turning in place; at 60 frames a second and at uneven frame times.
//
// Each face holds the scene as it was seen from the mirrored camera when the face was drawn. In each frame, for every
// reflected point the view shows (the mast heads and hull of the ship, the other ship, the island), its reflection is
// off by the displacement of the mirrored camera since its face was drawn, relative to the point, over the point's
// distance: printed in pixels of a phone's sea view (1100 rows). Also the faces drawn per frame (the cost).
#include "cube_schedule.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using storm::sea::CubeSchedule;
using Vec = std::array<float, 3>;

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

Vec Add(Vec a, Vec b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec Sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec Scale(Vec a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float Dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float Length(Vec a) { return std::sqrt(Dot(a, a)); }
Vec Normal(Vec a) { return Scale(a, 1.0f / Length(a)); }
Vec Cross(Vec a, Vec b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
Vec Mirror(Vec a) { return {a[0], -a[1], a[2]}; }
int Face(Vec v)
{
    const float ax = std::fabs(v[0]), ay = std::fabs(v[1]), az = std::fabs(v[2]);
    if (ax >= ay && ax >= az)
        return v[0] >= 0.0f ? 0 : 1;
    if (ay >= az)
        return v[1] >= 0.0f ? 2 : 3;
    return v[2] >= 0.0f ? 4 : 5;
}

// The schedule before cube_schedule.hpp (SEA::CubeFacesDue): five faces above the surface in turn by the clock.
class Previous
{
  public:
    uint32_t Due(const CubeSchedule::Frame &frame)
    {
        const float moved = Length(Sub(frame.eye, eye_));
        if (started_ && moved > 10.0f)
            all_ = true;
        const float rate = frame.dt > 0.0f && moved > 0.5f * frame.dt ? 150.0f : 60.0f;
        started_ = true;
        eye_ = frame.eye;
        if (all_)
        {
            all_ = false;
            due_ = 0.0f;
            return CubeSchedule::kAll;
        }
        constexpr int faces[5] = {0, 1, 2, 4, 5};
        due_ = std::min(due_ + frame.dt * rate, 5.0f);
        uint32_t due = 0;
        for (; due_ >= 1.0f; due_ -= 1.0f)
            due |= 1u << faces[cursor_++ % 5];
        return due;
    }

  private:
    Vec eye_{};
    float due_ = 0.0f;
    uint32_t cursor_ = 0;
    bool started_ = false, all_ = true;
};

constexpr float kHalfAngle = 1.285f * 0.5f;      // the sea cameras' horizontal angle (AICameras.c)
constexpr float kAspect = 1440.0f / 3200.0f;     // a phone in landscape
constexpr float kRows = 1100.0f;                 // the sea view's rows (preset Высокая)
const float kTanX = std::tan(kHalfAngle), kTanY = std::tan(kHalfAngle * kAspect); // storm::math::projectionMatrix
const float kPixel = 2.0f * std::atan(kTanY) / kRows;                           // radians per row

struct Point
{
    uint64_t identity; // the hull it moves with (0: the still world)
    Vec offset;        // from that hull, or the world position
};

struct Pose
{
    Vec eye, target;
    Vec ship, other; // the hulls: the camera's ship, another
};

struct Result
{
    float worst_px = 0.0f;   // the largest error of a reflection on screen
    int frames_over = 0;     // frames with an error over 2.5 pixels
    int frames = 0;
    float faces = 0.0f;      // faces drawn per frame
};

template <class Schedule>
Result Run(const char *name, const char *label, int frames, float (*dt_of)(int), Pose (*pose_of)(float))
{
    Schedule schedule;
    // What each face holds: the mirrored camera and the hulls when it was drawn.
    std::array<Vec, 6> eye_at{};
    std::array<std::array<Vec, 2>, 6> hulls_at{};
    const std::vector<Point> points = {
        {1, {0.0f, 32.0f, 12.0f}}, {1, {0.0f, 28.0f, -10.0f}}, {1, {0.0f, 4.0f, 18.0f}}, {1, {0.0f, 4.0f, -18.0f}},
        {2, {0.0f, 26.0f, 0.0f}},  {0, {160.0f, 25.0f, 260.0f}}, {0, {-220.0f, 35.0f, 120.0f}},
    };
    Result result;
    long drawn = 0;
    float time = 0.0f;
    for (int frame = 0; frame < frames; ++frame)
    {
        const float dt = frame == 0 ? 0.0f : dt_of(frame);
        time += dt;
        const Pose pose = pose_of(time);
        const Vec forward = Normal(Sub(pose.target, pose.eye));
        const Vec right = Normal(Cross({0.0f, 1.0f, 0.0f}, forward));
        const Vec up = Cross(forward, right);
        const std::array<CubeSchedule::Object, 2> objects = {CubeSchedule::Object{1, pose.ship, 20.0f},
                                                             CubeSchedule::Object{2, pose.other, 18.0f}};
        CubeSchedule::Frame f;
        f.dt = dt;
        f.eye = pose.eye;
        f.objects = objects.data();
        f.object_count = objects.size();
        const CubeSchedule::Read read = CubeSchedule::ReadFaces(right, up, forward, kTanX, kTanY);
        f.read = read.faces;
        f.direct = read.direct;
        const uint32_t due = schedule.Due(f);
        for (int face = 0; face < 6; ++face)
            if (due & (1u << face))
            {
                eye_at[face] = Mirror(pose.eye);
                hulls_at[face] = {pose.ship, pose.other};
                ++drawn;
            }
        const Vec mirrored = Mirror(pose.eye);
        float worst = 0.0f;
        for (const Point &p : points)
        {
            const Vec base = p.identity == 1 ? pose.ship : p.identity == 2 ? pose.other : Vec{};
            const Vec q = Add(base, p.offset);
            // On screen: the reflection is where the view sees the point mirrored in the sea.
            const Vec image = Sub(Mirror(q), pose.eye);
            const float z = Dot(image, forward);
            if (z <= 0.0f || std::fabs(Dot(image, right)) > z * kTanX || std::fabs(Dot(image, up)) > z * kTanY)
                continue;
            const int face = Face(Sub(q, mirrored));
            Vec moved = Sub(mirrored, eye_at[face]);
            if (p.identity)
                moved = Sub(moved, Sub(base, hulls_at[face][p.identity - 1]));
            worst = std::max(worst, Length(moved) / Length(Sub(q, mirrored)) / kPixel);
        }
        if (frame > 0)
        {
            ++result.frames;
            result.worst_px = std::max(result.worst_px, worst);
            result.frames_over += worst > 2.5f;
        }
    }
    result.faces = static_cast<float>(drawn - 6) / static_cast<float>(frames - 1);
    std::printf("%-30s %-9s reflections off by up to %5.1f px, over 2.5 px in %3d of %d frames; %.2f faces a frame\n",
                name, label, result.worst_px, result.frames_over, result.frames, result.faces);
    return result;
}

float Steady(int) { return 1.0f / 60.0f; }
float Steady30(int) { return 1.0f / 30.0f; }
float Uneven(int frame)
{
    // 11..25 ms, a fixed sequence (frame pacing on a busy phone).
    uint32_t h = static_cast<uint32_t>(frame) * 2654435761u;
    h ^= h >> 13;
    return (11.0f + static_cast<float>(h % 15u)) / 1000.0f;
}

// The ship camera dragged round the ship at 1.5 radians a second, 40 m out and 14 m up (40 m/s).
Pose Drag(float t)
{
    const float az = 1.5f * t;
    const Vec ship{0.0f, 0.0f, 0.0f};
    return {{40.0f * std::sin(az), 14.0f, 40.0f * std::cos(az)}, {0.0f, 8.0f, 0.0f}, ship, {70.0f, 0.0f, -90.0f}};
}
// The same from higher up (22 m, 33 m out, looking down 12 degrees): the view also reads the sky face.
Pose DragHigh(float t)
{
    const float az = 1.5f * t;
    return {{33.0f * std::sin(az), 22.0f, 33.0f * std::cos(az)}, {0.0f, 15.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
            {70.0f, 0.0f, -90.0f}};
}
// Following the ship sailing at 6 m/s, from astern on its quarter; another ship passes 60 m off at 7 m/s the other way.
Pose Sail(float t)
{
    const Vec ship{0.0f, 0.0f, 6.0f * t};
    return {Add(ship, {-20.0f, 14.0f, -36.0f}), Add(ship, {0.0f, 8.0f, 0.0f}), ship, {60.0f, 0.0f, 120.0f - 7.0f * t}};
}
// Standing behind the ship lying still.
Pose Stand(float)
{
    return {{-20.0f, 14.0f, -36.0f}, {0.0f, 8.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {70.0f, 0.0f, -90.0f}};
}
// Turning in place (the deck camera looking round) at 1 radian a second.
Pose Turn(float t)
{
    const Vec eye{0.0f, 9.0f, 0.0f};
    return {eye, Add(eye, {std::sin(t), -0.35f, std::cos(t)}), {0.0f, 0.0f, 6.0f}, {70.0f, 0.0f, -90.0f}};
}
} // namespace

int main()
{
    struct Case
    {
        const char *name;
        float (*dt)(int);
        Pose (*pose)(float);
        int frames;
    };
    const Case cases[] = {
        {"drag, 60 fps", Steady, Drag, 240},
        {"drag, uneven frames", Uneven, Drag, 240},
        // At 30 frames a second the previous schedule drew every face in each frame: nothing lagged there.
        {"drag, 30 fps", Steady30, Drag, 120},
        {"drag from higher, 60 fps", Steady, DragHigh, 240},
        {"drag from higher, uneven frames", Uneven, DragHigh, 240},
        {"following under sail, 60 fps", Steady, Sail, 600},
        {"following, uneven frames", Uneven, Sail, 600},
        {"standing, 60 fps", Steady, Stand, 300},
        {"turning in place, 60 fps", Steady, Turn, 300},
    };
    for (const Case &c : cases)
    {
        const Result before = Run<Previous>(c.name, "previous", c.frames, c.dt, c.pose);
        const Result after = Run<CubeSchedule>(c.name, "now", c.frames, c.dt, c.pose);
        // Never more faces than the previous schedule drew (its rate while the camera moved is the budget).
        CHECK(after.faces <= before.faces + 0.01f);
        if ((c.pose == Drag || c.pose == DragHigh) && c.dt != Steady30)
        {
            // The defect this reproduces: a dragged camera's reflections lag it, by tens of pixels in every other
            // frame. Now they stay within the schedule's error (about two pixels), but for a face the budget leaves
            // a frame behind now and then (frames shorter than the budget's 6.7 ms a face, three faces in view).
            CHECK(before.worst_px > 10.0f && before.frames_over > before.frames / 3);
            CHECK(after.frames_over * 10 < before.frames_over && after.frames_over <= after.frames / 20);
        }
        else
        {
            CHECK(after.worst_px <= CubeSchedule::kError / kPixel + 1.0f);
            CHECK(after.frames_over == 0);
        }
        if (c.pose == Sail || c.pose == Stand)
            CHECK(after.faces < before.faces);
    }
    // The faces a view reads: a camera looking down the sea reads its forward side faces and the sky above, never -Y.
    {
        const uint32_t read = CubeSchedule::ReadFaces({1.0f, 0.0f, 0.0f}, {0.0f, 0.96f, 0.28f}, {0.0f, -0.28f, 0.96f},
                                                      kTanX, kTanY).faces;
        CHECK(read & (1u << 4));
        CHECK(!(read & (1u << 5)));
        CHECK(!(read & (1u << 3)));
        // Looking at the sky only: no sea in view.
        CHECK(CubeSchedule::ReadFaces({1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, -0.87f}, {0.0f, 0.87f, 0.5f}, kTanX, kTanY).faces == 0);
    }
    std::printf("sea cube schedule checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
