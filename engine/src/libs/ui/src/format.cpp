#include "format.h"

#include "screens/screen.h"

#include <cstdio>

namespace storm::ui
{

std::string Fill(std::string text, std::span<const std::string> args)
{
    int number = 1;
    for (const std::string &arg : args)
    {
        const std::string mark = "{" + std::to_string(number++) + "}";
        for (size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + arg.size()))
            text.replace(at, mark.size(), arg);
    }
    return text;
}

// One decimal with the decimal mark of the language ("2,3").
std::string Decimal(const screens::Bindings &bindings, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f", value);
    std::string text = buffer;
    const size_t dot = text.find('.');
    if (dot != std::string::npos)
        text.replace(dot, 1, bindings.Text("launcher.decimal_mark"));
    return text;
}

std::string Decimal(const std::string &text, const std::string &language)
{
    if (language == "english")
        return text;
    std::string out = text;
    for (size_t i = 1; i + 1 < out.size(); ++i)
        if (out[i] == '.' && out[i - 1] >= '0' && out[i - 1] <= '9' && out[i + 1] >= '0' && out[i + 1] <= '9')
            out[i] = ',';
    return out;
}

// A size in GB from a tenth of a GB, else in MB; `scale` (when given) picks the unit, so
// that "done of total" share one.
std::string Size(const screens::Bindings &bindings, double bytes, double scale)
{
    if ((scale < 0 ? bytes : scale) >= kGiB / 10)
        return Fill(bindings.Text("launcher.gb"), {Decimal(bindings, bytes / kGiB)});
    return Fill(bindings.Text("launcher.mb"), {Decimal(bindings, bytes / kMiB)});
}

std::string OneLine(const std::string &text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    }
    const size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, out.find_last_not_of(' ') - first + 1);
}

} // namespace storm::ui
