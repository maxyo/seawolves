// UTF-8 text helpers for the interface's languages (ru, en, de, fr, es, it). The generic parts are RmlUi's
// (Rml::StringUtilities: StripWhitespace, LengthUTF8, ConvertCharacterOffsetToByteOffset, StringIteratorU8).
#pragma once

#include <RmlUi/Core/StringUtilities.h>

#include <string>
#include <string_view>

namespace storm::ui
{

using Rml::StringUtilities::StripWhitespace; // spaces, tabs, "\r" and "\n" at both ends

// Unicode simple case mapping of ASCII, the Latin-1 Supplement, Latin Extended-A and the Cyrillic letters
// U+0400–U+045F, which cover the six languages; other characters (ß, µ, everything from U+0460) are kept. It is
// RmlUi's (patches/rmlui-text-transform-unicode.patch), so the pack's `text-transform` maps the same letters. No
// library: the standard library maps ASCII only, ICU is in the NDK from API 31 only (minSdk is 24) and utf8proc
// carries about 300 KB of tables for every script.
std::string Lower(std::string_view text);
std::string Upper(std::string_view text);
// The first character upper case ("église" → "Église", "таверна" → "Таверна").
std::string Capitalized(std::string_view text);

// The longest start of `text` of at most `bytes` bytes that does not end inside a UTF-8 sequence.
std::string_view Utf8Prefix(std::string_view text, size_t bytes);

} // namespace storm::ui
