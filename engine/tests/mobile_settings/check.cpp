// A settings slider in the touch UI (mobile_slider.hpp): its widget from the node's state, the positions a "set"
// action may take, and a saved value read back as the position it was saved from.
#include "mobile_slider.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

int main()
{
    assert(static_cast<int>(0.59f * 100) == 58); // what truncation did
    for (int initial = 0; initial <= 100; ++initial)
    {
        int position = initial;
        for (int reopen = 0; reopen < 20; ++reopen)
        {
            char saved[64];
            std::snprintf(saved, sizeof(saved), "%.7f", static_cast<float>(position) / 100);
            position = SliderPositionFromNormalized(std::strtof(saved, nullptr), 100);
            assert(position == initial);
        }
    }
    assert(SliderPositionFromNormalized(0.595f, 100) == 60);
    assert(SliderPositionFromNormalized(0.594f, 100) == 59);
    assert(SliderPositionFromNormalized(std::numeric_limits<float>::quiet_NaN(), 100) == 0);
    assert(SliderPositionFromNormalized(std::numeric_limits<float>::max(), 100) == INT32_MAX);
    int value = -1;
    assert(MobileSliderSetPosition("set@7:100", 7, value) && value == 100);
    for (const std::string &bad :
         {"set@6:50", "set@8:50", "set@7:-1", "set@7:+1", "set@7:1.5", "set@7: 1", "set@7:1 ", "set@7:", "set@7:1:2",
          "set@7:2147483648", "set@18446744073709551616:1", "50", "press"})
        assert(!MobileSliderSetPosition(bad, 7, value));
    assert(!MobileSliderPosition(std::string("12\0tail", 7), 0, 100, value));
    assert(MobileSliderSetPosition("set@18446744073709551615:0", UINT64_MAX, value));

    MobileSliderState slider;
    slider.node = "SOUND_SLIDE";
    slider.current = 50;
    slider.quantity = 100;
    const auto widget = MobileSliderWidget(slider);
    assert(widget && widget->type == "slider" && widget->value == "50" && widget->min == 0 && widget->max == 100);
    assert(widget->enabled && widget->step == 1 && widget->actions == std::vector<std::string>{"set"});
    assert(MobileSliderTarget(slider, -1, "0") == 0 && MobileSliderTarget(slider, -1, "100") == 100);
    assert(!MobileSliderTarget(slider, 0, "50")); // a slider has no rows
    for (auto *flag : {&slider.selected, &slider.clickable})
    {
        *flag = false;
        assert(!MobileSliderTarget(slider, -1, "20"));
        *flag = true;
    }
    slider.used = false;
    assert(!MobileSliderWidget(slider) && !MobileSliderTarget(slider, -1, "20"));
    slider.used = true;
    for (auto *flag : {&slider.locked, &slider.lockStatus})
    {
        *flag = true;
        assert(!MobileSliderTarget(slider, -1, "20"));
        *flag = false;
    }
    slider.minimum = 20;
    slider.maximum = 80;
    assert(!MobileSliderTarget(slider, -1, "19") && !MobileSliderTarget(slider, -1, "81"));
    assert(MobileSliderTarget(slider, -1, "20") == 20 && MobileSliderTarget(slider, -1, "80") == 80);
    slider.maximum = 200; // the grate is the limit
    assert(!MobileSliderTarget(slider, -1, "101"));
    slider.minimum = 101;
    assert(!MobileSliderTarget(slider, -1, "100") && !MobileSliderWidget(slider)->enabled);
    slider.minimum = -1;
    slider.quantity = 0;
    assert(!MobileSliderTarget(slider, -1, "0"));
    slider.quantity = 100;
    for (const char *name : {"GAMMA_SLIDE", "bright_slide", "CONTRAST_SLIDE"})
    {
        slider.node = name;
        const auto color = MobileSliderWidget(slider);
        assert(!color->enabled && color->actions.empty() && color->unavailableReason == "android_color_correction");
        assert(!MobileSliderTarget(slider, -1, "50"));
    }
    std::cout << "PASS slider range, parsing, stale revisions, disabled guards, normalized save/reopen stability\n";
}
