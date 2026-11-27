#include "font_glyphs.hpp"

#include <stdexcept>

#include <catch2/catch.hpp>

using storm::font::ParseGlyphEntry;

TEST_CASE("font_glyphs.entries")
{
    const auto a = ParseGlyphEntry("A,16,0,14,75", false);
    CHECK(a.codepoint == U'A');
    CHECK((a.x == 16 && a.y == 0 && a.width == 14 && a.height == 75));
    CHECK((a.kern_left == 0 && a.kern_right == 0));
    CHECK(ParseGlyphEntry("com,309,0,14,75", false).codepoint == U',');
    CHECK(ParseGlyphEntry("equ,1,2,3,4", false).codepoint == U'=');
    CHECK(ParseGlyphEntry("q_,1,2,3,4", false).codepoint == U'q');
    CHECK(ParseGlyphEntry("\xE2\x93\xAA,128,192,64,64", false).codepoint == U'⓪'); // circled zero, past 0x2070
    CHECK(ParseGlyphEntry("\",34, 0, 19, 75", false).codepoint == U'"');

    const auto kerned = ParseGlyphEntry("!,47,0,17,75,2,4", true);
    CHECK((kerned.kern_left == 2 && kerned.kern_right == 4));
}

TEST_CASE("font_glyphs.errors")
{
    CHECK_THROWS_AS(ParseGlyphEntry("A,1,2,3", false), std::invalid_argument);
    CHECK_THROWS_AS(ParseGlyphEntry("A,1,2,3,4,5,6", false), std::invalid_argument); // kerning numbers without Kerning
    CHECK_THROWS_AS(ParseGlyphEntry("A,1,2,3,4", true), std::invalid_argument);
    CHECK_THROWS_AS(ParseGlyphEntry("AB,1,2,3,4", false), std::invalid_argument);
    CHECK_THROWS_AS(ParseGlyphEntry("A,1,x,3,4", false), std::invalid_argument);
    CHECK_THROWS_AS(ParseGlyphEntry("A,1,2,0,4", false), std::invalid_argument);
    CHECK_THROWS_AS(ParseGlyphEntry("A", false), std::invalid_argument);
}
