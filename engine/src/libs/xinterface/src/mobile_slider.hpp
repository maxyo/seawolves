#pragma once
#include "mobile_widget.hpp"
#include "string_compare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// A slider's position from a saved normalized value. A saved 0.59 is just below 0.59 as a float, so truncation would
// turn 59 into 58 on reopening: the nearest discrete slider unit is taken.
inline int32_t SliderPositionFromNormalized(float value, int32_t quantity)
{
    const double position = std::round(static_cast<double>(value) * quantity);
    if (!std::isfinite(position))
        return 0;
    return static_cast<int32_t>(std::clamp(position, static_cast<double>(std::numeric_limits<int32_t>::min()),
                                           static_cast<double>(std::numeric_limits<int32_t>::max())));
}

// What the touch UI shows and takes of a slider node (CXI_SLIDELINE).
struct MobileSliderState
{
    const char *node = "";
    bool used = true, selected = true, clickable = true, locked = false, lockStatus = false;
    int32_t current = 0, quantity = 0, minimum = -1, maximum = -1; // -1: no limit of its own
    int32_t top = 0, left = 0;
};

// The slider's widget; nullopt for a node not in use.
inline std::optional<MobileWidget> MobileSliderWidget(const MobileSliderState &slider)
{
    if (!slider.used)
        return std::nullopt;
    MobileWidget widget{slider.node, "slider", "", std::to_string(slider.current), -1, slider.top * 8192 + slider.left};
    widget.min = std::max(0, slider.minimum);
    widget.max = slider.maximum < 0 ? slider.quantity : std::min(slider.quantity, slider.maximum);
    widget.step = 1;
    widget.enabled = slider.selected && slider.clickable && !slider.locked && !slider.lockStatus &&
                     slider.quantity > 0 && widget.max >= widget.min;
    // SDL's Android window driver has no gamma ramp: color correction is not offered until the renderer has it.
    if (storm::iEquals(slider.node, "GAMMA_SLIDE") || storm::iEquals(slider.node, "BRIGHT_SLIDE") ||
        storm::iEquals(slider.node, "CONTRAST_SLIDE"))
    {
        widget.enabled = false;
        widget.unavailableReason = "android_color_correction";
    }
    if (widget.enabled)
        widget.actions.push_back("set");
    return widget;
}

// The position the UI's value sets; nullopt when the slider does not take it. The node's own setter applies it (its
// attributes, limits and the scripts' eSlideChange event).
inline std::optional<int> MobileSliderTarget(const MobileSliderState &slider, int index, const std::string &value)
{
    const auto widget = MobileSliderWidget(slider);
    int position;
    if (index != -1 || !widget || !widget->enabled || !MobileSliderPosition(value, widget->min, widget->max, position))
        return std::nullopt;
    return position;
}
