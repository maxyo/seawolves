// platform/process_memory.hpp: the /proc fields in MB and the resident peaks by kind with their times.
#include "platform/process_memory.hpp"

#include <catch2/catch.hpp>

#include <string>

namespace pm = storm::platform::process_memory;

namespace
{
// /proc/self/status as the kernel writes it (the fields around the ones read, tabs and spaces as there).
constexpr const char *kStatus = "Name:\tengine\n"
                                "VmPeak:\t 9000000 kB\n"
                                "VmHWM:\t 1740800 kB\n"
                                "VmRSS:\t  512000 kB\n"
                                "RssAnon:\t  307200 kB\n"
                                "RssFile:\t  204800 kB\n"
                                "RssShmem:\t    1024 kB\n"
                                "VmSwap:\t       0 kB\n";

std::string status(int anon_kb, int file_kb)
{
    return "Name:\tengine\nRssAnon:\t " + std::to_string(anon_kb) + " kB\nRssFile:\t " + std::to_string(file_kb) +
           " kB\n";
}
} // namespace

TEST_CASE("fields of a /proc text in MB; a missing one is -1")
{
    double rss = 0, anon = 0, file = 0, swap = 0, peak = 0, missing = 0;
    pm::FieldsMb(kStatus, {{"VmRSS", &rss},
                           {"RssAnon", &anon},
                           {"RssFile", &file},
                           {"VmSwap", &swap},
                           {"VmHWM", &peak},
                           {"MemAvailable", &missing}});
    CHECK(rss == Approx(500.0));
    CHECK(anon == Approx(300.0));
    CHECK(file == Approx(200.0));
    CHECK(swap == 0.0);
    CHECK(peak == Approx(1700.0));
    CHECK(missing == -1.0);
    // "Rss" is not "RssAnon": a key matches the whole field name only.
    double rss_prefix = 0;
    pm::FieldsMb(kStatus, {{"Rss", &rss_prefix}});
    CHECK(rss_prefix == -1.0);
}

TEST_CASE("the peaks of each kind keep their maximum and its time")
{
    const pm::RssPeaks &peaks = pm::Peaks();
    CHECK(peaks.anon_mb == -1.0); // nothing noted yet
    pm::NoteRssPeaks(status(100 * 1024, 900 * 1024), 1.0);
    pm::NoteRssPeaks(status(1500 * 1024, 300 * 1024), 4.5); // the anonymous peak, file pages lower
    pm::NoteRssPeaks(status(600 * 1024, 950 * 1024), 9.0);  // the file peak, later
    pm::NoteRssPeaks(status(700 * 1024, 100 * 1024), 12.0);
    CHECK(peaks.anon_mb == Approx(1500.0));
    CHECK(peaks.anon_at_s == Approx(4.5));
    CHECK(peaks.file_mb == Approx(950.0));
    CHECK(peaks.file_at_s == Approx(9.0));
    // An unreadable status (no fields) changes nothing.
    pm::NoteRssPeaks("", 20.0);
    CHECK(peaks.anon_mb == Approx(1500.0));
    CHECK(peaks.file_at_s == Approx(9.0));
}

TEST_CASE("this process's age")
{
    const double seconds = pm::ProcessSeconds();
    CHECK(seconds >= 0.0);
    CHECK(seconds < 600.0); // a check runs for seconds
}

TEST_CASE("a start phase keeps its first mark: memory now and the largest noted up to it")
{
    pm::RssPeaks peaks;
    peaks.anon_mb = 1600.0; // sampled between two phases, gone again at the next
    peaks.file_mb = 100.0;
    pm::StartMemory start;
    start.Mark(pm::StartPhase::TitleBegins, status(400 * 1024, 300 * 1024), 4.0, peaks);
    start.Mark(pm::StartPhase::TitleBegins, status(900 * 1024, 900 * 1024), 6.0, peaks); // a later scene's begin
    const pm::StartMark &mark = start.Marks()[static_cast<size_t>(pm::StartPhase::TitleBegins)];
    CHECK(mark.seconds == Approx(4.0));
    CHECK(mark.anon == Approx(400.0));
    CHECK(mark.file == Approx(300.0));
    CHECK(mark.anon_peak == Approx(1600.0)); // the sampled peak
    CHECK(mark.file_peak == Approx(300.0));  // the mark itself is above what was sampled
    CHECK(start.Marks()[static_cast<size_t>(pm::StartPhase::TitleShown)].seconds < 0.0); // not reached
}
