// Shared parts of the hero section screens, see hero.h.
#include "hero.h"

#include "amount.h"
#include "../text_utf8.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace storm::ui::screens::hero
{

std::string Field(const Widget *fact, const char *key)
{
    if (!fact)
        return {};
    const auto it = fact->context.find(key);
    if (it != fact->context.end())
        return it->second;
    // Script attributes are case-insensitive and their shared string codec keeps whichever spelling was interned
    // first: real AndroidFacts can export a field in another case than the adapter wrote it.
    const std::string folded = Lower(key);
    for (const auto &[name, value] : fact->context)
        if (Lower(name) == folded)
            return value;
    return {};
}

int FieldAmount(Bindings &bindings, const Widget *fact, const char *key)
{
    const std::string text = Field(fact, key);
    return text.empty() ? 0 : Amount(bindings, text, key);
}

std::string Thousands(int value)
{
    std::string raw = std::to_string(std::abs(value)), out;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (i && (raw.size() - i) % 3 == 0)
            out += "\xC2\xA0";
        out += raw[i];
    }
    return (value < 0 ? "-" : "") + out;
}

void CheckAdapter(const Admission &admitted, Bindings &bindings, const char *fact, uint64_t &opening)
{
    if (opening == admitted.model->opening)
        return;
    opening = admitted.model->opening;
    if (!admitted.One(fact))
        bindings.Error(admitted.model->screen + ": no " + fact +
                       " from the interface adapter (android_hero.c not loaded?)");
}

const char *SkillIcon(const std::string &id)
{
    static const char *const icons[][2] = {
        {"Leadership", "leadership"}, {"FencingL", "rapier"},  {"FencingS", "sabre"},    {"FencingH", "axe"},
        {"Pistol", "pistol"},         {"Fortune", "fortune"},  {"Sneak", "sneak"},       {"Sailing", "wheel"},
        {"Accuracy", "accuracy"},     {"Cannons", "cannon"},   {"Grappling", "grapple"}, {"Defence", "shield"},
        {"Repair", "repair"},         {"Commerce", "commerce"}};
    for (const auto &pair : icons)
        if (id == pair[0])
            return pair[1];
    return "";
}

} // namespace storm::ui::screens::hero
