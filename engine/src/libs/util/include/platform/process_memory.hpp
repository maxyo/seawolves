// The process's memory from /proc (Linux, Android) for the renderer's "memory:" line, and the resident peaks by
// kind: VmHWM alone cannot say whether a peak was anonymous memory (what the low-memory killer counts against the
// app) or file pages (reclaimable). The stall watchdog's thread notes them every poll from the engine's start: the
// peak can sit inside one long load frame, where the game thread logs nothing.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <initializer_list>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace storm::platform::process_memory
{

// The "<key>: <n> kB" fields of a /proc text (status, meminfo) in MB, -1 for a key the text lacks.
inline void FieldsMb(std::string_view text, std::initializer_list<std::pair<std::string_view, double *>> keys)
{
    for (const auto &[key, value] : keys)
        *value = -1.0;
    while (!text.empty())
    {
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        for (const auto &[key, value] : keys)
        {
            if (line.size() <= key.size() || line.substr(0, key.size()) != key || line[key.size()] != ':')
                continue;
            std::string_view number = line.substr(key.size() + 1);
            number.remove_prefix(std::min(number.find_first_not_of(" \t"), number.size()));
            long long kb = 0;
            if (std::from_chars(number.data(), number.data() + number.size(), kb).ec == std::errc{})
                *value = static_cast<double>(kb) / 1024.0;
        }
    }
}

// A /proc file's text into `buffer` (status and meminfo are 1-2 KiB); empty when it cannot be read.
template <size_t N> std::string_view ReadProc(const char *path, char (&buffer)[N])
{
    FILE *file = std::fopen(path, "r");
    if (!file)
        return {};
    const size_t size = std::fread(buffer, 1, N, file);
    std::fclose(file);
    return {buffer, size};
}

// Seconds since this process started (the kernel's start time in /proc/self/stat against CLOCK_BOOTTIME); -1 where
// it is unknown.
inline double ProcessSeconds()
{
#if defined(__linux__)
    static const double start = [] {
        char buffer[1024];
        const std::string_view stat = ReadProc("/proc/self/stat", buffer);
        // Field 22, counted after the command name's closing parenthesis (the name may hold spaces).
        size_t at = stat.rfind(')');
        for (int field = 2; field < 22 && at != std::string_view::npos; ++field)
            at = stat.find(' ', at + 1);
        unsigned long long ticks = 0;
        if (at == std::string_view::npos ||
            std::from_chars(stat.data() + at + 1, stat.data() + stat.size(), ticks).ec != std::errc{})
            return -1.0;
        return static_cast<double>(ticks) / static_cast<double>(sysconf(_SC_CLK_TCK));
    }();
    timespec now{};
    if (start < 0.0 || clock_gettime(CLOCK_BOOTTIME, &now) != 0)
        return -1.0;
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9 - start;
#else
    return -1.0;
#endif
}

struct RssPeaks
{
    std::atomic<double> anon_mb{-1.0}, file_mb{-1.0};     // the largest RssAnon / RssFile noted
    std::atomic<double> anon_at_s{-1.0}, file_at_s{-1.0}; // when, in seconds since the process started
};

inline RssPeaks &Peaks()
{
    static RssPeaks peaks;
    return peaks;
}

// Keeps the maxima of a /proc/self/status text read `seconds` after the process started. One writer: the stall
// watchdog's thread.
inline void NoteRssPeaks(std::string_view status, double seconds)
{
    double anon = 0.0, file = 0.0;
    FieldsMb(status, {{"RssAnon", &anon}, {"RssFile", &file}});
    RssPeaks &peaks = Peaks();
    if (anon > peaks.anon_mb.load(std::memory_order_relaxed))
    {
        peaks.anon_at_s.store(seconds, std::memory_order_relaxed);
        peaks.anon_mb.store(anon, std::memory_order_relaxed);
    }
    if (file > peaks.file_mb.load(std::memory_order_relaxed))
    {
        peaks.file_at_s.store(seconds, std::memory_order_relaxed);
        peaks.file_mb.store(file, std::memory_order_relaxed);
    }
}

inline void NoteRssPeaks()
{
    char buffer[4096];
    NoteRssPeaks(ReadProc("/proc/self/status", buffer), ProcessSeconds());
}

// The start's phases, in order, for the "memory at start" line: where in the start a peak (as the 1.6 GB anonymous one
// of a first start after an install) happened.
enum class StartPhase : uint8_t
{
    Loaded,         // the engine library loaded
    RendererBegins, // the renderer starts (after the engine's start and its first script compile)
    BgfxBegins,     // bgfx::init is called (after the Vulkan probe)
    BgfxInit,       // bgfx initialised
    SelfTest,       // the render backend's self-test passed
    TitleBegins,    // the first scene (the title) begins loading
    TitleShown,     // ... and is shown
    FirstScene,     // the scene after it is shown
    Count,
};
inline constexpr size_t kStartPhases = static_cast<size_t>(StartPhase::Count);
inline constexpr const char *kStartPhaseNames[kStartPhases] = {
    "loaded", "renderer_begins", "bgfx_begins", "bgfx_init", "self_test", "title_begins", "title_shown", "first_scene"};

// RssAnon and RssFile at a phase and the largest of each noted up to it, in MB; the seconds since the process started
// (-1: the phase was not reached).
struct StartMark
{
    double seconds = -1.0, anon = -1.0, file = -1.0, anon_peak = -1.0, file_peak = -1.0;
};

// One thread (the game thread, after the library's load). A phase keeps its first mark.
class StartMemory
{
  public:
    void Mark(StartPhase phase, std::string_view status, double seconds, const RssPeaks &peaks)
    {
        StartMark &mark = marks_[static_cast<size_t>(phase)];
        if (mark.seconds >= 0.0)
            return;
        FieldsMb(status, {{"RssAnon", &mark.anon}, {"RssFile", &mark.file}});
        mark.anon_peak = std::max(mark.anon, peaks.anon_mb.load(std::memory_order_relaxed));
        mark.file_peak = std::max(mark.file, peaks.file_mb.load(std::memory_order_relaxed));
        mark.seconds = std::max(seconds, 0.0);
    }
    const std::array<StartMark, kStartPhases> &Marks() const
    {
        return marks_;
    }

  private:
    std::array<StartMark, kStartPhases> marks_{};
};

inline StartMemory &Start()
{
    static StartMemory start;
    return start;
}

inline void MarkStart(StartPhase phase)
{
    char buffer[4096];
    Start().Mark(phase, ReadProc("/proc/self/status", buffer), ProcessSeconds(), Peaks());
}

} // namespace storm::platform::process_memory
