#pragma once

#include <cstddef>
#include <string_view>

#include "jni_text.hpp"

namespace storm::android
{

inline constexpr size_t dialogueEditMaxCodePoints = 256;
inline constexpr size_t dialogueEditMaxBytes = 1024;

// Empty strings and punctuation are intentional script inputs. Reject rather
// than repair invalid text so the value shown by Android is the value committed.
inline std::string_view dialogueEditError(std::string_view text)
{
    if (text.size() > dialogueEditMaxBytes)
        return "too_long";
    size_t codePoints = 0;
    for (size_t i = 0; i < text.size();)
    {
        const auto [c, length] = jni_text::DecodeUtf8(text, i);
        // C0/C1 controls and Unicode line/paragraph separators are not single-line input.
        if (!length || c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029)
            return "invalid_text";
        i += length;
        if (++codePoints > dialogueEditMaxCodePoints)
            return "too_long";
    }
    return {};
}

} // namespace storm::android
