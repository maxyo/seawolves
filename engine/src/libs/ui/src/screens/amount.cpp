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
