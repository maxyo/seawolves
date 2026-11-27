// Shared parts of the hero section screens (hero_character.cpp, hero_ship.cpp, …): reading
// the interface adapter's facts (android_hero.c, widgets ANDROID_<LIST>), game numbers and
// words, and the header's switch over the people of the game's character list.
#pragma once

#include "controls.h"

#include <functional>

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
// A description from the game's text files (AbilityDescribe, RPGDescribe): carriage returns dropped and a
// line holding only "~", the stock paragraph separator, an empty line; trimmed. Shown with white-space: pre-line.
std::string StockText(const std::string &raw);
// Our bare icon of a skill by its id ("Sailing" → "wheel"; pictures.ini ICONS_SPEC names its pictures
// "<id> skill icon"), "" for an id that has none.
const char *SkillIcon(const std::string &id);
// The text after "Caption:" of a game line ("Мораль команды: Повышенная" → "Повышенная").
std::string AfterColon(const std::string &text);
// A crew's quality as the ship screen draws it (a c-stat-row each): the experience of the three
// groups of the game's crew table `table` (SetCrewExpTable rows Sailors / Cannoners / Soldiers:
// caption, word) with the adapter's numbers 0…100 (fields <prefix>sailors, <prefix>cannoners,
// <prefix>soldiers of `fact`) and the morale (field <prefix>morale, its word after the colon of the
// game's `morale_text`). {groups: [{label, value, word}], moraleLabel, morale, moraleWord}. The ship
// screen (hero_ship.cpp) shows crews with it.
Json CrewQuality(const Admission &admitted, Bindings &bindings, const char *table, const char *morale_text,
                 const Widget *fact, const std::string &prefix = std::string());
// The row image of a game portrait by face, null without one.
Json Portrait(const std::string &face);
// The interface adapter's facts are part of these screens, but a closing interface may publish a
// last model without them (the script's subject is gone): a missing `fact` is an error only in
// the first model of an opening. `opening` keeps the opening already checked.
void CheckAdapter(const Admission &admitted, Bindings &bindings, const char *fact, uint64_t &opening);
// A person of the game's character list (CHARACTERS_SCROLL, the adapter's ANDROID_ROSTER): the captain, the
// officers, companions, passengers and prisoners, each once in the list's order, with the slots they fill (an
// officer may hold several posts) and those slots' roles.
struct Person
{
    const Widget *fact; // the person's first slot
    std::vector<int> slots;
    std::vector<std::string> roles;
};
std::vector<Person> People(const Admission &admitted, Bindings &bindings);
// The person the game shows (the adapter's ANDROID_SUBJECT, the stock scripts' xi_refCharacter), whose card and lists
// are drawn: a pick in the game's list selects its slot at once and shows the person a game frame later (ProcessFrame),
// so the header follows the person, not the slot. A free post shows the captain (character_all.c SetButtonsState).
size_t SelectedPerson(const Admission &admitted, Bindings &bindings, const std::vector<Person> &people);
// Roles on a line of their own, "Штурман · канонир": in sentence case (German nouns keep their capitals), a line that
// wraps ends in the dot, the next one starts with a role.
std::string Roles(const std::vector<std::string> &roles);
// The subject switch over People, with their list; `select` sends the selection of the person's first slot.
Json Subject(const Admission &admitted, Bindings &bindings, SubjectSwitch &subjects,
             std::function<void(const Widget &)> select);

} // namespace storm::ui::screens::hero
