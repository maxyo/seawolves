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
    // first. Real AndroidFacts can therefore export itemid although the adapter wrote itemId. Keep that same lookup
    // rule here: losing the identity makes a selected worn item look unrelated to its slot and hides its parchment.
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

Json Portrait(const std::string &face)
{
    return face.empty() ? Json(nullptr)
                        : Json{{"kind", "portrait"}, {"texture", "INTERFACES/PORTRAITS/128/face_" + face}};
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

std::vector<Person> People(const Admission &admitted, Bindings &bindings)
{
    std::vector<Person> people;
    for (const Widget *entry : admitted.All("ANDROID_ROSTER"))
    {
        const std::string character = Field(entry, "character");
        if (character.empty())
            continue; // a free post
        auto person = std::find_if(people.begin(), people.end(),
                                   [&](const Person &p) { return Field(p.fact, "character") == character; });
        if (person == people.end())
            person = people.insert(people.end(), Person{entry, {}, {}});
        person->slots.push_back(FieldAmount(bindings, entry, "slot"));
        person->roles.push_back(InlineNoun(Field(entry, "role"), admitted.model->language));
    }
    return people;
}

size_t SelectedPerson(const Admission &admitted, Bindings &bindings, const std::vector<Person> &people)
{
    const Widget *subject = admitted.One("ANDROID_SUBJECT");
    if (!subject)
        return 0; // the last model of a closing interface (CheckAdapter)
    const std::string character = Field(subject, "character");
    for (size_t i = 0; i < people.size(); ++i)
        if (Field(people[i].fact, "character") == character)
            return i;
    bindings.Error(admitted.model->screen + ": the game shows character " + character +
                   ", who is not in its character list");
    return 0;
}

std::string Roles(const std::vector<std::string> &roles)
{
    std::string text;
    for (const std::string &role : roles)
        text += (text.empty() ? "" : "\xC2\xA0· ") + role; // the dot stays with the role before it
    return Capitalized(text);
}

Json Subject(const Admission &admitted, Bindings &bindings, SubjectSwitch &subjects,
             std::function<void(const Widget &)> select)
{
    const std::vector<Person> people = People(admitted, bindings);
    std::vector<SubjectSwitch::Choice> choices;
    std::vector<int> slots;
    for (const Person &person : people)
    {
        choices.push_back({person.fact->label, Roles(person.roles), Portrait(Field(person.fact, "face")), "hero"});
        slots.push_back(person.slots.front());
    }
    const size_t shown = SelectedPerson(admitted, bindings, people);
    // The game's list has selected a slot whose person (a free post's: the captain) the game shows a game frame later:
    // this model is a step of the pick's answer, and the pick's prediction stays over it.
    std::vector<const Widget *> rows = admitted.All("CHARACTERS_SCROLL");
    size_t listed = 0;
    for (const Widget *row : rows)
        if (row->value == "1")
            for (size_t i = 0; i < people.size(); ++i)
                if (std::find(people[i].slots.begin(), people[i].slots.end(), row->index) != people[i].slots.end())
                    listed = i;
    if (listed != shown)
        bindings.Await();
    Json view =
        subjects.View(bindings, choices, shown, bindings.Text("hero.subjects"), [rows, slots, select](size_t index) {
            for (const Widget *row : rows)
                if (row->index == slots[index] && row->Offers("select"))
                    select(*row);
        });
    subjects.PredictChoice(bindings, choices, shown);
    return view;
}

std::string StockText(const std::string &raw)
{
    std::string out;
    for (size_t at = 0; at <= raw.size();)
    {
        const size_t end = std::min(raw.find('\n', at), raw.size());
        std::string line = raw.substr(at, end - at);
        std::erase(line, '\r');
        if (StripWhitespace(line) == "~")
            line.clear();
        out += (at ? "\n" : "") + line;
        at = end + 1;
    }
    return StripWhitespace(out);
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

std::string AfterColon(const std::string &text)
{
    const size_t colon = text.find(':');
    return StripWhitespace(colon == std::string::npos ? text : text.substr(colon + 1));
}

Json CrewQuality(const Admission &admitted, Bindings &bindings, const char *table, const char *morale_text,
                 const Widget *fact, const std::string &prefix)
{
    const auto value = [&](const char *field) {
        return std::clamp(FieldAmount(bindings, fact, (prefix + field).c_str()), 0, 100);
    };
    Json groups = Json::array();
    const std::pair<const char *, const char *> fields[] = {
        {"Sailors", "sailors"}, {"Cannoners", "cannoners"}, {"Soldiers", "soldiers"}};
    for (const Widget *row : admitted.All(table))
        for (const auto &[id, field] : fields)
            if (row->row_id == id)
                groups.push_back(Json{{"label", StripWhitespace(row->CellText(1))},
                                      {"value", value(field)},
                                      {"word", Lower(StripWhitespace(row->CellText(2)))}});
    return Json{{"groups", groups},
                {"moraleLabel", bindings.Text("ship.morale")},
                {"morale", value("morale")},
                {"moraleWord", Lower(AfterColon(OneLine(admitted.Label(morale_text))))}};
}

} // namespace storm::ui::screens::hero
