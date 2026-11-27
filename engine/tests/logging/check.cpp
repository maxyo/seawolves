// Host check of the engine's loggers (src/libs/diagnostics/src/logging.cpp): every logger's errors also land in
// error.log, with the logger that wrote them; lines below error stay in their own log; a warning is on disk at once;
// and the periodic flush puts the lines of a producer that stops (a hung game thread) on disk by spdlog's flush thread
// alone. That wait is on the file, not on a clock (kFlushPeriod is the production constant handed to
// spdlog::flush_every): a flusher that never runs hangs here and run.py's deadlock guard reports it.
#include "logging.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch.hpp>

namespace
{
std::string read(const std::string &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

size_t count(const std::string &text, const std::string &part)
{
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        ++n;
    return n;
}
} // namespace

TEST_CASE("logging")
{
    // fs::GetLogsPath: $XDG_STATE_HOME/seawolves/logs on Linux.
    const auto state = std::filesystem::temp_directory_path();
    REQUIRE(setenv("XDG_STATE_HOME", state.c_str(), 1) == 0);
    using storm::logging::getOrCreateLogger;
    auto system = getOrCreateLogger("system");
    auto compile = getOrCreateLogger("compile");
    auto errors = getOrCreateLogger("error");
    system->info("system info line");
    system->error("system error line");
    compile->warn("compile warning line");
    compile->critical("compile critical line");
    errors->info("script error line");
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

    const std::string logs = (state / "seawolves/logs/").string();
    const auto systemLog = read(logs + "system.log"), compileLog = read(logs + "compile.log"),
               errorLog = read(logs + "error.log");
    REQUIRE((count(systemLog, "system info line") == 1 &&
             count(systemLog, "system error line") == 1));    // system.log has its lines
    REQUIRE(count(compileLog, "compile critical line") == 1); // compile.log has its lines
    REQUIRE(count(errorLog, "[system] [error] system error line") ==
            1); // an error of another logger is in error.log, with its logger
    REQUIRE(count(errorLog, "[compile] [critical] compile critical line") == 1); // critical lines too
    REQUIRE(count(errorLog, "script error line") == 1);                          // error.log keeps its own lines once
    REQUIRE((errorLog.find("info line") == std::string::npos &&
             errorLog.find("warning line") == std::string::npos)); // lines below error stay in their own log

    // A warning reaches the file at once, with the lines logged before it (no flush call, no periodic flush yet).
    auto warned = getOrCreateLogger("warned");
    warned->info("the line before the warning");
    warned->warn("the warning line");
    const auto warnedLog = read(logs + "warned.log");
    REQUIRE((count(warnedLog, "the line before the warning") == 1 && count(warnedLog, "the warning line") == 1));

    // A producer that writes a line below warn and then hangs: nobody flushes but spdlog's own thread.
    auto hung = getOrCreateLogger("hung");
    hung->info("the last line before the hang");
    REQUIRE(count(read(logs + "hung.log"), "the last line before the hang") ==
            0); // unflushed, the line is only in the buffer
    storm::logging::startPeriodicFlush();
    std::puts("step: the periodic flush puts the hung producer's last line on disk");
    std::fflush(stdout);
    while (count(read(logs + "hung.log"), "the last line before the hang") != 1)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    spdlog::shutdown();
    std::puts(
        "PASS: every logger's errors are in error.log, once, with their logger; lines flushed by the periodic flush");
}
