#include "syncable_sink.hpp"

#include <cstdio>

#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>

storm::logging::sinks::syncable_sink::syncable_sink(const spdlog::filename_t &filename, bool truncate)
{
    file_helper_.open(filename, truncate);
}

void storm::logging::sinks::syncable_sink::log(const spdlog::details::log_msg &msg)
{
    std::lock_guard lock(mutex_);
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);
    file_helper_.write(formatted);
}

void storm::logging::sinks::syncable_sink::flush()
{
    std::lock_guard lock(mutex_);
    file_helper_.flush();
}

void storm::logging::sinks::syncable_sink::set_pattern(const std::string &pattern)
{
    std::lock_guard lock(mutex_);
    formatter_ = spdlog::details::make_unique<spdlog::pattern_formatter>(pattern);
}

void storm::logging::sinks::syncable_sink::set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter)
{
    std::lock_guard lock(mutex_);
    formatter_ = std::move(sink_formatter);
}

void storm::logging::sinks::syncable_sink::sync()
{
    std::lock_guard lock(mutex_);
    file_helper_.sync();
}

// The exit's last write (LifecycleDiagnosticsService::terminate, on its bounded flushing thread): to the disk, then
// closed. A failed fsync is reported where the log cannot be: stderr.
void storm::logging::sinks::syncable_sink::terminate_immediately()
{
    std::lock_guard lock(mutex_);
    try
    {
        file_helper_.sync();
    }
    catch (const spdlog::spdlog_ex &error)
    {
        std::fprintf(stderr, "log %s: %s\n", file_helper_.filename().c_str(), error.what());
    }
    file_helper_.close();
}
