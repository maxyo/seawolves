// Crew hire (interface "hirecrew"): the trade layout with one
// row, the crew, whose quantity is always open; under it the experience and morale of the
// ship's crew and of the men in the tavern, drawn as the ship screen draws a crew (bars with the
// game's words, hero::CrewQuality; the numbers are the adapter's ANDROID_CREWHIRE). Fills the
// pack screen <s-crew>.
//
// The amount goes to the game as it is set: hirecrew.c ChangeQTY_EDIT clamps it (the tavern's men, the ship's room
// for crew, the purse, the hold) and puts it on both crews at once, keeping the crew it started from (the adapter's
// `base`). The screen shows the game's answer: the crews, the amount (crew less base) and its cost (QTY_Result).
// «Нанять / Уволить N» is the game's OK.
#include "amount.h"
#include "controls.h"
#include "hero.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"SHIPS_SCROLL", "choice", true},
    {"QTY_EDIT", "edit", true},
    {"ANDROID_CREWHIRE", "fact"}, // the adapter's crew before the amount set, the purse, the crews' quality
    {"CREW_QTY", "text", true},
    {"CREW_QTY2", "text"},
    {"EXIT_BTN", "button"},
    {"TITLE", "heading|text"},
    {"STORECAPTION1", "text"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OUR_GOLD", "text"},
    {"CAPACITY", "text"},
    {"STORE_CAPACITY", "text"},
    {"FOOD_SHIP", "text"},
    {"MONEY_SHIP", "text"},
    {"INFO_SHIP", "text"},
    {"TAVERN_PRICE", "text"},
    {"CREW_MORALE_TEXT", "text"},
    {"CREW_MORALE_TEXT2", "text"},
    {"TABLE_CREW", "choice|text"},
    {"TABLE_CREW2", "choice|text"},
    {"QTY_OK_BUTTON", "button"},
    {"QTY_CANCEL_BUTTON", "button"},
    {"QTY_ADD_BUTTON", "button"},
    {"QTY_REMOVE_BUTTON", "button"},
    {"QTY_ADD_ALL_BUTTON", "button"},
    {"QTY_REMOVE_ALL_BUTTON", "button"},
    {"QTY_*", "text"},
    {"CREW_PICTURE*", "image"},
    {"CREW_MORALE_PIC*", "image"},
    {"MAIN_CHARACTER_PICTURE", "image"},
    {"OTHER_PICTURE", "image"},
};

class CrewScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-crew";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget *edit = admitted.One("QTY_EDIT");
        const Widget *ok = admitted.One("QTY_OK_BUTTON");
        const std::vector<const Widget *> ships = admitted.All("SHIPS_SCROLL");
        size_t current = 0;
        for (size_t i = 0; i < ships.size(); ++i)
            if (ships[i]->value == "1")
                current = i;
        const std::string owner = ships[current]->row_id; // the ship every quantity action is bound to

        // The crews as the game has them, with the amount it has set on them.
        const int crew = Amount(bindings, admitted.Label("CREW_QTY"), "CREW_QTY");
        const int tavern = Amount(bindings, admitted.Label("CREW_QTY2"), "CREW_QTY2");
        const int price = Amount(bindings, admitted.Label("TAVERN_PRICE"), "TAVERN_PRICE");
        const Widget *facts = admitted.One("ANDROID_CREWHIRE");
        if (!facts)
            bindings.ErrorOnce("facts", "hirecrew: no ANDROID_CREWHIRE from the interface adapter: the crew before "
                                        "the amount set and the purse are unknown");
        const auto fact = [&bindings, facts](const char *key) -> std::optional<int> {
            if (!facts)
                return std::nullopt;
            const auto found = facts->context.find(key);
            if (found == facts->context.end())
                return std::nullopt;
            return Amount(bindings, found->second, key);
        };
        const int base = fact("base").value_or(crew);
        const int amount = crew - base; // > 0 hire, < 0 fire
        if (owner != deal_owner_)
        {
            deal_owner_ = owner;
            asked_ = -1;
        }
        // The crew the player asked for is shown until the game has answered it.
        const bool asking = asked_ >= 0 && bindings.AnsweredSince(asked_floor_) == Bindings::Answers::Waiting;
        const int shown = asking ? asked_ : crew;
        const int high = crew + tavern; // every man of the ship and of the tavern
        // hirecrew.c ChangeQTY_EDIT writes "Стоимость найма N" while a hire is set.
        const int cost = amount > 0 ? ParseAmount(admitted.Label("QTY_Result")).value_or(0) : 0;

        Json row{{"id", "0"},
                 {"name", bindings.Text("crew.sailors")},
                 {"image", {{"kind", "icon"}, {"icon", "crew"}}},
                 {"meta", OneLine(admitted.Label("INFO_SHIP"))},
                 {"cells", Json::array({std::to_string(crew), std::to_string(tavern),
                                        Json{{"text", std::to_string(price)}, {"bold", true}}})},
                 {"selected", true},
                 // One slider for both directions, as the store: the crew the window started from is the
                 // origin, firing to its left, hiring to its right; the cost is on the button.
                 {"q", Json{{"value", shown},
                            {"min", 0},
                            {"max", high},
                            {"zones", true},
                            {"origin", base},
                            {"lessLabel", bindings.Text("crew.fire")},
                            {"moreLabel", bindings.Text("crew.hire")},
                            {"label", bindings.Text("crew.on_ship")},
                            {"allLabel", bindings.Text("common.all")},
                            {"summary", "-"},
                            {"steps", Json::array({1, 10})},
                            {"disabled", !edit->Offers("edit")}}}};
        const auto set = [this, &bindings, edit, owner, base, high](const Event &event) {
            int wanted = 0;
            if (!EventNumber(bindings, event, wanted))
                return;
            asked_ = std::clamp(wanted, 0, high);
            asked_floor_ = bindings.RequestFloor();
            // "0" first: after a fire the game keeps firing on any amount until its window is reset.
            bindings.SendLatest(*edit, "edit", std::to_string(asked_ - base), owner, std::string("0"));
        };
        bindings.On("crew", "change", set);
        bindings.On("crew", "commit", set);
        bindings.Ignore("crew", "row");

        Json view = Json::object();
        const std::string caption = OneLine(admitted.Label("STORECAPTION1"));
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE")
                        : caption.empty()                ? bindings.Text("crew.title")
                                                         : caption;
        view["subject"] = Json();
        if (ships.size() > 1)
            view["subject"] = Subject(bindings, ships, current);
        view["stats"] = Stats(base, crew, fact("money"));
        view["list"] = Json{{"label", ""},
                            {"cols", Json::array({Json{{"label", bindings.Text("crew.on_ship")}, {"icon", "ship"}},
                                                  Json{{"label", bindings.Text("crew.in_tavern")}, {"icon", "tavern"}},
                                                  Json{{"label", bindings.Text("trade.price")}, {"icon", "money"}}})},
                            {"rows", Json::array({row})}};
        view["shipLabel"] = bindings.Text("crew.on_ship");
        view["tavernLabel"] = bindings.Text("crew.in_tavern");
        // The adapter's crew numbers (android_hero.c AndroidCrewHire): the ship's as the ship screen's, the tavern's
        // prefixed "town_".
        if (facts && facts->context.find("morale") == facts->context.end())
            bindings.ErrorOnce("quality", "hirecrew: ANDROID_CREWHIRE has no crew quality (morale, experience)");
        view["ship"] = hero::CrewQuality(admitted, bindings, "TABLE_CREW", "CREW_MORALE_TEXT", facts);
        view["tavern"] = hero::CrewQuality(admitted, bindings, "TABLE_CREW2", "CREW_MORALE_TEXT2", facts, "town_");
        std::string notes;
        for (const char *node : {"MONEY_SHIP", "FOOD_SHIP"})
            if (!admitted.Label(node).empty())
                notes += (notes.empty() ? "" : " · ") + OneLine(admitted.Label(node));
        view["notes"] = notes;

        view["primary"] = Json();
        if (ok && amount != 0)
        {
            view["primary"] =
                Json{{"label", bindings.Text(amount > 0 ? StringKey("crew.hire") : StringKey("crew.fire")) + " " +
                                   std::to_string(std::abs(amount))},
                     {"sub", cost > 0 ? hero::Thousands(cost) : std::string()},
                     {"disabled", !ok->Offers("press") || !edit->Offers("edit")},
                     {"reason", ok->unavailable_reason},
                     {"busy", bindings.Pending("primary")}};
            // Queued: an amount still on its way goes first.
            bindings.On("primary", "",
                        [&bindings, ok, owner](const Event &) { bindings.SendAfter(0.0, *ok, "press", {}, owner); });
        }
        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [&bindings, exit](const Event &) { bindings.Back(exit); });
        return view;
    }

  private:
    // The ship the crew is for, with arrows to the fleet's other ships.
    static Json Subject(Bindings &bindings, const std::vector<const Widget *> &ships, size_t current)
    {
        Json subject{{"label", ships[current]->label},
                     {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " +
                                 std::to_string(ships.size())},
                     {"prev", current > 0},
                     {"next", current + 1 < ships.size()},
                     {"list", false}};
        const auto choose = [&bindings, ships](size_t index) {
            if (index < ships.size() && ships[index]->Offers("select"))
                bindings.Send(*ships[index], "select", ships[index]->row_id);
        };
        bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
        bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
        bindings.Ignore("header", "subject-list");
        return subject;
    }

    // The crew the window started from, with the one the game has set; the purse (the adapter's: hirecrew.c charges
    // it on OK).
    static Json Stats(int base, int crew, std::optional<int> money)
    {
        Json stats = Json::array();
        Json men{{"icon", "crew"}, {"value", std::to_string(base)}};
        if (crew != base)
            men["preview"] = std::to_string(crew);
        stats.push_back(std::move(men));
        if (money)
            stats.push_back(Json{{"icon", "money"}, {"value", hero::Thousands(*money)}, {"tone", "gold"}});
        return stats;
    }

    std::string deal_owner_; // the ship of the crew asked for
    int asked_ = -1;         // the crew the player asked for; -1: none
    uint64_t asked_floor_ = 0;
};

} // namespace

std::unique_ptr<Screen> CreateCrewScreen()
{
    return std::make_unique<CrewScreen>();
}

} // namespace storm::ui::screens
