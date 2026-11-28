#include "utf8.h"

#include "platform/jni_text.hpp"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <memory>
#include <string>

#include <catch2/catch.hpp>

TEST_CASE("utf8")
{
    using utf8::Utf8ToCodepoint;
    REQUIRE(Utf8ToCodepoint("") == -1);
    REQUIRE(Utf8ToCodepoint("A") == 'A');
    REQUIRE(Utf8ToCodepoint("\x7f") == 0x7f);
    REQUIRE(Utf8ToCodepoint("\xD0\x96") == 0x416);           // Ж
    REQUIRE(Utf8ToCodepoint("\xE2\x82\xAC") == 0x20AC);      // €
    REQUIRE(Utf8ToCodepoint("\xF0\x9F\x98\x80") == 0x1F600); // 😀
    // A decoder needs at most four bytes, regardless of the remaining text.
    // ASan catches strlen or any attempt to inspect a fifth byte here.
    {
        std::unique_ptr<char[]> exact(new char[4]);
        std::memcpy(exact.get(), "\xF0\x9F\x98\x80", 4);
        REQUIRE(Utf8ToCodepoint(exact.get()) == 0x1F600);
    }
    // Truncated sequences: no read past the terminator (heap copies of exact size, ASan).
    for (const char *text : {"\xD0", "\xE2\x82", "\xF0\x9F\x98", "\xE2", "\xF0"})
    {
        const size_t size = std::strlen(text) + 1;
        std::unique_ptr<char[]> exact(new char[size]);
        std::memcpy(exact.get(), text, size);
        REQUIRE(Utf8ToCodepoint(exact.get()) == -1);
    }
    // Encoded UTF-16 surrogates (U+D800..U+DFFF) are invalid; signed char used to hide this check.
    REQUIRE(Utf8ToCodepoint("\xED\xA0\x80") == -1);
    REQUIRE(Utf8ToCodepoint("\xED\xBF\xBF") == -1);
    REQUIRE(Utf8ToCodepoint("\xED\x9F\xBF") == 0xD7FF); // last code point before the surrogates
    REQUIRE(Utf8ToCodepoint("\xEE\x80\x80") == 0xE000); // first one after
    // Round trip through the encoder.
    for (uint32_t cp : {0x24u, 0x416u, 0x20ACu, 0xFFFDu, 0x1F600u, 0x10FFFFu})
    {
        char out[5];
        utf8::CodepointToUtf8(out, cp);
        REQUIRE(static_cast<uint32_t>(Utf8ToCodepoint(out)) == cp);
    }
    // Decoding every character of a long text is linear: 4 MiB took minutes with strlen per call. Measured in this
    // thread's CPU time, which other processes on a loaded host do not inflate (wall time did).
    std::string text;
    constexpr int repetitions = 512 * 1024;
    for (int i = 0; i < repetitions; ++i)
        text += "\xD0\x96\xE2\x82\xAC\xF0\x9F\x98\x80";
    const auto cpu_ms = [] {
        timespec now{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        return now.tv_sec * 1e3 + now.tv_nsec / 1e6;
    };
    const double begin = cpu_ms();
    long long sum = 0;
    for (size_t i = 0; i < text.size(); i += utf8::u8_inc(text.c_str() + i))
        sum += Utf8ToCodepoint(text.c_str() + i);
    const double ms = cpu_ms() - begin;
    REQUIRE((sum == static_cast<long long>(repetitions) * (0x416 + 0x20AC + 0x1F600) && ms < 5000));
    std::printf("utf8: decode/surrogates/bounds PASS (%zu bytes walked in %.0f ms of CPU under ASan)\n", text.size(),
                ms);
}

// The one validation and repair of UTF-8 text (jni_text IsUtf8, RepairUtf8).
TEST_CASE("utf8.repair")
{
    using storm::jni_text::IsUtf8;
    const auto repaired = [](std::string text) {
        const size_t replaced = storm::jni_text::RepairUtf8(text);
        REQUIRE(IsUtf8(text));
        return std::pair(text, replaced);
    };
    const std::string mark = "\xEF\xBF\xBD"; // U+FFFD

    for (const char *valid : {"", "Port Royal", "Сен-Пьер", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\xF4\x8F\xBF\xBF"})
    {
        CHECK(IsUtf8(valid));
        CHECK(repaired(valid) == std::pair(std::string(valid), size_t{0}));
    }
    // Not UTF-8: a stray lead or continuation byte, overlong forms, an encoded surrogate, a value above U+10FFFF,
    // the lead bytes of five and six byte forms, a sequence cut by the end of the text.
    for (const char *invalid :
         {"\xFF", "\x80", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", "\xED\xA0\x80",
          "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80", "\xD0", "\xE2\x82", "\xF0\x9F\x98"})
    {
        INFO(invalid);
        CHECK(!IsUtf8(invalid));
        CHECK(repaired(invalid).second == std::strlen(invalid));
    }
    // Every valid character around the bad bytes is kept.
    CHECK(repaired("Сен-\xFFПьер") == std::pair("Сен-" + mark + "Пьер", size_t{1}));
    CHECK(repaired("\xC0\xAFМартиника\xD0") == std::pair(mark + mark + "Мартиника" + mark, size_t{3}));
    CHECK(repaired("\xD0\xD0\x96") == std::pair(mark + "Ж", size_t{1})); // a lead byte before a whole character
}
