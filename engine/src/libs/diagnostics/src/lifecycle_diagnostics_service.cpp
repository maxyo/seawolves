#include "lifecycle_diagnostics_service.hpp"

#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "fs.h"
#include "spdlog_sinks/syncable_sink.hpp"

namespace storm::diag
{

namespace
{
// The last flush writes every log out and fsyncs the syncable ones; on slow storage an fsync can block for
// seconds. The process ends right after this flush, inside the app's exit budget of 3 s (app/android/game/TESTING.md,
// «Exit»): the exit waits for the flush at most this long.
constexpr auto kFlushLimit = std::chrono::milliseconds(1500);
} // namespace

LifecycleDiagnosticsService::~LifecycleDiagnosticsService()
{
    terminate();
}

LifecycleDiagnosticsService::Guard LifecycleDiagnosticsService::initialize()
{
    if (!active_)
    {
        active_ = true;
        create_directories(fs::GetLogsPath());
    }
    return Guard(*this);
}

void LifecycleDiagnosticsService::terminate()
{
    if (!active_)
        return;
    active_ = false;
    // The flushing thread holds its own references: the registry may go while it still writes.
    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    spdlog::apply_all([&loggers](const std::shared_ptr<spdlog::logger> &logger) { loggers.push_back(logger); });
    std::promise<void> done;
    auto flushed = done.get_future();
    std::thread([loggers = std::move(loggers), done = std::move(done)]() mutable {
        for (const auto &logger : loggers)
        {
            logger->flush();
            for (auto &sink : logger->sinks())
                if (const auto syncable = std::dynamic_pointer_cast<logging::sinks::syncable_sink>(sink))
                    syncable->terminate_immediately();
        }
        done.set_value();
    }).detach();
    if (flushed.wait_for(kFlushLimit) == std::future_status::timeout)
    {
        // The logs are what hangs: this goes where they do not write. spdlog::shutdown would wait on the same sinks.
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "StormEngine",
                            "log flush did not finish in %lld ms: ending without it (the logs may be cut)",
                            static_cast<long long>(kFlushLimit.count()));
#else
        std::fprintf(stderr, "log flush did not finish in %lld ms: ending without it (the logs may be cut)\n",
                     static_cast<long long>(kFlushLimit.count()));
#endif
        return;
    }
    spdlog::shutdown();
}

} // namespace storm::diag
