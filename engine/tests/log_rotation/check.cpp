// Host check of the engine's session logs (src/libs/diagnostics/include/log_rotation.hpp): the last kKeptSessions
// sessions' logs are kept, each file with its session, and a session that ended without a clean exit is told.
#include "log_rotation.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;

namespace
{
void write(const fs::path &file, const std::string &text)
{
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

std::string read(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace

TEST_CASE("log_rotation")
{
    const fs::path logs = fs::temp_directory_path() / "Logs";
    using namespace storm::logging;
    static_assert(kKeptSessions == 3);

    REQUIRE(rotatePreviousSessionLogs(logs).empty()); // a missing directory is a first start
    REQUIRE(!fs::exists(logs));                       // nothing is created
    fs::create_directories(logs);

    // Session 1 wrote system and error, session 2 only system, session 3 system and mimalloc, session 4 system.
    const auto session = [&logs](int n, std::initializer_list<const char *> bases) {
        for (const char *base : bases)
            write(logs / (std::string(base) + ".log"), "session " + std::to_string(n) + " " + base + "\n");
        REQUIRE(rotatePreviousSessionLogs(logs).empty()); // a rotation
    };
    write(logs / "ff_keys.json", "{}");
    session(1, {"system", "error"});
    REQUIRE((read(logs / "system.prev1.log") == "session 1 system\n" &&
             read(logs / "error.prev1.log") == "session 1 error\n"));               // the session before is .prev1
    REQUIRE((!fs::exists(logs / "system.log") && !fs::exists(logs / "error.log"))); // the current names are free
    session(2, {"system"});
    REQUIRE((read(logs / "system.prev1.log") == "session 2 system\n" &&
             read(logs / "system.prev2.log") == "session 1 system\n")); // two sessions back is .prev2
    REQUIRE((
        !fs::exists(logs / "error.prev1.log") &&
        read(logs / "error.prev2.log") ==
            "session 1 error\n")); // a log a session did not write leaves a gap: it is not passed off as that session's
    session(3, {"system", "mimalloc"});
    REQUIRE((read(logs / "system.prev3.log") == "session 1 system\n" &&
             read(logs / "error.prev3.log") == "session 1 error\n" &&
             read(logs / "mimalloc.prev1.log") == "session 3 mimalloc\n")); // three sessions are kept
    session(4, {"system"});
    REQUIRE((!fs::exists(logs / "system.prev4.log") && !fs::exists(logs / "error.prev3.log") &&
             !fs::exists(logs / "error.prev4.log"))); // the session four back is removed, every file of it
    REQUIRE((read(logs / "system.prev1.log") == "session 4 system\n" &&
             read(logs / "system.prev2.log") == "session 3 system\n" &&
             read(logs / "system.prev3.log") == "session 2 system\n" &&
             read(logs / "mimalloc.prev2.log") == "session 3 mimalloc\n")); // each file moved with its session
    REQUIRE(read(logs / "ff_keys.json") == "{}");                           // other files stay

    // Names that only look like a generation are logs of their own; directories are skipped.
    write(logs / "odd.prev0.log", "zero\n");
    write(logs / "bare.prev.log", "bare\n");
    fs::create_directories(logs / "dir.log");
    REQUIRE(rotatePreviousSessionLogs(logs).empty()); // a rotation with odd names
    REQUIRE((read(logs / "odd.prev0.prev1.log") == "zero\n" && read(logs / "bare.prev.prev1.log") == "bare\n" &&
             fs::is_directory(logs / "dir.log"))); // odd names, directories

    // A failure is reported, not dropped: the target name is taken by a non-empty directory.
    write(logs / "script_stack.log", "stack\n");
    fs::create_directories(logs / "script_stack.prev1.log" / "blocker");
    const auto failures = rotatePreviousSessionLogs(logs);
    REQUIRE((failures.size() == 1 &&
             failures[0].find("script_stack.log") != std::string::npos)); // a failed rotation is reported
    REQUIRE(read(logs / "script_stack.log") == "stack\n"); // a log that could not be rotated is untouched

    // The running marker's decision table (uncleanEnd): which ends are unclean.
    const std::string started = "started 2026-11-13 18:30:01, pid 4242\n";
    const auto beat = [](long long at) { return "heartbeat " + std::to_string(at) + "\n"; };
    const auto background = [](long long at) { return "background " + std::to_string(at) + "\n"; };
    REQUIRE(uncleanEnd("").empty()); // no marker: a clean exit
    REQUIRE(uncleanEnd(started + beat(1000)).find("ended in the foreground") !=
            std::string::npos); // killed in the foreground (a hang swiped away, a crash): unclean
    REQUIRE(uncleanEnd(started + beat(1000) + background(1000 + kStuckSeconds))
                .empty()); // in the background with a live game thread (the system or the player ended it there): clean
    REQUIRE(uncleanEnd(started + beat(1000) + background(1001 + kStuckSeconds)).find("stuck") !=
            std::string::npos); // in the background, but the game thread had stopped beating before: unclean
    REQUIRE(uncleanEnd(started + background(1000)).find("never alive") != std::string::npos); // never beat: unclean
    REQUIRE(uncleanEnd(started + beat(1000)).rfind("started 2026-11-13 18:30:01, pid 4242;", 0) ==
            0); // the record names the session
    REQUIRE(uncleanEnd("garbage\n").find("unreadable") !=
            std::string::npos); // an unreadable marker is unclean, and said

    // The marker as the engine writes it: start, beats, background from another thread, foreground, clean exit.
    SessionMarker marker(logs, "2026-11-13 18:30:01", 4242);
    std::vector<std::string> problems;
    REQUIRE((marker.Beat(1000).empty() && !fs::exists(logs / kRunningMarker))); // no marker before the start
    REQUIRE(marker.Start(1000).empty());                                        // start
    REQUIRE(read(logs / kRunningMarker) == started + beat(1000)); // the marker names the session and its heartbeat
    REQUIRE((marker.Beat(1001).empty() &&
             read(logs / kRunningMarker) == started + beat(1000))); // beats are written every kHeartbeatSeconds
    REQUIRE((marker.Beat(1000 + kHeartbeatSeconds).empty() &&
             read(logs / kRunningMarker) == started + beat(1000 + kHeartbeatSeconds))); // a beat
    REQUIRE((marker.Background(1003).empty() &&
             read(logs / kRunningMarker) == started + beat(1002) + background(1003))); // background
    REQUIRE((marker.Foreground(1100).empty() &&
             read(logs / kRunningMarker) == started + beat(1100)));          // foreground again: unclean if killed
    REQUIRE((marker.Clear().empty() && !fs::exists(logs / kRunningMarker))); // a clean exit removes it
    REQUIRE((marker.Background(1200).empty() &&
             !fs::exists(logs / kRunningMarker))); // nothing is marked after a clean exit

    // Full storage fails every write: reported once per run of failures, again after a write succeeded.
    write(logs / "blocked", "a file where the marker's folder should be\n");
    SessionMarker blocked(logs / "blocked", "2026-11-13 18:30:01", 4242);
    REQUIRE(!blocked.Start(1000).empty()); // a failed write is reported
    REQUIRE((blocked.Beat(1000 + kHeartbeatSeconds).empty() &&
             blocked.Background(1003).empty())); // the next failures are not
    fs::remove(logs / "blocked");
    REQUIRE((blocked.Foreground(1100).empty() && fs::exists(logs / "blocked" / kRunningMarker))); // writable again
    fs::remove_all(logs / "blocked");
    write(logs / "blocked", "full again\n");
    REQUIRE(!blocked.Beat(1100 + kHeartbeatSeconds).empty()); // a new run of failures is reported again
    fs::remove(logs / "blocked");

    // At the next start: a clean background end is not reported, an unclean one is, with the last log line; both
    // consumed.
    write(logs / kRunningMarker, started + beat(1000) + background(1003));
    REQUIRE((takeUncleanEnd(logs, problems).empty() && !fs::exists(logs / kRunningMarker) &&
             problems.empty())); // a clean background end
    write(logs / kRunningMarker, started + beat(1000));
    write(logs / "system.log", "[18:30:01] start\n[18:41:07] [system] [info] last words\n\n");
    const auto unclean = takeUncleanEnd(logs, problems);
    REQUIRE((unclean.rfind("started 2026-11-13 18:30:01, pid 4242; ended in the foreground", 0) == 0 &&
             unclean.find("; its last log line: [18:41:07] [system] [info] last words") !=
                 std::string::npos)); // an unclean end is told with the session and its last log line
    REQUIRE((problems.empty() && !fs::exists(logs / kRunningMarker)));          // the marker is consumed
    REQUIRE(read(logs / "system.log").find("last words") != std::string::npos); // the log is read before the rotation

    std::puts(
        "PASS: three sessions kept, files move with their session, failures reported; the marker's decision table");
}
