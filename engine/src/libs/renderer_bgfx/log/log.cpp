#include "storm/render/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <android/set_abort_message.h>
#include <spdlog/sinks/android_sink.h>
#endif

namespace storm::render
{
namespace
{
constexpr std::string_view kTag = "[storm-bgfx] ";

// Writes `prefix` and the level's mark before each line, then hands it to its sinks. The sinks are fixed at
// construction, so the lines of several threads need no lock here (each sink takes its own).
class MarkSink final : public spdlog::sinks::dist_sink_st
{
  public:
    MarkSink(std::string_view prefix, std::vector<spdlog::sink_ptr> sinks)
        : spdlog::sinks::dist_sink_st(std::move(sinks)), prefix_(prefix)
    {
    }

  protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        const std::string_view mark = msg.level >= spdlog::level::err    ? "ERROR: "
                                      : msg.level == spdlog::level::warn ? "WARNING: "
                                                                         : "";
        spdlog::memory_buf_t line;
        line.append(prefix_);
        line.append(mark);
        line.append(msg.payload);
        spdlog::details::log_msg marked(msg);
        marked.payload = spdlog::string_view_t(line.data(), line.size());
        spdlog::sinks::dist_sink_st::sink_it_(marked);
    }

  private:
    std::string_view prefix_;
};

std::shared_ptr<spdlog::logger> Make(std::string name, std::vector<spdlog::sink_ptr> sinks,
                                     spdlog::level::level_enum level, spdlog::level::level_enum flush_on)
{
    auto tagged = std::make_shared<MarkSink>(kTag, std::move(sinks));
    tagged->set_level(level);
    std::vector<spdlog::sink_ptr> all{tagged};
#ifdef __ANDROID__
    static const auto logcat = std::make_shared<MarkSink>(
        "", std::vector<spdlog::sink_ptr>{std::make_shared<spdlog::sinks::android_sink_mt>("storm-bgfx", true)});
    all.push_back(logcat);
#endif
    auto logger = std::make_shared<spdlog::logger>(std::move(name), all.begin(), all.end());
    // A line below every sink's level is not even formatted (logcat takes debug lines on Android).
#ifdef __ANDROID__
    logger->set_level(spdlog::level::debug);
#else
    logger->set_level(level);
#endif
    logger->flush_on(flush_on);
    return logger;
}

// The loggers are never destroyed: a worker thread (the bakes', the relief generator's) may still write a line while
// the process exits and the statics go.

// Before the engine's sinks (tools, checks): the line on stderr.
spdlog::logger &Default()
{
    static spdlog::logger *const logger = [] {
        auto err = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        err->set_pattern("%v");
        return new spdlog::logger(*Make("storm-bgfx", {err}, spdlog::level::debug, spdlog::level::off));
    }();
    return *logger;
}

std::atomic<spdlog::logger *> current{nullptr};
std::atomic<bool> detailed{false};
bool detailed_set = false; // the first set logs its state too
// Every logger ever set stays alive: another thread may still be writing through the one replaced.
std::mutex kept_mutex;
auto &kept = *new std::vector<std::shared_ptr<spdlog::logger>>;
} // namespace

spdlog::logger &Log()
{
    if (spdlog::logger *logger = current.load(std::memory_order_acquire))
        return *logger;
    return Default();
}

void SetLogSinks(std::string name, std::vector<spdlog::sink_ptr> sinks, spdlog::level::level_enum level,
                 spdlog::level::level_enum flush_on)
{
    if (sinks.empty())
    {
        current.store(nullptr, std::memory_order_release);
        return;
    }
    auto logger = Make(std::move(name), std::move(sinks), level, flush_on);
    std::lock_guard lock(kept_mutex);
    kept.push_back(logger);
    current.store(logger.get(), std::memory_order_release);
}

void SetDetailedDiagnostics(bool on)
{
    const bool first = !std::exchange(detailed_set, true);
    if (detailed.exchange(on, std::memory_order_relaxed) != on || first)
        Log().info("diagnostics: detailed {}", on ? "on" : "off");
}

bool DetailedDiagnostics()
{
    return detailed.load(std::memory_order_relaxed);
}

void FatalLine(const std::string &line)
{
    const std::string text = "ERROR: " + line;
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, "storm-bgfx", "%s", text.c_str());
    android_set_abort_message(text.c_str());
#endif
    std::fprintf(stderr, "%.*s%s\n", int(kTag.size()), kTag.data(), text.c_str());
    std::abort();
}
} // namespace storm::render
