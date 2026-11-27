// Self repair (interface "repair"): hull, masts and sails as amount
// rows, the ship selector in the header, the planks and sailcloth in stock and the repair
// button with its time and the materials the order takes. Fills the pack screen <s-repair>.
//
// The game moves an amount only by its arrows (hull and sails 1 % per press, masts all at
// once) and stops them where the materials end, so a new amount is sent as presses once it
// settles and the row then shows where the game stopped. The amounts are the game's captions
// ("61%", "2/3"); what the order has set and takes, the sails' maximum and the stock are the
// adapter's ANDROID_REPAIR (repair.c chr.repair).
#include "controls.h"

#include "amount.h"

#include <algorithm>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"REPAIR_QTY_H", "text", true},
    {"REPAIR_LEFT_H", "button", true},
    {"REPAIR_RIGHT_H", "button", true},
    {"REPAIR_OK", "button", true},
    {"REPAIR_QTY_M", "text"},
    {"REPAIR_QTY_S", "text"},
    {"ANDROID_REPAIR", "fact"}, // the adapter's order and stock
    {"REPAIR_LEFT_M", "button"},
    {"REPAIR_RIGHT_M", "button"},
    {"REPAIR_LEFT_S", "button"},
    {"REPAIR_RIGHT_S", "button"},
    {"REPAIR_STATUS_*", "text"},
    {"REPAIR_QTY_*", "text"},
    {"REPAIR_HULL_CAPTION", "text"},
    {"REPAIR_MAST_CAPTION", "text"},
    {"REPAIR_SAIL_CAPTION", "text"},
    {"REPAIR_WINDOW_CAPTION", "text|heading"},
    {"REPAIR_SHIP_NAME", "text"},
    {"REPAIR_ALL_SHIP", "button"},
    {"REPAIR_ALL_SQUADRON", "button"},
    {"REPAIR_CANCEL", "button"},
    {"EXIT_BTN", "button"},
    {"SHIPS_SCROLL", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"NAME_STR", "text"},
    {"LOCATION_STR", "text"},
    {"TIME_STR", "text"},
    {"REPAIR_*", "image"},
    {"MAIN_CHARACTER_*", "image"},
    {"OTHER_*", "image"},
};

// Every whole number of a text, in order: "4/4 … 3/4" → {4, 4, 3, 4}.
std::vector<int> Numbers(const std::string &text)
{
    std::vector<int> out;
    for (size_t i = 0; i < text.size();)
    {
        if (text[i] >= '0' && text[i] <= '9')
        {
            const size_t end = text.find_first_not_of("0123456789", i);
            const std::optional<int> number = ParseNumber<int>(std::string_view(text).substr(i, end - i));
            if (!number)
                return {}; // more digits than an int holds: no figures
            out.push_back(*number);
            i = end == std::string::npos ? text.size() : end;
        }
        else
            ++i;
    }
    return out;
}
int At(const std::vector<int> &numbers, size_t index)
{
    return index < numbers.size() ? numbers[index] : -1;
}

// One part of the ship as the game describes it.
struct Part
{
    char kind;      // H hull, M masts, S sails: suffix of the game's nodes
    const char *id; // element id in the view
    const char *icon;
    bool masts;          // counted in masts ("2/3"), else in per cent
    const char *set;     // the adapter's field: the amount the order has set
    const char *stock;   // the adapter's field: the material it takes in the squadron's holds
    const char *missing; // the word when there is none
};
constexpr Part kParts[] = {
    {'H', "hull", "hull", false, "hull", "planks_stock", StringKey("repair.no_planks")},
    {'M', "masts", "sail-furled", true, "mast", "planks_stock", StringKey("repair.no_planks")},
    {'S', "sails", "sail-full", false, "sail", "sailcloth_stock", StringKey("repair.no_sailcloth")},
};
constexpr const char *kCaption[] = {"REPAIR_HULL_CAPTION", "REPAIR_MAST_CAPTION", "REPAIR_SAIL_CAPTION"};

std::string Field(const Widget &facts, const char *key)
{
    const auto found = facts.context.find(key);
    return found == facts.context.end() ? std::string() : found->second;
}

class RepairScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-repair";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        Json view = Json::object();
        view["title"] = bindings.Text("repair.title");

        // repair.c keeps the order of the ship shown in chr.repair; the adapter hands it over.
        const Widget *facts = admitted.One("ANDROID_REPAIR");
        if (!facts)
            bindings.ErrorOnce("facts", "repair: no ANDROID_REPAIR from the interface adapter: the order is unknown");
        const auto fact = [&bindings, facts](const char *key) {
            return facts ? Amount(bindings, Field(*facts, key), key) : 0;
        };
        Json parts = Json::array();
        if (facts)
            for (size_t p = 0; p < std::size(kParts); ++p)
                if (Json row = PartView(admitted, bindings, p, fact); !row.is_null())
                    parts.push_back(std::move(row));
        view["parts"] = parts;

        Json stock = Json::array();
        if (facts)
        {
            stock.push_back(Json{{"icon", "planks"},
                                 {"label", bindings.Text("repair.planks")},
                                 {"value", std::to_string(fact("planks_stock"))}});
            stock.push_back(Json{{"icon", "sailcloth"},
                                 {"label", bindings.Text("repair.sailcloth")},
                                 {"value", std::to_string(fact("sailcloth_stock"))}});
        }
        view["stock"] = stock;

        // The ship being repaired.
        const std::vector<const Widget *> ships = admitted.All("SHIPS_SCROLL");
        view["subject"] = Json();
        if (ships.size() > 1)
            view["subject"] = Subject(bindings, ships);
        Json actions = Json::array();
        if (const Widget *all = admitted.One("REPAIR_ALL_SQUADRON"))
        {
            Json button = ButtonView(bindings, *all, "all-ships", "quiet");
            button["label"] = bindings.Text("repair.all_ships");
            actions.push_back(button);
        }
        view["actions"] = actions;

        const Widget *ok = admitted.One("REPAIR_OK");
        const std::vector<int> hours = Numbers(admitted.Label("TIME_STR"));
        std::string sub;
        if (At(hours, 0) > 0)
            sub = std::to_string(At(hours, 0)) + " " + bindings.Text("save.hours");
        // What the order takes (repair.c RepairMatherialShow: chr.repair.preq, sreq).
        for (const auto &[key, word] :
             {std::pair{"planks", StringKey("repair.planks")}, std::pair{"sailcloth", StringKey("repair.sailcloth")}})
            if (const int takes = facts ? fact(key) : 0; takes > 0)
                sub += (sub.empty() ? "" : " · ") + bindings.Text(word) + " " + std::to_string(takes);
        view["primary"] = Json{{"label", bindings.Text("repair.do")},
                               {"sub", sub},
                               {"disabled", !ok->Offers("press") || At(hours, 0) <= 0},
                               {"reason", ok->unavailable_reason},
                               {"busy", bindings.Pending("primary")}};
        bindings.Forward("primary", *ok, "press");

        const Widget *cancel = admitted.One("REPAIR_CANCEL");
        const Widget *exit = cancel ? cancel : admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // One part (hull, sails, masts): its slider; null when the game's caption cannot be read. A settled new amount
    // goes out as the game's presses.
    template <class Fact> Json PartView(const Admission &admitted, Bindings &bindings, size_t p, const Fact &fact)
    {
        const Part &part = kParts[p];
        const std::string suffix(1, part.kind);
        const Widget *less = admitted.One("REPAIR_LEFT_" + suffix);
        const Widget *more = admitted.One("REPAIR_RIGHT_" + suffix);
        const std::vector<int> amount = Numbers(admitted.Label("REPAIR_QTY_" + suffix)); // "61%" | "2/3"
        if (amount.empty() || (part.masts && amount.size() < 2))
        {
            if (part.kind == 'H')
                bindings.Error("repair: unreadable hull caption");
            return Json();
        }
        // The masts' whole is the caption's; the sails' the game's maximum (repair.c SetSailMaximum).
        const int full = part.masts ? At(amount, 1) : part.kind == 'S' ? fact("sailmax") : 100;
        const int shown = At(amount, 0);
        const int now = shown - fact(part.set); // the part's state: what the caption shows less what is set
        State &state = state_[p];
        const bool recent = state.target >= 0 && (state.send >= 0 || bindings.Now() - state.send_at < 1.0);
        if (recent && state.send < 0)
            bindings.RebuildAfter(state.send_at + 1.0 - bindings.Now());
        const int value = recent ? state.target : shown;
        const bool none = now < full && fact(part.stock) == 0;
        const bool idle = now >= full;
        const std::string unit = part.masts ? std::string() : "%";
        // The whole 0…full with the knob at the part's state: it cannot go below it; the game stops the arrows where
        // the materials end.
        Json q{{"value", value},
               {"min", 0},
               {"max", full},
               {"floor", now},
               {"unit", unit},
               {"summary", part.masts ? bindings.Text("common.of") + " " + std::to_string(full) : std::string("-")},
               {"steps", Json::array({1, 10})},
               {"all", true},
               {"allLabel", bindings.Text("common.all")},
               {"disabled", idle || !less || !more}};
        Json row{{"id", part.id},
                 {"icon", part.icon},
                 {"label", admitted.Label(kCaption[p])},
                 {"now", bindings.Text("repair.now") + " " + std::to_string(now) +
                             (part.masts ? " " + bindings.Text("common.of") + " " + std::to_string(full) : "%")},
                 {"q", q},
                 {"need", none ? bindings.Text(part.missing) : std::string()},
                 {"short", none},
                 {"disabled", idle}};

        // A settled new amount: the presses that take the game's amount there. Masts move
        // all at once: one press either way.
        if (state.send >= 0 && bindings.Now() >= state.send_at && less && more)
        {
            const int wanted = state.send;
            // While the game still shows the number earlier presses started from, they
            // are on their way: count from where they lead.
            const int from = shown == state.base && state.expected >= 0 ? state.expected : shown;
            state.send = -1;
            state.base = shown;
            state.expected = wanted;
            const int presses = part.masts ? (wanted != from ? 1 : 0) : std::abs(wanted - from);
            for (int i = 0; i < presses; ++i)
                bindings.Send(wanted > from ? *more : *less, "press");
        }
        else if (state.send >= 0)
            bindings.RebuildAfter(state.send_at - bindings.Now());
        const auto set = [this, &bindings, p, low = now, high = std::max(now, full)](const Event &event) {
            int wanted = 0;
            if (!EventNumber(bindings, event, wanted))
                return;
            State &state = state_[p];
            state.target = state.send = std::clamp(wanted, low, high);
            state.send_at = bindings.Now() + (event.verb == "commit" ? 0.0 : 0.4);
            bindings.Rebuild();
        };
        bindings.On(part.id, "change", set);
        bindings.On(part.id, "commit", set);
        return row;
    }

    // The ship being repaired, with arrows to the squadron's other ships.
    static Json Subject(Bindings &bindings, const std::vector<const Widget *> &ships)
    {
        size_t current = 0;
        for (size_t i = 0; i < ships.size(); ++i)
            if (ships[i]->value == "1")
                current = i;
        Json subject{{"label", ships[current]->label},
                     {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " +
                                 std::to_string(ships.size())},
                     {"prev", current > 0},
                     {"next", current + 1 < ships.size()},
                     {"list", false}};
        const auto choose = [&bindings, ships](size_t index) {
            if (index < ships.size())
                TapChoice(bindings, *ships[index]);
        };
        bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
        bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
        bindings.Ignore("header", "subject-list");
        return subject;
    }

    // The amount the player set last per part: shown until the game's own number catches up.
    struct State
    {
        int target = -1; // shown for a second after sending, until the game's number catches up
        int send = -1;   // waiting to be sent when the drag or the steps settle
        double send_at = 0.0;
        int base = -1, expected = -1; // presses already sent from the game's `base` towards `expected`
    };
    State state_[3];
};

} // namespace

std::unique_ptr<Screen> CreateRepairScreen()
{
    return std::make_unique<RepairScreen>();
}

} // namespace storm::ui::screens
