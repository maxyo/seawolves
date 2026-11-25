// Host check of the JNI text conversions (src/libs/util/include/platform/jni_text.hpp): UTF-16 <-> UTF-8 with
// characters outside the BMP, invalid input in both directions, the empty string, the U+0000 policy, the lenient
// conversion for archive names, and the JNI wrappers against a fake JNIEnv (characters released, VM failures and
// pending exceptions kept).
#include "platform/jni_text.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace text = storm::jni_text;

namespace
{

template <class Call> std::string rejects(Call call)
{
    try
    {
        call();
    }
    catch (const std::invalid_argument &error)
    {
        return error.what();
    }
    return {};
}

// "Корсар 😀 𝄞 €": Cyrillic, two characters outside the BMP (U+1F600, U+1D11E), one of three UTF-8 bytes.
const std::string kUtf8 =
    "\xD0\x9A\xD0\xBE\xD1\x80\xD1\x81\xD0\xB0\xD1\x80 \xF0\x9F\x98\x80 \xF0\x9D\x84\x9E \xE2\x82\xAC";
const std::u16string kUtf16 = u"Корсар \U0001F600 \U0001D11E €";

void conversions()
{
    CHECK(text::ToUtf8(kUtf16) == kUtf8);                 // UTF-16 with supplementary characters to UTF-8
    CHECK(text::ToUtf16(kUtf8) == kUtf16);                // UTF-8 with supplementary characters to UTF-16
    CHECK(text::ToUtf16(text::ToUtf8(kUtf16)) == kUtf16); // round trip
    CHECK((text::ToUtf8(u""sv).empty() && text::ToUtf16(""sv).empty())); // the empty string is valid both ways
    CHECK(text::ToUtf8(u"\U0010FFFF"sv) == "\xF4\x8F\xBF\xBF");          // the last code point

    // UTF-16 from Java: a surrogate without its partner.
    CHECK(rejects([] { text::ToUtf8(u"a\xD83D"s); }).find("unit 1") !=
          std::string::npos);                                   // a high surrogate at the end
    CHECK(!rejects([] { text::ToUtf8(u"\xDE00z"s); }).empty()); // a lone low surrogate
    CHECK(!rejects([] { text::ToUtf8(u"\xD83Dz"s); }).empty()); // a high surrogate before a non-surrogate

    // UTF-8 from the engine: every class of malformed input, with the byte it starts at.
    const std::vector<std::pair<std::string, size_t>> invalid = {
        {"\x80", 0},                     // a continuation byte without a lead
        {"ab\xF0\x9F\x98", 2},           // a sequence cut short
        {"\xC0\xAF", 0},                 // an overlong '/'
        {"\xE0\x80\x80", 0},             // an overlong NUL
        {"\xED\xA0\x80", 0},             // an encoded surrogate
        {"\xED\xA0\xBD\xED\xB8\x80", 0}, // U+1F600 in Modified UTF-8 (what GetStringUTFChars gives)
        {"\xF4\x90\x80\x80", 0},         // above U+10FFFF
        {"x\xF5\x80\x80\x80", 1},        // a lead byte UTF-8 never uses
        {"\xC3(", 0},                    // a lead followed by a non-continuation
        {"\xCA\xEE\xF0\xF1\xE0\xF0", 0}, // Windows-1251 text
    };
    for (const auto &[bytes, at] : invalid)
    {
        const std::string why = rejects([&bytes = bytes] { text::ToUtf16(bytes); });
        INFO(("rejects malformed UTF-8: " + why).c_str());
        CHECK(why == "invalid UTF-8 at byte " + std::to_string(at));
    }

    // U+0000 never reaches a C string, either way; the lenient conversion keeps that rule.
    CHECK(rejects([] { text::ToUtf8(u"a\0b"s); }).find("U+0000") != std::string::npos); // U+0000 from Java
    CHECK(rejects([] { text::ToUtf16("a\0b"s); }).find("U+0000") != std::string::npos); // U+0000 from the engine
    size_t replaced = 0;
    CHECK(!rejects([&] { text::ToUtf16Replacing("a\0b"s, replaced); }).empty()); // U+0000 in the lenient conversion

    // The lenient conversion: each byte that starts no valid sequence is one U+FFFD, counted.
    CHECK((text::ToUtf16Replacing("a\xFF"
                                  "b\xF0\x9F"
                                  "c",
                                  replaced) == u"a�b��c" &&
           replaced == 3)); // lenient conversion replaces and counts
    CHECK((text::ToUtf16Replacing(kUtf8, replaced) == kUtf16 && replaced == 0)); // lenient conversion of valid text
}
} // namespace

#if __has_include(<jni.h>)
namespace
{
// A JNIEnv whose strings are the UTF-16 of a table; it records VM failures and leaks.
struct Fake
{
    JNINativeInterface_ table{};
    JNIEnv env{&table};
    std::u16string input = kUtf16;
    std::vector<std::u16string> made;
    bool pending = false, fail_chars = false, fail_new = false, violation = false;
    int acquired = 0, released = 0, thrown = 0, live = 0;
    static Fake *current;

