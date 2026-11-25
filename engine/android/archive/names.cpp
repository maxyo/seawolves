// Entry names that are not UTF-8: the Russian DOS and Windows code pages, decoded once where the reader takes a
// name from libarchive, so that every name the reader validates, reports and writes is UTF-8.
#include "internal.hpp"
#include "platform/jni_text.hpp"
#include <cstdio>
#ifdef __ANDROID__
#include <android/log.h>
#endif
namespace storm_archive
{
namespace
{
// Bytes 0x80-0xFF. 0: not a character of the code page.
const uint16_t cp866[128] = {
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C,
    0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429,
    0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436,
    0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x2591, 0x2592, 0x2593, 0x2502,
    0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
    0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
    0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
    0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F, 0x0401, 0x0451, 0x0404, 0x0454, 0x0407,
    0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0};
const uint16_t cp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0,      0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
    0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
    0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
    0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};

void append(std::string &out, uint32_t c)
{
    if (c < 0x80)
        out += static_cast<char>(c);
    else if (c < 0x800)
    {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Letters of the Cyrillic alphabets of Russian, Ukrainian and Belarusian.
bool letter(uint32_t c)
{
    switch (c)
    {
    case 0x0401:
    case 0x0451:
    case 0x0404:
    case 0x0454:
    case 0x0406:
    case 0x0456:
    case 0x0407:
    case 0x0457:
    case 0x040E:
    case 0x045E:
    case 0x0490:
    case 0x0491:
        return true;
    default:
        return c >= 0x0410 && c <= 0x044F;
    }
}

// Typography that occurs in Russian names beside letters: № « » – —.
bool sign(uint32_t c)
{
    return c == 0x2116 || c == 0x00AB || c == 0x00BB || c == 0x2013 || c == 0x2014;
}

// The name in `table`, and how many Cyrillic letters it has; -1 when it does not read as Cyrillic text: a byte
// decodes to anything but a letter or one of the signs (box drawing, Serbian letters, currency), or a word mixes
// Latin and Cyrillic letters (Western or CJK text read in the wrong code page: "caf\xE9" as "cafщ").
int decode(const std::string &raw, const uint16_t *table, std::string &out)
{
    int letters = 0;
    bool latin = false, cyrillic = false; // in the current word
    out.clear();
    for (unsigned char b : raw)
    {
        uint32_t c = b;
        if (b >= 0x80)
        {
            c = table[b - 0x80];
            if (!letter(c) && !sign(c))
                return -1;
        }
        const bool ascii_letter = (c | 0x20) >= 'a' && (c | 0x20) <= 'z';
        if (ascii_letter || letter(c))
        {
            (ascii_letter ? latin : cyrillic) = true;
            if (latin && cyrillic)
                return -1;
            letters += !ascii_letter;
        }
        else
            latin = cyrillic = false;
        append(out, c);
    }
    return letters;
}
} // namespace

std::string printable_name(const std::string &raw)
{
    std::string out;
    for (size_t at = 0; at < raw.size();)
    {
        const auto [c, length] = storm::jni_text::DecodeUtf8(raw, at);
        if (length && c >= 0x20 && c != 0x7F && (c < 0x80 || c > 0x9F))
        {
            out.append(raw, at, length);
            at += length;
            continue;
        }
        char hex[5];
        std::snprintf(hex, sizeof hex, "\\x%02X", static_cast<unsigned char>(raw[at]));
        out += hex;
        ++at;
    }
    return out;
}

std::string windows1251_text(const std::string &s)
{
    std::string out;
    for (unsigned char b : s)
        append(out, b < 0x80 ? b : cp1251[b - 0x80] ? cp1251[b - 0x80] : '?');
    return out;
}

std::string LegacyNames::decode(const std::string &raw)
{
    if (storm::jni_text::IsUtf8(raw))
        return raw;
    std::string dos, windows;
    const int dos_letters = storm_archive::decode(raw, cp866, dos);
    const int windows_letters = storm_archive::decode(raw, cp1251, windows);
    if (dos_letters <= 0 && windows_letters <= 0)
        throw Failure(STORM_ARCHIVE_INVALID,
                      "Archive entry name is neither UTF-8 nor Cyrillic DOS (CP866) or Windows (CP1251) text: " +
                          printable_name(raw));
    // The ZIP format defines a name without the UTF-8 flag as DOS (OEM) text, which is what Windows' own ZIP
    // folders, 7-Zip and WinRAR write on a Russian system; Windows-1251 needs more letters to win.
    const bool windows_wins = windows_letters > dos_letters;
    if (windows_letters == dos_letters)
        ++ambiguous;
    ++(windows_wins ? as_windows : as_dos);
    if (example.empty())
        example = windows_wins ? windows : dos;
    return windows_wins ? windows : dos;
}

void LegacyNames::report(const std::string &container) const
{
    if (!as_dos && !as_windows)
        return;
    warn(std::to_string(as_dos + as_windows) + " entry names in " + container +
         " are not UTF-8: " + std::to_string(as_dos) + " read as CP866, " + std::to_string(as_windows) +
         " as CP1251 (" + std::to_string(ambiguous) + " fit both and were read as CP866), first: " + example);
}

void warn(const std::string &message)
{
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_WARN, "StormArchive", "%s", message.c_str());
#else
    std::fprintf(stderr, "StormArchive: %s\n", message.c_str());
#endif
}
} // namespace storm_archive
