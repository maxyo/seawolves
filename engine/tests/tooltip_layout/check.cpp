#include "tooltip_layout.hpp"

#include <catch2/catch.hpp>

#include <cmath>

using namespace storm::xi;

namespace
{
// templates as a tooltip's ini places them; a text 1000 px wide on one line wraps into lines of 25 px
TooltipInput Input()
{
    TooltipInput in;
    in.back = {0, 0, 600, 400};
    in.text = {30, 66, 570, 506};
    in.picture_top = 71;
    in.title_height = 39;
    in.ornament_height = 20;
    in.header = true;
    in.title_width = 100;
    in.node = {100, 200, 150, 240};
    in.min_width = 250;
    in.max_width = 500;
    in.screen_width = 1920;
    in.screen_height = 1080;
    in.measure = [](float width) { return std::pair{std::min(1000.0f, width), 25.0f * std::ceil(1000.0f / width)}; };
    return in;
}

void Check(const TooltipRect &r, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    CHECK(r.left == left);
    CHECK(r.top == top);
    CHECK(r.right == right);
    CHECK(r.bottom == bottom);
}
} // namespace

TEST_CASE("tooltip_layout.width_and_height")
{
    // the text as wide as the maximum, its margins kept; below it the bottom margin
    auto layout = LayoutTooltip(Input());
    Check(layout.back, 150, 200, 710, 344); // right of the node, level with its top
    Check(layout.text, 180, 266, 680, 316);
    CHECK(layout.shift == 130);

    auto in = Input();
    in.measure = [](float) { return std::pair{120.0f, 25.0f}; };
    layout = LayoutTooltip(in);
    CHECK(layout.text.right - layout.text.left == 250); // at least the minimum

    in.title_width = 300; // a wider title widens the text
    layout = LayoutTooltip(in);
    CHECK(layout.text.right - layout.text.left == 300);

    in.title_width = 700; // up to the title's width
    in.measure = [](float width) { return std::pair{width, 25.0f}; };
    layout = LayoutTooltip(in);
    CHECK(layout.text.right - layout.text.left == 700);
}

TEST_CASE("tooltip_layout.picture_and_no_header")
{
    auto in = Input();
    in.header = false;
    in.picture_width = 64;
    in.picture_height = 64;
    const auto layout = LayoutTooltip(in);
    // no title: everything moves up by the title's and the ornament's height; the text 16 below the picture
    CHECK(layout.picture.top - layout.back.top == 71 - 59);
    CHECK(layout.picture.bottom - layout.picture.top == 64);
    CHECK(layout.picture.left + layout.picture.right == layout.text.left + layout.text.right); // both centred
    CHECK(layout.text.top - layout.back.top == 12 + 64 + 16);
    CHECK(layout.back.bottom - layout.text.bottom == 28);
}

TEST_CASE("tooltip_layout.placement")
{
    auto in = Input();
    in.node = {1800, 1000, 1900, 1060}; // more room on the left; low on the screen: level with the node's bottom
    Check(LayoutTooltip(in).back, 1240, 916, 1800, 1060);

    in.node = {100, 950, 150, 1100}; // a node taller than the box: level with its top, kept on the screen
    Check(LayoutTooltip(in).back, 150, 936, 710, 1080);

    in.node = {0, 100, 1920, 140}; // no room beside: below, from the node's left
    Check(LayoutTooltip(in).back, 0, 140, 560, 284);

    in.node = {500, 900, 1500, 940}; // no room beside, more above: above, from the node's left
    Check(LayoutTooltip(in).back, 500, 756, 1060, 900);

    in.measure = [](float width) { return std::pair{width, 600.0f}; };
    in.node = {0, 400, 1920, 480}; // fits neither below nor above: in the screen's corner
    Check(LayoutTooltip(in).back, 1360, 386, 1920, 1080);
}
