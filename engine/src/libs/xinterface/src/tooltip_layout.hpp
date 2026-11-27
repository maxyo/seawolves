#pragma once

// Where a tooltip's parts go (MSG_INTERFACE_SET_TOOLTIP with rich text). The templates place the parts around the
// back picture; the text is centred on the back's centre, as wide as its widest line within the scripts' limits, and
// the back grows downwards to hold the picture and the text. The whole tooltip then goes beside the hovered node
// (right or left, where the screen has more room), else above or below it.

#include <algorithm>
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
    TooltipRect back, text;   // the templates' places
    int32_t picture_top = 0;  // the picture template's top
    int32_t title_height = 0; // of the title's back
    int32_t ornament_height = 0;
    bool header = false;
    int32_t title_width = 0;                       // the title's widest line
    int32_t picture_width = 0, picture_height = 0; // 0: none
    TooltipRect node;                              // the hovered node
    int32_t min_width = 0, max_width = 0;          // of the text
    int32_t screen_width = 0, screen_height = 0;
    // the widest line and the height of the text laid out that wide
    std::function<std::pair<float, float>(float width)> measure;
};

struct TooltipLayout
{
    TooltipRect back, text, picture; // on the screen
    int32_t shift = 0;               // how far the parts centred on the template's back moved sideways
};

// The original engine's spacing: below the picture, and from the text to the back's bottom.
inline constexpr int32_t kTooltipPictureGap = 16;
inline constexpr int32_t kTooltipBottomMargin = 28;

namespace detail
{
// left and top of a `width` x `height` box beside, else above or below, `node`
inline std::pair<int32_t, int32_t> PlaceTooltip(const TooltipRect &node, int32_t width, int32_t height,
                                                int32_t screen_width, int32_t screen_height)
{
    const int32_t room_right = screen_width - node.right;
    const bool left_side = room_right < node.left;
    if (left_side ? width < node.left : width < room_right)
    {
        const int32_t left = left_side ? node.left - width : node.right;
        // level with the node's top; with its bottom when the box is taller and the node is low on the screen
        const bool up = node.bottom - node.top < height && (node.top + node.bottom) / 2 >= screen_height / 2;
        int32_t top = up ? node.bottom - height : node.top;
        top = std::min(std::max(top, 0), screen_height - height);
        return {left, top};
    }
    const int32_t room_below = screen_height - node.bottom;
    int32_t top = 0;
    bool fits = true;
    if (node.top < room_below)
    {
        fits = height < room_below;
        top = fits ? node.bottom : screen_height - height;
    }
    else
    {
        fits = height < node.top;
        top = fits ? node.top - height : 0;
    }
    if (!fits) // in a corner of the screen, on the side with more room
        return {left_side ? 0 : screen_width - width, top};
    const bool leftwards = node.right - node.left < width && (node.left + node.right) / 2 >= screen_width / 2;
    int32_t left = leftwards ? node.right - width : node.left;
    left = std::min(std::max(left, 0), screen_width - width);
    return {left, top};
}
} // namespace detail

inline TooltipLayout LayoutTooltip(const TooltipInput &in)
{
    const bool picture = in.picture_width > 0 && in.picture_height > 0;
    const int32_t picture_width = picture ? in.picture_width : 0;
    const int32_t centre = (in.back.left + in.back.right) / 2;
    int32_t widest = std::max(in.max_width, picture_width);
    const auto text_width = static_cast<int32_t>(in.measure(static_cast<float>(widest)).first);
    const int32_t title_width = in.header ? in.title_width : 0;
    widest = std::max(widest, title_width);
    int32_t width = std::max({text_width, title_width, picture_width, 0});
    width = width < in.min_width ? in.min_width : std::min(width, widest);

    // the back as it would sit centred on the template's back, its top at 0
    const int32_t text_left = centre - width / 2;
    const int32_t back_left = text_left - (in.text.left - in.back.left);
    const int32_t back_right = text_left + width - (in.text.right - in.back.right);
    int32_t y = in.header ? 0 : -(in.title_height + in.ornament_height);
    TooltipLayout out;
    if (picture)
    {
        const int32_t top = in.picture_top - in.back.top + y;
        out.picture = {centre - picture_width / 2, top, centre - picture_width / 2 + picture_width,
                       top + in.picture_height};
        y = out.picture.bottom + kTooltipPictureGap;
    }
    else
        y += in.text.top - in.back.top;
    const auto text_height = static_cast<int32_t>(in.measure(static_cast<float>(width)).second);
    out.text = {text_left, y, text_left + width, y + std::max(text_height, 0)};
    const int32_t height = out.text.bottom + kTooltipBottomMargin;

    const auto [left, top] =
        detail::PlaceTooltip(in.node, back_right - back_left, height, in.screen_width, in.screen_height);
    out.shift = left - back_left;
    out.back = {left, top, left + back_right - back_left, top + height};
    const auto move = [&](TooltipRect &r) {
        r = {r.left + out.shift, r.top + top, r.right + out.shift, r.bottom + top};
    };
    move(out.text);
    if (picture)
        move(out.picture);
    return out;
}
} // namespace storm::xi
