#pragma once

// The glyph entries of a fonts.ini section written as repeated `char = <glyph>,x,y,w,h` (Caribbean Legend), with two
// more numbers, the kerning on the left and on the right, in a section that has the `Kerning` key. <glyph> is one
// UTF-8 character, `com` for the comma, `equ` for `=`, or a letter followed by `_`.

#include <cstdint>
#include <string_view>

namespace storm::font
{
struct GlyphEntry
{
    char32_t codepoint = 0;
    int32_t x = 0, y = 0, width = 0, height = 0;
    int32_t kern_left = 0, kern_right = 0;
};

// Throws std::invalid_argument naming what is wrong.
GlyphEntry ParseGlyphEntry(std::string_view value, bool kerning);
} // namespace storm::font
