#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "jni_text.hpp"

namespace storm::android
{

inline constexpr size_t dialogueEditMaxCodePoints = 256;
inline constexpr size_t dialogueEditMaxBytes = 1024;

struct DialogueInput
{
    uint64_t generation;
    unsigned index;
    // No value means an ordinary choice; an empty value is an explicit empty edit.
    std::optional<std::string_view> text;
};

// The outer "dialogue:" prefix is removed by the existing game-thread mailbox.
// Only the first two separators are metadata. User text is never script source.
inline std::optional<DialogueInput> parseDialogueInput(std::string_view input)
{
    const auto first = input.find(':');
    if (first == std::string_view::npos)
        return {};
    const auto second = input.find(':', first + 1);
    const auto end = second == std::string_view::npos ? input.size() : second;
    DialogueInput result{};
    const auto a = std::from_chars(input.data(), input.data() + first, result.generation);
    const auto b = std::from_chars(input.data() + first + 1, input.data() + end, result.index);
    if (a.ec != std::errc{} || a.ptr != input.data() + first || b.ec != std::errc{} || b.ptr != input.data() + end)
        return {};
    if (second != std::string_view::npos)
        result.text = input.substr(second + 1);
    return result;
}

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
