// Case mapping and UTF-8 cuts of src/text_utf8.h over the alphabets of the interface's six languages.
#include "../src/text_utf8.h"
#include "fakes.h"

using storm::ui::Capitalized;
using storm::ui::Lower;
using storm::ui::Upper;
using storm::ui::Utf8Prefix;

int main()
{
    // Upper and lower case of every letter that has both, per language.
    const char *const alphabets[][3] = {
        {"ru", "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ", "абвгдеёжзийклмнопрстуфхцчшщъыьэюя"},
        {"en", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"},
        {"de", "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ", "abcdefghijklmnopqrstuvwxyzäöü"},
        {"fr", "ABCDEFGHIJKLMNOPQRSTUVWXYZÀÂÆÇÉÈÊËÎÏÔŒÙÛÜŸ", "abcdefghijklmnopqrstuvwxyzàâæçéèêëîïôœùûüÿ"},
        {"es", "ABCDEFGHIJKLMNOPQRSTUVWXYZÁÉÍÑÓÚÜ", "abcdefghijklmnopqrstuvwxyzáéíñóúü"},
        {"it", "ABCDEFGHIJKLMNOPQRSTUVWXYZÀÈÉÌÍÎÒÓÙÚ", "abcdefghijklmnopqrstuvwxyzàèéìíîòóùú"},
    };
    for (const auto &alphabet : alphabets)
    {
        ExpectEqual(Lower(alphabet[1]), alphabet[2], std::string(alphabet[0]) + " lower case");
        ExpectEqual(Upper(alphabet[2]), alphabet[1], std::string(alphabet[0]) + " upper case");
    }

    ExpectEqual(Lower("ЁЖ И ЁЛКА"), "ёж и ёлка", "Ё lower case");
    ExpectEqual(Lower("ОТЛИЧНОЕ"), "отличное", "a game word lower case");
    // Letters without a one-letter pair, signs and other scripts stay.
    ExpectEqual(Upper("straße µ"), "STRAßE µ", "ß and µ stay");
    ExpectEqual(Lower("× ÷ 10 € — «Ы»"), "× ÷ 10 € — «ы»", "signs stay");
    ExpectEqual(Lower("ΑΒΓ 日本"), "ΑΒΓ 日本", "other scripts stay");
    ExpectEqual(Lower(std::string("A\xD0") + "B"), std::string("a\xD0") + "b", "a malformed byte is kept");

    ExpectEqual(Capitalized("église"), "Église", "fr place name");
    ExpectEqual(Capitalized("über den Fluss"), "Über den Fluss", "de: only the first letter");
    ExpectEqual(Capitalized("ñandú"), "Ñandú", "es");
    ExpectEqual(Capitalized("ìsola"), "Ìsola", "it");
    ExpectEqual(Capitalized("таверна"), "Таверна", "ru");
    ExpectEqual(Capitalized("ёлка"), "Ёлка", "ru ё");
    ExpectEqual(Capitalized("port"), "Port", "en");
    ExpectEqual(Capitalized(""), "", "empty");
    ExpectEqual(Capitalized("«дом»"), "«дом»", "a sign first");

    // "ая": two bytes each.
    ExpectEqual(std::string(Utf8Prefix("ая", 3)), "а", "cut inside a character");
    ExpectEqual(std::string(Utf8Prefix("ая", 4)), "ая", "whole text");
    ExpectEqual(std::string(Utf8Prefix("aя", 2)), "a", "cut before a lead byte");
    ExpectEqual(std::string(Utf8Prefix("я", 1)), "", "nothing fits");
    ExpectEqual(std::string(Utf8Prefix("€x", 2)), "", "three-byte character");

    return Finish("text");
}
