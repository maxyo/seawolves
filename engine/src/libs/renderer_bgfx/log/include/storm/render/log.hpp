#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/logger.h>

namespace storm::render
{
// The renderer's log (shim, effects, fx, textures): spdlog, the level is the call (Log().error(...)). In the log
// files and on stderr every line reads "[storm-bgfx] <text>", an error "[storm-bgfx] ERROR: <text>", a warning
// "[storm-bgfx] WARNING: <text>": the scene sweep and the diagnostics digest look for these marks. On Android every
// line also goes to logcat under the tag storm-bgfx as "<text>" / "ERROR: <text>" (the diagnostics report's renderer
// facts). Until SetLogSinks the lines go to stderr. Any thread may log.
spdlog::logger &Log();

// From now on the lines go to `sinks` (besides logcat) from `level` up, under the logger name `name`, flushed at once
// from `flush_on` up (the engine: its system log's files, logging.hpp fileSinks). Empty `sinks`: stderr again.
void SetLogSinks(std::string name, std::vector<spdlog::sink_ptr> sinks,
                 spdlog::level::level_enum level = spdlog::level::debug,
                 spdlog::level::level_enum flush_on = spdlog::level::off);

// A programming error the caller cannot continue from: the "ERROR: ..." line straight to stderr and logcat (a log
// behind a sink may not be written before the process ends), the process's abort message (the crash report carries
// it), then abort().
[[noreturn]] void FatalLine(const std::string &line);
template <typename... Args> [[noreturn]] void Fatal(fmt::format_string<Args...> format, Args &&...args)
{
    FatalLine(fmt::format(format, std::forward<Args>(args)...));
}

// The periodic summary lines (frame pacing, frame work, memory, GPU passes, the thermal status, the state of the
// background texture work): one each per period at most, so that a report holds a whole session of them.
inline constexpr std::chrono::seconds kSummaryPeriod{30};

// Detailed diagnostics (the About screen's «Подробная диагностика», off by default; README.md "Diagnostics"): what
// costs a frame something or repeats per texture runs only while it is on. The set logs "diagnostics: detailed on|off"
// when it changes. Any thread may ask.
void SetDetailedDiagnostics(bool on);
bool DetailedDiagnostics();

// True the first time it is asked for `flag`, and again after `flag` is set back to false: a line said once for
// as long as the flag lives (a member: once per object; a function-local static: once per process).
inline bool Once(bool &flag)
{
    return !std::exchange(flag, true);
}
inline bool Once(std::atomic<bool> &flag)
{
    return !flag.exchange(true, std::memory_order_relaxed);
}

// True when `now` differs from `last`, which then takes it: a line said on a change.
template <typename T, typename U> bool Changed(T &last, const U &now)
{
    if (last == now)
        return false;
    last = now;
    return true;
}
} // namespace storm::render
