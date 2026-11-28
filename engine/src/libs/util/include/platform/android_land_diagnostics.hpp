#pragma once
#ifdef STORM_TOUCH_UI
#include "debug_switch.hpp"
#include <array>
#include <string>
#include <string_view>

namespace storm::android
{
// Opt-in traces of the touch input bridge (game thread): switch StormInput (debug_switch.hpp), read once, before the
// game starts. One budget for the process keeps a broken adapter from flooding the log.
inline bool inputTraceEnabled()
{
    static const bool enabled = storm::platform::debugSwitch("StormInput");
    return enabled;
}
inline void inputTrace(const std::string &line)
{
    static unsigned budget = 320;
    if (!inputTraceEnabled() || !budget)
        return;
    --budget;
    storm::platform::debugTrace("StormInput", line);
}
inline void traceContextAction(const char *stage, const std::string &action, const std::string &detail)
{
    if (inputTraceEnabled())
        inputTrace(std::string("context ") + stage + " action=" + action + " " + detail);
}
// The land adapter's state per channel, logged when it changes.
inline void traceLandAdapter(unsigned channel, const std::string &state)
{
    static std::array<std::string, 6> previous;
    if (!inputTraceEnabled() || channel >= previous.size() || previous[channel] == state)
        return;
    previous[channel] = state;
    constexpr const char *names[] = {"bootstrap", "readiness", "source", "helper", "owner", "selection"};
    inputTrace(std::string("land-adapter ") + names[channel] + " " + state);
}
} // namespace storm::android
#endif
