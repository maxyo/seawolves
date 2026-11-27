#pragma once

// Where a tooltip's parts go (MSG_INTERFACE_SET_TOOLTIP with rich text): the templates give the parts' places in a
// frame at (0, 0) and the text's margin; the frame is as wide as the text needs within the scripts' limits, as high as
// its content, and beside the hovered node on the screen.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace storm::xi
{
struct TooltipRect
{
    int32_t left = 0, top = 0, right = 0, bottom = 0;
};

struct TooltipInput
{
    int32_t margin = 0;     // the text template's distance from the frame's left side
    int32_t header_top = 0; // the text template's top: below the title
    bool header = false;
    int32_t picture_width = 0, picture_height = 0; // 0: none
    TooltipRect node;                              // the hovered node
    int32_t min_width = 0, max_width = 0;
    int32_t screen_width = 0, screen_height = 0;
    // the widest line and the height of the text laid out that wide
    std::function<std::pair<float, float>(float width)> measure;
};

struct TooltipLayout
{
    TooltipRect frame;         // on the screen
    TooltipRect text, picture; // in the frame
};

inline TooltipLayout LayoutTooltip(const TooltipInput &in)
{
    const int32_t margin = in.margin;
    const bool picture = in.picture_width > 0 && in.picture_height > 0;
    int32_t width = static_cast<int32_t>(in.measure(static_cast<float>(in.max_width - 2 * margin)).first) + 2 * margin;
    if (picture)
        width = std::max(width, in.picture_width + 2 * margin);
    width = std::clamp(width, in.min_width, std::max(in.min_width, in.max_width));
    const float text_height = in.measure(static_cast<float>(width - 2 * margin)).second;

    TooltipLayout out;
    int32_t y = in.header ? in.header_top : margin;
    if (picture)
    {
        out.picture = {(width - in.picture_width) / 2, y, (width + in.picture_width) / 2, y + in.picture_height};
        y = out.picture.bottom + margin / 2;
    }
    out.text = {margin, y, width - margin, y + static_cast<int32_t>(std::ceil(text_height))};
    const int32_t height = out.text.bottom + margin;

    // right of the node where the screen has room, else left of it; level with its top, on the screen
    int32_t left = in.node.right + margin;
    if (left + width > in.screen_width)
        left = in.node.left - margin - width;
    left = std::clamp(left, 0, std::max(0, in.screen_width - width));
    const int32_t top = std::clamp(in.node.top, 0, std::max(0, in.screen_height - height));
    out.frame = {left, top, left + width, top + height};
    return out;
}
} // namespace storm::xi
