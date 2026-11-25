// The world map's labels: the side of its point each one takes (wdm_label_layout.hpp) and where it lands on the screen
// for a camera (wdm_label_screen.hpp).
#include "wdm_label_screen.hpp"

#include <catch2/catch.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using storm::worldmap::LabelBox;
using storm::worldmap::LabelLayout;
using storm::worldmap::LabelSide;

namespace
{
// A 100 x 20 label at the point (x, y)
LabelBox Box(float x, float y, uint32_t weight, LabelSide side = LabelSide::None)
{
    LabelBox box;
    box.x = x, box.y = y, box.width = 100.0f, box.height = 20.0f, box.weight = weight, box.side = side;
    return box;
}
std::vector<LabelSide> Sides(const std::vector<LabelBox> &boxes)
{
    std::vector<LabelSide> out;
    for (const LabelBox &box : boxes)
        out.push_back(box.side);
    return out;
}
bool Overlap(const LabelBox &a, const LabelBox &b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
constexpr float gap = 8.0f, hold = 6.0f;
} // namespace

TEST_CASE("the size above a kind's height: 1 up to it, then heightView / height, never below the floor")
{
    using storm::worldmap::HeightScale;
    CHECK(HeightScale(250, 100, .75f) == 1);
    CHECK(HeightScale(250, 250, .75f) == 1);
    CHECK(HeightScale(250, 300, .75f) == Approx(250.0f / 300));
    CHECK(HeightScale(200, 500, .75f) == .75f);
    CHECK(HeightScale(1000, 500, .75f) == 1);
    CHECK(HeightScale(0, 500, .75f) == 1);
    CHECK(HeightScale(250, std::numeric_limits<float>::quiet_NaN(), .75f) == 1);
}

TEST_CASE("apart: every name under its point (the game's place), clear of the others")
{
    LabelLayout layout;
    std::vector<LabelBox> boxes{Box(0, 0, 1), Box(300, 0, 1), Box(0, 100, 50)};
    layout.Place(boxes, gap, hold);
    using enum LabelSide;
    CHECK(Sides(boxes) == std::vector<LabelSide>{Below, Below, Below});
    CHECK((boxes[0].clear && boxes[1].clear && boxes[2].clear));
    CHECK((boxes[0].left == -50 && boxes[0].top == 0 && boxes[0].right == 50 && boxes[0].bottom == 20));
}

TEST_CASE("colliding: the heavier keeps its place, the lighter takes the first free side")
{
    LabelLayout layout;
    using enum LabelSide;
    std::vector<LabelBox> boxes{Box(10, -12, 1), Box(0, 0, 100)};
    layout.Place(boxes, gap, hold);
    CHECK(Sides(boxes) == std::vector<LabelSide>{Above, Below});
    CHECK(boxes[0].clear);
    CHECK_FALSE(Overlap(boxes[0], boxes[1]));
    // Above taken too: right of the point.
    boxes = {Box(0, 0, 100), Box(0, -30, 100), Box(60, 2, 1)};
    layout.Place(boxes, gap, hold);
    CHECK(boxes[2].side == Right);
    CHECK(boxes[2].clear);
    CHECK(boxes[2].left == 60 + gap * 0.5f);
    for (size_t i = 0; i < 2; ++i)
        CHECK_FALSE(Overlap(boxes[i], boxes[2]));
}

TEST_CASE("no free side: the name still shows under its point, not clear")
{
    LabelLayout layout;
    std::vector<LabelBox> boxes{Box(0, 0, 100), Box(0, -25, 100), Box(-110, -10, 100), Box(110, -10, 100),
                                Box(0, 1, 1)};
    layout.Place(boxes, gap, hold);
    CHECK(boxes[4].side == LabelSide::Below);
    CHECK_FALSE(boxes[4].clear);
}

TEST_CASE("a camera jittering across the gap's edge does not swap sides every frame")
{
    LabelLayout layout;
    std::vector<LabelBox> boxes;
    int changes = 0;
    LabelSide last = LabelSide::Below;
    for (int frame = 0; frame < 200; ++frame)
    {
        // From 30 px apart to 0 and back, with a +-1 px jitter every frame.
        const float distance = (frame < 100 ? 30.0f - 0.3f * frame : 0.3f * (frame - 100)) + (frame % 2 ? 1.0f : -1.0f);
        const LabelSide side = boxes.empty() ? LabelSide::Below : boxes[1].side;
        boxes = {Box(0, 0, 100, LabelSide::Below), Box(100.0f + distance, 0, 1, side)};
        layout.Place(boxes, gap, hold);
        changes += boxes[1].side != last;
        last = boxes[1].side;
    }
    CHECK(changes == 1); // moved once on the way in, keeps the free side on the way out
}

TEST_CASE("the same boxes and last sides give the same result")
{
    std::vector<LabelBox> a{Box(0, 0, 50), Box(40, 0, 50), Box(80, 0, 50), Box(120, 0, 50)}, b = a;
    LabelLayout layout, other;
    layout.Place(a, gap, hold);
    other.Place(b, gap, hold);
    CHECK(Sides(a) == Sides(b));
    for (const LabelBox &box : a)
        CHECK(box.side != LabelSide::None);
}

namespace
{
// One label of 150 x 30 px at the font's size; pxPerDp 1.
struct Screen
{
    storm::worldmap::LabelScreen screen;
    std::vector<storm::worldmap::MapLabel> labels = std::vector<storm::worldmap::MapLabel>(1);
    float cameraHeight = 1;
    Screen()
    {
        auto &label = labels[0];
        label.width = 150, label.height = 30, label.heightView = 100000, label.weight = 100, label.icon = -1;
        label.text = "Dominica";
    }
    storm::worldmap::MapLabel &Render(const glm::mat4 &mtx, float w, float h, float scale = 1)
    {
        screen.Place(labels, mtx, w, h, scale, 1.0f, cameraHeight);
        return labels[0];
    }
};

// The label stands centred under (x, y) at `scale` times its size.
void RequireAnchored(const storm::worldmap::MapLabel &label, float x, float y, float scale = 1)
{
    CHECK((label.l + label.r) * 0.5f == Approx(x).margin(.003f));
    CHECK(label.t == Approx(y).margin(.003f));
    CHECK(label.r - label.l == Approx(label.width * scale).margin(.003f));
}
void RequireShown(const storm::worldmap::MapLabel &label, bool shown)
{
    CHECK(label.onScreen == shown);
    CHECK(label.placed == shown);
}
} // namespace

TEST_CASE("a label stays at its world point's projection and leaves the viewport without a clamp")
{
    Screen screen;
    auto &label = screen.labels[0];
    for (const auto viewport : {std::array<float, 2>{960, 540}, {1280, 720}})
    {
        const auto [w, h] = viewport;
        const auto projection = storm::math::projectionMatrix(1.2f, w, h, 1, 10000);
        const float x = w - 195, y = 150, z = 500;
        label.pos = {(x * 2 / w - 1) * z / projection[0][0], (1 - y * 2 / h) * z / projection[1][1], z};
        RequireAnchored(screen.Render(projection, w, h), x, y);
        RequireShown(label, true);
        for (const float edgeX : {-40.0f, w + 40.0f, w + 160.0f})
        {
            label.pos.x = (edgeX * 2 / w - 1) * z / projection[0][0];
            RequireAnchored(screen.Render(projection, w, h), edgeX, y);
            RequireShown(label, edgeX < w + label.width / 2);
        }
    }
}

TEST_CASE("a fixed world point through camera pan, pitch, yaw, distance and zoom")
{
    // The oracle: the point in camera coordinates and the field of view's tangent.
    Screen screen;
    auto &label = screen.labels[0];
    label.pos = {100, 20, 1000};
    for (const float yaw : {-0.2f, 0.0f, 0.2f})
        for (const float pitch : {-0.15f, 0.0f, 0.15f})
            for (const float pan : {-70.0f, 70.0f})
                for (const float distance : {-200.0f, 200.0f})
                    for (const float fov : {0.8f, 1.4f})
                    {
                        const float w = 1280, h = 720, scale = 1.5f;
                        const auto view = glm::inverse(storm::math::eulerMatrix(pitch, yaw, 0, pan, 0, distance));
                        const auto camera = view * glm::vec4(label.pos, 1);
                        const float x = w / 2 + camera.x / camera.z / std::tan(fov / 2) * w / 2;
                        const float y = h / 2 - camera.y / camera.z / std::tan(h / w * fov / 2) * h / 2;
                        screen.Render(storm::math::projectionMatrix(fov, w, h, 1, 10000) * view, w, h, scale);
                        RequireAnchored(label, x, y, scale);
                        RequireShown(label, true);
                    }
}

TEST_CASE("above its kind's height a label still shows, shrunk down to the floor")
{
    Screen screen;
    auto &label = screen.labels[0];
    label.pos = {0, 0, 500};
    label.heightView = 250;
    const auto projection = storm::math::projectionMatrix(1.2f, 960, 540, 1, 10000);
    for (const auto &[height, expected] :
         std::vector<std::pair<float, float>>{{100, 1}, {250, 1}, {280, 250.f / 280}, {400, .625f}, {1000, .25f}})
    {
        screen.cameraHeight = height;
        RequireAnchored(screen.Render(projection, 960, 540, 2), 480, 270,
                        2 * std::max(expected, storm::worldmap::kLabelFarScale));
        RequireShown(label, true);
    }
}

TEST_CASE("points behind the camera, on its plane or not finite show no label")
{
    // The projection's clip w is the view z: a point behind the camera would project mirrored.
    Screen screen;
    auto &label = screen.labels[0];
    const auto projection = storm::math::projectionMatrix(1.2f, 960, 540, 1, 10000);
    for (const float z : {500.0f, -500.0f, 500.0f, 0.0f, 500.0f})
    {
        label.pos = {0, 0, z};
        screen.Render(projection, 960, 540);
        RequireShown(label, z > 0);
        if (z > 0)
            RequireAnchored(label, 480, 270);
    }
    for (const float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        label.pos = {bad, 0, 500};
        screen.Render(projection, 960, 540);
        RequireShown(label, false);
    }
}

TEST_CASE("the labels that show come lighter first: a covered one lies under the heavier")
{
    Screen screen;
    screen.labels.resize(3, screen.labels[0]);
    const uint32_t weights[3] = {100, 1, 50};
    for (size_t i = 0; i < 3; ++i)
    {
        screen.labels[i].weight = weights[i];
        screen.labels[i].pos = {static_cast<float>(i) * 10.0f, 0, 500};
    }
    screen.labels[2].pos.z = -500; // behind the camera: not shown
    const auto projection = storm::math::projectionMatrix(1.2f, 960, 540, 1, 10000);
    const auto &shown = screen.screen.Place(screen.labels, projection, 960, 540, 1, 1, 1);
    CHECK(shown == std::vector<int32_t>{1, 0});
}
