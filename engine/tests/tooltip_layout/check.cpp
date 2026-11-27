#include "tooltip_layout.hpp"

#include <catch2/catch.hpp>

using namespace storm::xi;

namespace
{
// a text 400 px wide on one line; narrower, it wraps into lines of 25 px
TooltipInput Input()
{
    TooltipInput in;
    in.margin = 30;
    in.header_top = 66;
    in.header = true;
    in.node = {100, 200, 150, 240};
    in.min_width = 250;
    in.max_width = 600;
    in.screen_width = 1920;
    in.screen_height = 1080;
    in.measure = [](float width) {
        const float lines = std::ceil(400.0f / width);
        return std::pair{std::min(400.0f, width), 25.0f * lines};
    };
    return in;
}
} // namespace

TEST_CASE("tooltip_layout.size_and_place")
{
    const auto layout = LayoutTooltip(Input());
    CHECK(layout.frame.left == 180); // right of the node, a margin away
    CHECK(layout.frame.top == 200);
    CHECK(layout.frame.right - layout.frame.left == 460); // the text and its margins
    CHECK(layout.text.top == 66);                         // below the title
    CHECK(layout.text.bottom == 66 + 25);
    CHECK(layout.frame.bottom - layout.frame.top == 66 + 25 + 30);
}

TEST_CASE("tooltip_layout.limits_picture_and_screen_edges")
{
    auto in = Input();
    in.max_width = 300; // the text wraps into two lines
    auto layout = LayoutTooltip(in);
    CHECK(layout.frame.right - layout.frame.left == 300);
    CHECK(layout.text.bottom - layout.text.top == 50);

    in = Input();
    in.measure = [](float) { return std::pair{10.0f, 25.0f}; };
    CHECK(LayoutTooltip(in).frame.right - LayoutTooltip(in).frame.left == 250); // at least the minimum

    in = Input();
    in.header = false;
    in.picture_width = 100;
    in.picture_height = 80;
    layout = LayoutTooltip(in);
    CHECK(layout.picture.top == 30); // no title: content from the margin
    CHECK(layout.text.top == 30 + 80 + 15);

    in = Input();
    in.node = {1800, 1050, 1900, 1070}; // no room on the right or below
    layout = LayoutTooltip(in);
    CHECK(layout.frame.right == 1800 - 30);
    CHECK(layout.frame.bottom <= 1080);
}
