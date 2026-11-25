#include "text_utf8.h"

namespace storm::ui
{

namespace
{

bool Continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

char32_t LowerCode(char32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) // À–Þ without ×
        return c + 0x20;
    if (c == 0x130) // İ
        return 'i';
    if (c == 0x178) // Ÿ
        return 0xFF;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) // Ā/ā … Ķ/ķ, Ŋ/ŋ … Ŷ/ŷ: upper case even
        return c | 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) // Ĺ/ĺ … Ň/ň, Ź/ź … Ž/ž: upper case odd
        return c + (c & 1);
    if (c >= 0x400 && c <= 0x40F) // Ѐ–Џ (Ё)
        return c + 0x50;
    if (c >= 0x410 && c <= 0x42F) // А–Я
        return c + 0x20;
    return c;
}

char32_t UpperCode(char32_t c)
{
    if (c >= 'a' && c <= 'z')
        return c - 0x20;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) // à–þ without ÷
        return c - 0x20;
    if (c == 0xFF) // ÿ
        return 0x178;
    if (c == 0x131) // ı
        return 'I';
    if (c == 0x17F) // ſ
        return 'S';
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
        return c & ~char32_t(1);
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return c - ((c & 1) ^ 1);
    if (c >= 0x430 && c <= 0x44F) // а–я
        return c - 0x20;
    if (c >= 0x450 && c <= 0x45F) // ѐ–џ (ё)
        return c - 0x50;
    return c;
}

// Maps the first `count` characters. Every mapped character is below U+0800, so it is one or two bytes; longer
// sequences and malformed bytes are copied as they are.
std::string Mapped(std::string_view text, char32_t (*map)(char32_t), size_t count)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    for (; i < text.size() && count > 0; --count)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80)
        {
            out += static_cast<char>(map(lead));
            ++i;
            continue;
        }
        if ((lead & 0xE0) == 0xC0 && i + 1 < text.size() && Continuation(text[i + 1]))
        {
            const char32_t code = map(((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu));
            if (code < 0x80)
                out += static_cast<char>(code);
            else
                out += {static_cast<char>(0xC0 | (code >> 6)), static_cast<char>(0x80 | (code & 0x3F))};
            i += 2;
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && Continuation(text[end]))
            ++end;
        out.append(text.substr(i, end - i));
        i = end;
    }
    out.append(text.substr(i));
    return out;
}

} // namespace

std::string Lower(std::string_view text)
{
    return Mapped(text, LowerCode, std::string_view::npos);
}

std::string Upper(std::string_view text)
{
    return Mapped(text, UpperCode, std::string_view::npos);
}

std::string Capitalized(std::string_view text)
{
    return Mapped(text, UpperCode, 1);
}

std::string_view Utf8Prefix(std::string_view text, size_t bytes)
{
    if (bytes >= text.size())
        return text;
    while (bytes > 0 && Continuation(text[bytes]))
        --bytes;
    return text.substr(0, bytes);
}

} // namespace storm::ui
