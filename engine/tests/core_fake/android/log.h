#pragma once
// The NDK's log for engine code built for a host check: to stderr.
#include <cstdarg>
#include <cstdio>
enum
{
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
};
inline int __android_log_print(int, const char *tag, const char *format, ...)
{
    std::fprintf(stderr, "%s: ", tag);
    va_list list;
    va_start(list, format);
    const int written = std::vfprintf(stderr, format, list);
    va_end(list);
    std::fputc('\n', stderr);
    return written;
}
