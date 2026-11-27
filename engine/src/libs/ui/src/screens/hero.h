// Shared parts of the hero section screens still in C++: reading the interface adapter's facts
// (android_hero.c, widgets ANDROID_<LIST>), game numbers and words.
#pragma once

#include "controls.h"

namespace storm::ui::screens
{
// An intent whose step got no answer from the engine in this many seconds is dropped.
inline constexpr double kStepTimeout = 3.0;
} // namespace storm::ui::screens

namespace storm::ui::screens::hero
{

// A field of an adapter fact row ("" when absent). Script attribute names are case-insensitive and keep
// whichever spelling was interned first (rateSelf may arrive as rateself), so the lookup ignores case.
std::string Field(const Widget *fact, const char *key);
// A whole-number field of an adapter fact (ParseAmount): 0 when the fact omits it (the adapter leaves out the fields
// that do not apply, such as the captain's salary), an error line (Amount) when it holds no number.
int FieldAmount(Bindings &bindings, const Widget *fact, const char *key);
// 41440 → "41 440" (no-break space).
std::string Thousands(int value);
// Our bare icon of a skill by its id ("Sailing" → "wheel"; pictures.ini ICONS_SPEC names its pictures
// "<id> skill icon"), "" for an id that has none.
const char *SkillIcon(const std::string &id);
// The interface adapter's facts are part of these screens, but a closing interface may publish a
// last model without them (the script's subject is gone): a missing `fact` is an error only in
// the first model of an opening. `opening` keeps the opening already checked.
void CheckAdapter(const Admission &admitted, Bindings &bindings, const char *fact, uint64_t &opening);

} // namespace storm::ui::screens::hero
