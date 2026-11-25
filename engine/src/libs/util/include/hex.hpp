#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace storm
{
// Bytes as lower-case hexadecimal digits, two per byte.
inline std::string ToHex(std::string_view bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes)
    {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

// ToHex's text back to bytes; nothing for an odd length or a character that is no lower-case hex digit.
inline std::optional<std::string> FromHex(std::string_view text)
{
    if (text.size() % 2)
        return std::nullopt;
    const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string bytes;
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2)
    {
        const int high = digit(text[i]), low = digit(text[i + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        bytes += static_cast<char>((high << 4) | low);
    }
    return bytes;
}
} // namespace storm
