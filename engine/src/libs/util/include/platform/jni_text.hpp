#pragma once
// Text across the JNI boundary, for every native library of the APK (engine, UI runtime, archive reader, torrent).
//
// JNI's own UTF-8 calls (GetStringUTFChars, NewStringUTF) speak Modified UTF-8: a character outside the BMP (an
// emoji in a save name, a CJK folder name) is two 3-byte surrogate halves there, which is invalid UTF-8 for the
// engine, and standard 4-byte UTF-8 handed to NewStringUTF is rejected by CheckJNI. So text always crosses as UTF-16
// (GetStringChars, NewString), converted here.
//
// Policy, both directions:
// - Invalid input is an error, std::invalid_argument saying what and where: an unpaired surrogate in UTF-16; in
//   UTF-8 a stray or missing continuation byte, an overlong form, an encoded surrogate, a value above U+10FFFF.
// - U+0000 is rejected: the engine passes text on as C strings, where it would silently cut the text.
// - The empty string is valid.
// The lenient conversions (RepairUtf8, ToUtf16Replacing, ToJavaReplacing) are for text whose bytes come from outside
// (a save's description, names inside a player's archive or torrent): they count what they replaced, for the caller
// to report.
//
// C++17: the torrent library builds with it.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>

namespace storm::jni_text
{
// The code point of the UTF-8 sequence at `text[at]` and its length in bytes; 0 bytes when it is invalid.
inline std::pair<uint32_t, size_t> DecodeUtf8(std::string_view text, size_t at)
{
    const auto lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80)
        return {lead, 1};
    size_t length;
    uint32_t c, minimum;
    if (lead >= 0xC2 && lead <= 0xDF)
        length = 2, c = lead & 0x1F, minimum = 0x80;
    else if (lead >= 0xE0 && lead <= 0xEF)
        length = 3, c = lead & 0x0F, minimum = 0x800;
    else if (lead >= 0xF0 && lead <= 0xF4)
        length = 4, c = lead & 0x07, minimum = 0x10000;
    else
        return {0, 0};
    if (text.size() - at < length)
        return {0, 0};
    for (size_t i = 1; i < length; ++i)
    {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0) != 0x80)
            return {0, 0};
        c = (c << 6) | (next & 0x3F);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return {0, 0};
    return {c, length};
}

// Whether `text` is valid UTF-8 (U+0000 included).
inline bool IsUtf8(std::string_view text)
{
    for (size_t at = 0; at < text.size();)
    {
        const size_t length = DecodeUtf8(text, at).second;
        if (!length)
            return false;
        at += length;
    }
    return true;
}

// Makes `text` valid UTF-8 and keeps every valid character of it: each byte that does not start a valid sequence
// becomes U+FFFD. Returns how many bytes were replaced, for the caller to report (0: the text is unchanged).
inline size_t RepairUtf8(std::string &text)
{
    if (IsUtf8(text))
        return 0;
    size_t replaced = 0;
    std::string out;
    for (size_t at = 0; at < text.size();)
    {
        const size_t length = DecodeUtf8(text, at).second;
        if (length)
            out.append(text, at, length);
        else
        {
            out += "\xEF\xBF\xBD";
            ++replaced;
        }
        at += length ? length : 1;
    }
    text = std::move(out);
    return replaced;
}

