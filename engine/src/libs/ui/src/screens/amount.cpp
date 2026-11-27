// The game's numbers in its captions, see amount.h.
#include "amount.h"

#include "screen.h"
#include "../text_utf8.h"

namespace storm::ui::screens
{

int Amount(Bindings &bindings, std::string_view text, const char *what, int none)
{
    if (const std::optional<int> value = ParseAmount(text))
        return *value;
    bindings.ErrorOnce(std::string("amount:") + what, std::string("no number in ") + what + " ('" + std::string(text) +
                                                          "'): shown as " + std::to_string(none));
    return none;
}

double Number(Bindings &bindings, std::string_view text, const char *what, double none)
{
    const std::string trimmed = StripWhitespace(std::string(text));
    if (const std::optional<double> value = ParseNumber<double>(trimmed))
        return *value;
    bindings.ErrorOnce(std::string("number:") + what, std::string("no number in ") + what + " ('" + std::string(text) +
                                                          "'): shown as " + ToText(Json(none)));
    return none;
}

int TableRow(Bindings &bindings, std::string_view row_id)
{
    if (row_id.starts_with("tr"))
        if (const std::optional<int> row = ParseNumber<int>(row_id.substr(2)); row && *row > 0)
            return *row - 1;
    bindings.ErrorOnce("row:" + std::string(row_id), "row id '" + std::string(row_id) + "' is not tr<n>");
    return -1;
}

namespace
{

template <class T> bool Read(Bindings &bindings, const Event &event, T &out)
{
    if (const std::optional<T> value = ParseNumber<T>(event.value))
    {
        out = *value;
        return true;
    }
    bindings.Error(event.id + " " + event.verb + ": '" + event.value + "' is not a number, nothing done");
    return false;
}

} // namespace

bool EventNumber(Bindings &bindings, const Event &event, int &out)
{
    return Read(bindings, event, out);
}

bool EventNumber(Bindings &bindings, const Event &event, size_t &out)
{
    return Read(bindings, event, out);
}

bool EventNumber(Bindings &bindings, const Event &event, double &out)
{
    return Read(bindings, event, out);
}

} // namespace storm::ui::screens
