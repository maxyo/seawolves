// A number written alone ("12", "-3", "0.25"): std::from_chars over the whole text, nullopt for anything else ("",
// "3a", " 3", "+3", an integer out of range, a decimal that is not finite). The caller reports a nullopt where it
// meets it (an error line, the action not done): atoi and atof read such a text as 0, which is a real row.
#pragma once

#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace storm::ui
{

template <class T> std::optional<T> ParseNumber(std::string_view text)
{
    T value{};
    const char *end = text.data() + text.size();
    const auto [at, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || at != end)
        return std::nullopt;
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(value))
            return std::nullopt;
    return value;
}

// The first whole number of a game text. Its digits may be grouped by thousands with a space or a no-break space
// ("23 824", "11 575" → 23824; a group is exactly three digits, so "5 10" → 5); a minus sign right before
// the first digit makes it negative ("-3"); it ends at any other character ("4.6 / 140" → 4, "40 : 9 / 40" → 40,
// "100%" → 100). nullopt when the text holds no digit ("", "-", "—") or the number does not fit an int.
std::optional<int> ParseAmount(std::string_view text);

} // namespace storm::ui
