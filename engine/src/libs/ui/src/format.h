// The texts the engine's screens and the touch interface's natives write: a game text on one line, a string table
// entry filled, a decimal and a size in the language.
#pragma once

#include <initializer_list>
#include <span>
#include <string>

namespace storm::ui
{

namespace screens
{
class Bindings;
}

inline constexpr double kMiB = 1048576.0, kGiB = 1073741824.0;

// A game text on one line: line breaks ("\r", "\n"), tabs and runs of spaces become one space, trimmed.
std::string OneLine(const std::string &text);
// "{1}", "{2}" … of a string table entry replaced by the arguments.
std::string Fill(std::string text, std::span<const std::string> args);
inline std::string Fill(std::string text, std::initializer_list<std::string> args)
{
    return Fill(std::move(text), std::span<const std::string>(args.begin(), args.size()));
}
// One decimal with the decimal mark of the language ("2,3").
std::string Decimal(const screens::Bindings &bindings, double value);
// A game number with a decimal point in the interface's language ("2.4" → "2,4" except in
// English): every point between two digits.
std::string Decimal(const std::string &text, const std::string &language);
// A size in GB from a tenth of a GB, else in MB; `scale` (when given) picks the unit, so that "done of total" share
// one.
std::string Size(const screens::Bindings &bindings, double bytes, double scale = -1);

} // namespace storm::ui