namespace detail
{
inline void appendUtf8(std::string &out, uint32_t c)
{
    if (c < 0x80)
        out += static_cast<char>(c);
    else if (c < 0x800)
    {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

inline void appendUtf16(std::u16string &out, uint32_t c)
{
    if (c < 0x10000)
        out += static_cast<char16_t>(c);
    else
    {
        c -= 0x10000;
        out += static_cast<char16_t>(0xD800 + (c >> 10));
        out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
    }
}

[[noreturn]] inline void nul(const char *where, size_t at)
{
    throw std::invalid_argument(std::string("U+0000 in text at ") + where + ' ' + std::to_string(at) +
                                " (text is passed on as a C string)");
}

inline std::u16string toUtf16(std::string_view utf8, size_t *replaced)
{
    std::u16string out;
    out.reserve(utf8.size());
    for (size_t at = 0; at < utf8.size();)
    {
        const auto [c, length] = DecodeUtf8(utf8, at);
        if (!length)
        {
            if (!replaced)
                throw std::invalid_argument("invalid UTF-8 at byte " + std::to_string(at));
            ++*replaced;
            out += u'�';
            ++at;
            continue;
        }
        if (!c)
            nul("byte", at);
        appendUtf16(out, c);
        at += length;
    }
    return out;
}
} // namespace detail

// UTF-16 (a Java string's characters) as UTF-8.
inline std::string ToUtf8(std::u16string_view utf16)
{
    std::string out;
    out.reserve(utf16.size());
    for (size_t at = 0; at < utf16.size(); ++at)
    {
        uint32_t c = utf16[at];
        if (c >= 0xD800 && c <= 0xDBFF && at + 1 < utf16.size() && utf16[at + 1] >= 0xDC00 && utf16[at + 1] <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + (utf16[++at] - 0xDC00);
        else if (c >= 0xD800 && c <= 0xDFFF)
            throw std::invalid_argument("unpaired UTF-16 surrogate at unit " + std::to_string(at));
        if (!c)
            detail::nul("unit", at);
        detail::appendUtf8(out, c);
    }
    return out;
}

// UTF-8 as UTF-16.
inline std::u16string ToUtf16(std::string_view utf8)
{
    return detail::toUtf16(utf8, nullptr);
}

// UTF-8 as UTF-16, each byte that does not start a valid sequence as U+FFFD (`replaced` counts them); U+0000 is still
// an error.
inline std::u16string ToUtf16Replacing(std::string_view utf8, size_t &replaced)
{
    replaced = 0;
    return detail::toUtf16(utf8, &replaced);
}
} // namespace storm::jni_text

#if __has_include(<jni.h>)
#include <jni.h>
#include <optional>
#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace storm::jni_text
{
// The UTF-8 of a Java string. Throws std::invalid_argument for null or invalid text, std::runtime_error when the VM
// could not give its characters (its exception, OutOfMemoryError, stays pending).
inline std::string ToUtf8(JNIEnv *env, jstring value)
{
    if (!value)
        throw std::invalid_argument("null string");
    const jsize length = env->GetStringLength(value);
    const jchar *chars = env->GetStringChars(value, nullptr);
    if (!chars)
        throw std::runtime_error("the VM could not give the characters of a string");
    struct Release
    {
        JNIEnv *env;
        jstring value;
        const jchar *chars;
        ~Release()
        {
            env->ReleaseStringChars(value, chars);
        }
    } release{env, value, chars};
    return ToUtf8(std::u16string_view(reinterpret_cast<const char16_t *>(chars), static_cast<size_t>(length)));
}

// ToUtf8 for a native that reports instead of throwing: nothing when the text cannot be read, `error` saying why (after
// a VM failure its exception stays pending). Nothing, without a JNI call, while an exception is pending already.
inline std::optional<std::string> TryToUtf8(JNIEnv *env, jstring value, std::string &error)
{
    if (env->ExceptionCheck())
    {
        error = "a Java exception is pending";
        return std::nullopt;
    }
    try
    {
        return ToUtf8(env, value);
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return std::nullopt;
    }
}

// A new local reference to a Java string of UTF-16 text, or null when the VM failed (its exception pending).
inline jstring ToJava(JNIEnv *env, std::u16string_view utf16)
{
    if (utf16.size() > static_cast<size_t>(INT32_MAX))
        throw std::length_error("text too long for a Java string");
    return env->NewString(reinterpret_cast<const jchar *>(utf16.data()), static_cast<jsize>(utf16.size()));
}

// A new local reference to a Java string of UTF-8 text, or null when the VM failed (its exception pending). Throws
// std::invalid_argument for invalid UTF-8.
inline jstring ToJava(JNIEnv *env, std::string_view utf8)
{
    return ToJava(env, std::u16string_view(ToUtf16(utf8)));
}

// Like ToJava, for text made of bytes from outside (names inside a player's archive or torrent, which need not be
// UTF-8): such bytes become U+FFFD and `replaced` counts them, for the caller to report.
inline jstring ToJavaReplacing(JNIEnv *env, std::string_view utf8, size_t &replaced)
{
    return ToJava(env, std::u16string_view(ToUtf16Replacing(utf8, replaced)));
}

// ToJavaReplacing that logs, under `tag`, how many bytes of the `what` text ("archive", "torrent") were replaced.
inline jstring ToJavaReporting(JNIEnv *env, std::string_view utf8, const char *tag, const char *what)
{
    size_t replaced = 0;
    const jstring text = ToJavaReplacing(env, utf8, replaced);
    if (replaced)
    {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_WARN, tag, "%zu bytes of %s text that are not UTF-8 are shown as U+FFFD",
                            replaced, what);
#else
        std::fprintf(stderr, "%s: %zu bytes of %s text that are not UTF-8 are shown as U+FFFD\n", tag, replaced, what);
#endif
    }
    return text;
}

// Raises `class_name`(message) in Java unless an exception is pending already (a VM failure is never replaced).
// The message keeps every character: bytes of it that are not UTF-8 become U+FFFD, since an error report must get
// through whatever it quotes. When even that cannot be built (native memory exhausted), the exception carries a
// fixed ASCII message instead.
inline void Throw(JNIEnv *env, const char *class_name, std::string_view message) noexcept
{
    if (env->ExceptionCheck())
        return;
    const jclass type = env->FindClass(class_name);
    if (!type)
        return;
    struct Delete
    {
        JNIEnv *env;
        jobject ref;
        ~Delete()
        {
            if (ref)
                env->DeleteLocalRef(ref);
        }
    } type_ref{env, type};
    try
    {
        const jmethodID init = env->GetMethodID(type, "<init>", "(Ljava/lang/String;)V");
        if (!init)
            return;
        size_t replaced = 0;
        const jstring text = ToJava(env, std::u16string_view(ToUtf16Replacing(message, replaced)));
        if (!text)
            return;
        Delete text_ref{env, text};
        const auto error = static_cast<jthrowable>(env->NewObject(type, init, text));
        if (!error)
            return;
        Delete error_ref{env, error};
        env->Throw(error);
    }
    catch (...)
    {
        if (!env->ExceptionCheck())
            env->ThrowNew(type, "native error (its message could not be converted)");
    }
}
} // namespace storm::jni_text
#endif
