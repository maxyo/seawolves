#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/logger.h>

namespace storm::logging
{

using logger_ptr = std::shared_ptr<spdlog::logger>;

// What the previous session left, taken once per process before the first log file opens (log_rotation.hpp): how it
// ended if it did not end cleanly ("" otherwise) and the files the rotation could not keep. The first logger made
// takes it, whoever makes it (on Android a call from the app can come before the engine's main).
struct PreviousSession
{
    std::string uncleanEnd;
    std::vector<std::string> failures;
};
const PreviousSession &previousSession();

// The logger writing Logs/<name>.log (opened truncated after previousSession() kept the last session's). On
// Android its lines also go to logcat unless `logcat` is false (a source that logs there itself).
logger_ptr getOrCreateLogger(const std::string &name, spdlog::level::level_enum level = spdlog::level::trace,
                             bool logcat = true);
// The sinks of logger `name`'s files (Logs/<name>.log and its errors in error.log, getOrCreateLogger), for a source
// that writes into them under a logger of its own (with its own logcat tag).
std::vector<spdlog::sink_ptr> fileSinks(const std::string &name);
// Lines below warn reach the files at most this long after they are written (warnings and errors at once on
// Android, logging.cpp).
inline constexpr std::chrono::seconds kFlushPeriod{2};

// Flushes every logger every kFlushPeriod from spdlog's own thread: a hung game thread keeps its last lines.
void startPeriodicFlush();
} // namespace storm::logging
