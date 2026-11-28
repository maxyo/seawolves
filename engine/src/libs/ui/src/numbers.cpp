// ParseAmount (numbers.h): no screen or text library behind it, the script checks read amounts by it too.
#include "numbers.h"

#include <climits>

namespace storm::ui
{

namespace
{

bool Digit(char c)
{
    return c >= '0' && c <= '9';
}

// The length of a thousands separator at `at` (a space, or a no-break space in UTF-8), 0 when there is none.
size_t Separator(std::string_view text, size_t at)
{
    if (text.compare(at, 1, " ") == 0)
        return 1;
    if (text.compare(at, 2, "\xC2\xA0") == 0)
        return 2;
    return 0;
}

} // namespace

std::optional<int> ParseAmount(std::string_view text)
{
    size_t at = 0;
    while (at < text.size() && !Digit(text[at]))
        ++at;
    if (at == text.size())
        return std::nullopt;
    const bool negative = at > 0 && text[at - 1] == '-';
    long long value = 0;
    const auto take = [&](size_t count) {
        for (size_t end = at + count; at < end; ++at)
        {
            value = value * 10 + (text[at] - '0');
            if (value > INT_MAX)
                return false;
        }
        return true;
    };
    size_t run = at;
    while (run < text.size() && Digit(text[run]))
        ++run;
    if (!take(run - at))
        return std::nullopt;
    for (size_t gap; at < text.size() && (gap = Separator(text, at)) != 0;)
    {
        const size_t group = at + gap;
        if (group + 3 > text.size() || !Digit(text[group]) || !Digit(text[group + 1]) || !Digit(text[group + 2]) ||
            (group + 3 < text.size() && Digit(text[group + 3])))
            break;
        at = group;
        if (!take(3))
            return std::nullopt;
    }
    return static_cast<int>(negative ? -value : value);
}

} // namespace storm::ui
