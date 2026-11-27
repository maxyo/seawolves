#include "font_glyphs.hpp"

#include "utf8.h"

#include <charconv>
#include <stdexcept>
#include <string>

namespace storm::font
{
namespace
{
char32_t GlyphCodepoint(std::string_view symbol)
{
    if (symbol == "com")
        return ',';
    if (symbol == "equ")
        return '=';
    if (symbol.size() == 2 && symbol[1] == '_')
        return static_cast<unsigned char>(symbol[0]);
    const std::string text(symbol);
    if (!text.empty() && static_cast<size_t>(utf8::u8_inc(text.c_str())) == text.size())
        if (const int codepoint = utf8::Utf8ToCodepoint(text.c_str()); codepoint > 0)
            return static_cast<char32_t>(codepoint);
    throw std::invalid_argument("glyph '" + text + "' is not one character, com, equ or <letter>_");
}
} // namespace

GlyphEntry ParseGlyphEntry(std::string_view value, bool kerning)
{
    const size_t comma = value.find(',');
    if (comma == std::string_view::npos)
        throw std::invalid_argument("no numbers after the glyph");
    GlyphEntry glyph;
    glyph.codepoint = GlyphCodepoint(value.substr(0, comma));
    int32_t *numbers[] = {&glyph.x, &glyph.y, &glyph.width, &glyph.height, &glyph.kern_left, &glyph.kern_right};
    const size_t count = kerning ? 6 : 4;
    std::string_view rest = value.substr(comma + 1);
    for (size_t i = 0; i < count; ++i)
    {
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
        const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), *numbers[i]);
        if (error != std::errc{})
            throw std::invalid_argument("number " + std::to_string(i + 1) + " is missing or not a number");
        rest.remove_prefix(static_cast<size_t>(end - rest.data()));
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\r'))
            rest.remove_prefix(1);
        if (i + 1 < count)
        {
            if (rest.empty() || rest.front() != ',')
                throw std::invalid_argument(std::to_string(count) + " numbers expected, found " +
                                            std::to_string(i + 1));
            rest.remove_prefix(1);
        }
    }
    if (!rest.empty())
        throw std::invalid_argument("more than " + std::to_string(count) + " numbers");
    if (glyph.width <= 0 || glyph.height <= 0)
        throw std::invalid_argument("the glyph's size is not positive");
    return glyph;
}
} // namespace storm::font