    static jsize length(JNIEnv *, jstring)
    {
        return static_cast<jsize>(current->input.size());
    }
    static const jchar *chars(JNIEnv *, jstring, jboolean *)
    {
        if (current->pending)
            current->violation = true;
        if (current->fail_chars)
        {
            current->pending = true;
            return nullptr;
        }
        ++current->acquired;
        return reinterpret_cast<const jchar *>(current->input.data());
    }
    static void release(JNIEnv *, jstring, const jchar *)
    {
        ++current->released;
    }
    static jstring make(JNIEnv *, const jchar *data, jsize size)
    {
        if (current->pending)
            current->violation = true;
        if (current->fail_new)
        {
            current->pending = true;
            return nullptr;
        }
        current->made.emplace_back(reinterpret_cast<const char16_t *>(data), static_cast<size_t>(size));
        ++current->live;
        return reinterpret_cast<jstring>(current->made.size());
    }
    static jboolean check(JNIEnv *)
    {
        return current->pending;
    }
    static jclass find(JNIEnv *, const char *)
    {
        ++current->live;
        return reinterpret_cast<jclass>(1);
    }
    static jmethodID method(JNIEnv *, jclass, const char *, const char *)
    {
        return reinterpret_cast<jmethodID>(1);
    }
    static jobject construct(JNIEnv *, jclass, jmethodID, va_list)
    {
        ++current->live;
        return reinterpret_cast<jobject>(2);
    }
    static jint raise(JNIEnv *, jthrowable)
    {
        if (current->pending)
            current->violation = true;
        current->pending = true;
        ++current->thrown;
        return 0;
    }
    static void remove(JNIEnv *, jobject)
    {
        --current->live;
    }
    Fake()
    {
        current = this;
        table.GetStringLength = length;
        table.GetStringChars = chars;
        table.ReleaseStringChars = release;
        table.NewString = make;
        table.ExceptionCheck = check;
        table.FindClass = find;
        table.GetMethodID = method;
        table.NewObjectV = construct;
        table.Throw = raise;
        table.DeleteLocalRef = remove;
    }
};
Fake *Fake::current = nullptr;
const auto kString = reinterpret_cast<jstring>(1);

void wrappers()
{
    {
        Fake f;
        CHECK((text::ToUtf8(&f.env, kString) == kUtf8 && f.acquired == 1 && f.released == 1)); // a Java string's UTF-8
    }
    {
        Fake f;
        f.input = u"a\xD800"s;
        CHECK((!rejects([&] { text::ToUtf8(&f.env, kString); }).empty() &&
               f.released == 1)); // an invalid Java string is rejected and its characters released
    }
    {
        Fake f;
        CHECK((rejects([&] { text::ToUtf8(&f.env, nullptr); }) == "null string" &&
               f.acquired == 0)); // a null Java string
    }
    {
        Fake f;
        f.fail_chars = true;
        bool vm = false;
        try
        {
            text::ToUtf8(&f.env, kString);
        }
        catch (const std::invalid_argument &)
        {
        }
        catch (const std::runtime_error &)
        {
            vm = true;
        }
        CHECK((vm && f.pending && f.released == 0)); // a VM failure is a runtime_error with its exception pending
        std::string error;
        CHECK((!text::TryToUtf8(&f.env, kString, error) && error == "a Java exception is pending" &&
               !f.violation)); // no JNI call while an exception is pending
    }
    {
        Fake f;
        std::string error;
        f.input = u"\xDC00"s;
        CHECK((!text::TryToUtf8(&f.env, kString, error) && error.find("surrogate") != std::string::npos &&
               !f.pending)); // TryToUtf8 reports invalid text without a Java exception
    }
    {
        Fake f;
        const jstring made = text::ToJava(&f.env, kUtf8);
        CHECK((made && f.made.back() == kUtf16)); // a Java string of UTF-8 with supplementary characters
        CHECK((!rejects([&] { text::ToJava(&f.env, "\xED\xA0\xBD\xED\xB8\x80"sv); }).empty() &&
               f.made.size() == 1)); // Modified UTF-8 is not made into a Java string
        f.fail_new = true;
        CHECK((!text::ToJava(&f.env, "x"sv) && f.pending)); // a VM failure making a string is null with its exception
    }
    {
        Fake f;
        size_t replaced = 0;
        CHECK((text::ToJavaReplacing(&f.env, "dir\xCA\xEE", replaced) && f.made.back() == u"dir��" &&
               replaced == 2)); // an archive name that is not UTF-8
    }
    {
        Fake f;
        text::Throw(&f.env, "java/io/IOException", "\xF0\x9F\x98\x80 \xFF");
        CHECK((f.thrown == 1 && f.made.back() == u"\U0001F600 �" &&
               f.live == 0)); // an exception message keeps every character, references freed
        text::Throw(&f.env, "java/io/IOException", "second");
        CHECK((f.thrown == 1 && !f.violation)); // a pending exception is never replaced
    }
}
} // namespace
#endif

TEST_CASE("jni_text")
{
    conversions();
#if __has_include(<jni.h>)
    wrappers();
    const char *jni = "with the JNI wrappers";
#else
    const char *jni = "without the JNI wrappers (no jni.h)";
#endif
    std::printf("PASS: JNI text conversions %s\n", jni);
}
