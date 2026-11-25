#pragma once
// The spyglass (ISPYGLASS) publishes its target through the `spyglass` channel. Android never enables the legacy
// information icons or text.
#include "android_ship_telemetry.hpp"
#include "storm_ui/hud_state.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace storm::android
{

// The script's captain line is "<cannon type>  [<musket volley>]  <name>": parts apart by
// runs of two or more spaces. Returns {cannon type, captain name}.
inline std::pair<std::string, std::string> spyglassCaptainLine(std::string_view line)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && line[i] == ' ')
            ++i;
        size_t end = i;
        while (end < line.size() && !(line[end] == ' ' && end + 1 < line.size() && line[end + 1] == ' '))
            ++end;
        std::string part(line.substr(i, end - i));
        while (!part.empty() && part.back() == ' ')
            part.pop_back();
        if (!part.empty())
            parts.push_back(std::move(part));
        i = end;
    }
    if (parts.empty())
        return {};
    if (parts.size() == 1) // a line without its cannon type: only the name was sent
        return {std::string(), parts.front()};
    return {parts.front(), parts.back()};
}

// The telescope and its target (MSG_ISG_UPDATE): what the channel shows. The game's indices (class, charge,
// nation) are checked where the target is read (spyglass.cpp).
class SpyglassState
{
  public:
    void turn(bool on)
    {
        if (on != state_.on)
            state_ = {.known = true, .on = on};
    }
    // A target is shown now; its speed and distance to a tenth, as the game prints them.
    void target(storm::ui::hud::Spyglass next)
    {
        next.known = true;
        next.on = state_.on;
        next.target = state_.on;
        for (double *value : {&next.speed, &next.distance})
            *value = *value >= 0.0 && *value <= 1e6 ? shipDisplayedSpeed(*value).value_or(-1.0) : -1.0;
        state_ = std::move(next);
    }
    void clear()
    {
        state_ = {.known = true, .on = state_.on};
    }
    bool on() const
    {
        return state_.on;
    }
    const storm::ui::hud::Spyglass &hud() const
    {
        return state_;
    }

  private:
    storm::ui::hud::Spyglass state_{.known = true};
};

inline SpyglassState spyglass;

} // namespace storm::android
