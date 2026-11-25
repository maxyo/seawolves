// Shipyard (interface "shipyard"): one list on the left, «our ships» (every fleet slot with its
// trade-in value, one free place) over the yard's offers; on the right the ship being looked at:
// its figures with icons and full captions, an offer's figures beside those of the chosen ship of
// ours (better green, worse red), and the deal at the card's bottom. Buying and selling are held
// buttons that carry the price; the game's own confirmation is answered for a held deal and shown
// as a sheet only when the game opens it unasked. The game's other windows (repair, captain,
// officer, cannons, help) are sheets. Fills <s-shipyard>.
//
// The engine describes the shipyard with special widgets (xinterface/mobile_shipyard.inc):
// `shipyard_state` (index 1 = protocol version) carries the window mode and the selected
// owner in its context, `shipyard_catalog` the catalogue captions, `shipyard_control` the
// buttons that are not plain widgets (repair steps, help, close). Every action a widget
// offers is a capability token "yard@<opening>:<generation>:<verb>" that is valid for that
// snapshot only: the screen sends the token itself, at most one per model revision, and
// nothing while the engine says the state is not ready. No money, eligibility or repair
// rule lives here; the game decides and answers with the next model. The repair window's
// sliders are kept within the purse by the game's own prices (the adapter's ANDROID_YARDREPAIR,
// GetHullRepairCost / GetSailRepairCost of each amount).
#include "amount.h"
#include "controls.h"
#include "hero.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"ANDROID_SHIPYARD_STATE", "shipyard_state", true},
    {"ANDROID_SHIPYARD_CATALOG", "shipyard_catalog", true},
    {"ANDROID_SHIPYARD_CLOSE", "shipyard_control"},
    {"SHIPS_SCROLL", "choice|shipyard_control", true},
    {"BUTTON_BUY", "button", true},
    {"BUTTON_SELL", "button", true},
    {"BUTTON_REPAIR", "button", true},
    {"EXIT_BTN", "shipyard_control|button"},
    {"TITLE", "heading|text"},
    {"MAIN_BACK", "image"},
    {"SHIP_BIG_PICTURE", "image|shipyard_control"},
    {"MAIN_CHARACTER_PICTURE", "image|shipyard_control"},
    {"TABLE_SHIPYARD", "choice|shipyard_control"},
    {"TABLE_OTHER", "choice"},
    {"HERO_NAME", "text"},
    {"HERO_RANK", "text"},
    {"SHIP_RANK", "text"},
    {"EDIT_BOX", "edit"},
    {"FRAME_INFO_CAPTION", "text|heading"},
    {"INFO_TEXT", "text"},
    {"MONEY_TEXT", "text"},
    {"MONEY_PIC", "image"},
    {"MSG_WINDOW_CAPTION", "text|heading"},
    {"MSG_WINDOW_TEXT", "text"},
    {"MSG_OK", "button"},
    {"MSG_CANCEL", "button"},
    {"MSG_EXIT_BTN", "button"},
    {"REPAIR_WINDOW_CAPTION", "text|heading"},
    {"REPAIR_WINDOW_TEXT", "text"},
    {"REPAIR_QTY_H", "text"},
    {"REPAIR_QTY_S", "text"},
    {"REPAIR_LEFT_H", "shipyard_control|button"},
    {"REPAIR_RIGHT_H", "shipyard_control|button"},
    {"REPAIR_LEFT_S", "shipyard_control|button"},
    {"REPAIR_RIGHT_S", "shipyard_control|button"},
    {"REPAIR_OK", "button"},
    {"REPAIR_ALL", "button"},
    {"REPAIR_CANCEL", "button"},
    {"REPAIR_EXIT_BTN", "button"},
    {"REPAIR_*", "image"},
    {"ANDROID_YARDREPAIR", "fact"}, // the adapter's repair prices
    {"OFFICERS_WINDOW_CAPTION", "text|heading"},
    {"PASSENGERSLIST", "choice"},
    {"OFFICER_NAME", "text"},
    {"TABLE_SMALLSKILL", "choice|text"},
    {"TABLE_SMALLOTHER", "choice|text"},
    {"ACCEPT_ADD_OFFICER", "button"},
    {"CANCEL_ADD_OFFICER", "button"},
    {"OFFICER_EXIT_BTN", "button"},
    {"RPG_OFFICERS_WINDOW_CAPTION", "text|heading"},
    {"RPG_OFFICER_NAME", "text"},
    {"RPG_TABLE_SMALLSKILL", "choice|text"},
    {"RPG_TABLE_SMALLOTHER", "choice|text"},
    {"CANNONS_CAPTION", "text|heading"},
    {"CANNONS_TEXT", "text"},
    {"CANNONS_QTY_F", "text"},
    {"CANNONS_QTY_B", "text"},
    {"CANNONS_QTY_L", "text"},
    {"CANNONS_QTY_R", "text"},
    {"CANNONS_CANCEL", "button"},
    {"CANNONS_EXIT_BTN", "button"},
    {"CANNONS_SHIP", "image"},
    {"CANNONS_PIC", "image"},
    {"TOOLTIP_*", "text|heading|image"},
};

// The capability token of `verb` that the widget offers in this snapshot, or empty.
std::string Token(const Widget *widget, const std::string &verb)
{
    if (!widget || !widget->enabled)
        return {};
    for (const std::string &action : widget->actions)
        if (action.rfind("yard@", 0) == 0 && action.size() > verb.size() &&
            action.compare(action.size() - verb.size() - 1, std::string::npos, ":" + verb) == 0)
            return action;
    return {};
}

std::string Context(const Widget *widget, const std::string &key)
{
    if (!widget)
        return {};
    const auto found = widget->context.find(key);
    return found == widget->context.end() ? std::string() : found->second;
}

// The game writes line breaks in table cells as the two characters "\n".
std::string Lines(std::string text)
{
    for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at))
        text.replace(at, 2, "\n");
    std::replace(text.begin(), text.end(), '\r', '\n');
    return text;
}

// "23824" → "23 824"; any other text stays as it is.
std::string Grouped(const std::string &text)
{
    const size_t sign = !text.empty() && text[0] == '-' ? 1 : 0;
    if (text.size() <= sign + 3 || text.find_first_not_of("0123456789", sign) != std::string::npos)
        return text;
    std::string out = text.substr(0, sign);
    for (size_t i = sign; i < text.size(); ++i)
    {
        if (i > sign && (text.size() - i) % 3 == 0)
            out += ' ';
        out += text[i];
    }
    return out;
}

// "11 575" never breaks inside the number.
std::string KeepTogether(std::string text)
{
    for (size_t at = text.find(' '); at != std::string::npos; at = text.find(' ', at))
        text.replace(at, 1, "\u00a0");
    return text;
}

// Our ship's class as the offers write theirs ("класс 5"): the game names it "Класс 5" in the fleet list.
std::string ClassLine(const Bindings &bindings, const std::string &text)
{
    const size_t digit = text.find_first_of("0123456789");
    return digit == std::string::npos ? text : bindings.Text("yard.class") + " " + text.substr(digit);
}

// "Тартана\n\nПальмье" → {type, proper name}.
std::pair<std::string, std::string> TypeAndName(const std::string &cell)
{
    const std::string text = Lines(cell);
    const size_t gap = text.find('\n');
    if (gap == std::string::npos)
        return {StripWhitespace(text), {}};
    return {StripWhitespace(text.substr(0, gap)), StripWhitespace(text.substr(gap))};
}

class ShipyardScreen final : public Screen
{
  public:
    const char *Component() const override
    {
        return "s-shipyard";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Yard yard = YardOf(admitted, bindings);
        const Sender send{this, &bindings, yard.ready, yard.revision};
        const Offered offers{yard.ready};

        Json view = Json::object();
        const std::string title = StripWhitespace(admitted.Label("TITLE"));
        view["title"] = title.empty() ? bindings.Text("yard.title") : title;
        view["subject"] = Json(); // the fleet is in the list
        Json stats = Json::array();
        if (const std::string money = StripWhitespace(admitted.Label("MONEY_TEXT")); !money.empty())
            stats.push_back(Json{{"icon", "money"}, {"value", money}, {"tone", "gold"}});
        view["stats"] = stats;
        view["fleet"] = Fleet(bindings, yard, send, offers);
        const Widget *chosen = nullptr;
        view["offers"] = OfferRows(admitted, bindings, yard, send, offers, chosen);
        view["noOffers"] = view["offers"].empty() ? bindings.Text("yard.no_ships") : std::string();
        // The ship being looked at.
        view["detail"] = Json();
        view["empty"] = yard.empty_slot ? bindings.Text("yard.empty_slot") : Context(yard.owned, "shipName");
        if (yard.yard_preview || yard.has_owned_ship)
            view["detail"] = Detail(admitted, bindings, yard, send, offers, chosen);

        // The game's windows.
        Close close;
        view["sheet"] = Json();
        if (yard.mode != "confirm" && bindings.Now() - held_at_ > kHeldDeal)
            held_deal_.clear();
        if (yard.mode == "confirm" && !held_deal_.empty() && held_deal_ == Context(yard.state, "messageMode") &&
            bindings.Now() - held_at_ <= kHeldDeal)
        {
            // The deal was held on its button: the game's question is answered, not asked again.
            const Widget *ok = Typed(admitted, "MSG_OK", "button");
            if (send(ok, "press"))
                held_deal_.clear();
            else
                bindings.RebuildAfter(0.1);
        }
        else if (yard.mode == "confirm")
            view["sheet"] = ConfirmSheet(admitted, bindings, yard, send, offers, close);
        else if (yard.mode == "repair")
            view["sheet"] = RepairSheet(admitted, bindings, yard, send, offers, close);
        else if (yard.mode == "captain" || yard.mode == "rpg")
            view["sheet"] = CaptainSheet(admitted, bindings, yard, send, offers, close);
        else if (yard.mode == "cannons")
            view["sheet"] = CannonsSheet(admitted, bindings, close);
        else if (yard.mode == "help")
            view["sheet"] = HelpSheet(admitted, bindings, close);
        if (!view["sheet"].is_null())
        {
            const auto dismiss = [send, close](const Event &) { send(close.widget, close.verb); };
            bindings.On("sheet-close", "", dismiss);
            bindings.On("sheet-cancel", "", dismiss);
            bindings.Modal("sheet", dismiss);
        }

        last_mode_ = yard.mode;
        // ← and Android back close an open window first, then the shipyard.
        const Widget *exit = Control(admitted, "EXIT_BTN", -1);
        bindings.On("header-back", "", [send, close, exit, main = yard.main](const Event &) {
            if (!main)
                send(close.widget, close.verb);
            else
                send(exit, "press");
        });
        return view;
    }

  private:
    // The repair window's prices (android_hero.c AndroidYardRepair): the purse and what n % more of the hull or
    // the sails cost at this yard, cost[0] = 0 … cost[100 - now]. Amounts are the window's percentages (now … 100).
    struct YardPart
    {
        int now = 0;
        std::vector<int> cost;
        int Of(int percent) const
        {
            const int n = std::clamp(percent - now, 0, int(cost.size()) - 1);
            return cost.empty() ? 0 : cost[n];
        }
        // The highest percentage `budget` pays for.
        int Most(int budget) const
        {
            int n = 0;
            while (n + 1 < int(cost.size()) && cost[n + 1] <= budget)
                ++n;
            return now + n;
        }
    };
    struct YardRepair
    {
        int money = 0;
        YardPart hull, sails;
        int Cost(int h, int s) const
        {
            return hull.Of(h) + sails.Of(s);
        }
        int HullCeiling(int s) const
        {
            return hull.Most(money - sails.Of(s));
        }
        int SailsCeiling(int h) const
        {
            return sails.Most(money - hull.Of(h));
        }
        // «Все»: the hull as far as the purse goes, then the sails with what is left.
        std::pair<int, int> All() const
        {
            const int h = hull.Most(money);
            return {h, sails.Most(money - hull.Of(h))};
        }
    };
    struct Repair
    {
        int base = -1;        // the percentage when the window opened (nothing ordered)
        int target = -1;      // what the player asked for; -1 = nothing to do
        int last_value = -1;  // the percentage when the last step was sent
        uint64_t stepped = 0; // the model revision that step was sent in
    };
    // What a build reads of the engine's shipyard state and of our fleet.
    struct Yard
    {
        const Widget *state = nullptr;
        bool ready = false; // version 1 and ready: its capabilities may be used
        std::string mode;
        bool main = false; // no window of the game is open
        uint64_t revision = 0;
        std::vector<const Widget *> fleet;
        size_t current = 0;            // the chosen fleet slot
        const Widget *owned = nullptr; // ... its widget
        bool empty_slot = false, has_owned_ship = false;
        bool yard_preview = false; // the card shows an offer of the yard (else our chosen ship)
    };
    // Sends a capability of this snapshot. A capability may be used once: the engine consumes the generation with
    // it, so at most one goes out per model revision, and none while the state is not ready.
    struct Sender
    {
        ShipyardScreen *screen;
        Bindings *bindings;
        bool ready;
        uint64_t revision;
        bool operator()(const Widget *widget, const std::string &verb) const
        {
            const std::string token = ready ? Token(widget, verb) : std::string();
            if (token.empty() || screen->sent_revision_ == revision)
                return false;
            screen->sent_revision_ = revision;
            bindings->Send(*widget, token);
            return true;
        }
    };
    // Whether the widget offers `verb` in this snapshot.
    struct Offered
    {
        bool ready;
        bool operator()(const Widget *widget, const std::string &verb) const
        {
            return ready && !Token(widget, verb).empty();
        }
    };
    // What closes the open window of the game: its widget and verb.
    struct Close
    {
        const Widget *widget = nullptr;
        std::string verb = "press";
    };

    Yard YardOf(const Admission &admitted, Bindings &bindings)
    {
        Yard yard;
        yard.state = Typed(admitted, "ANDROID_SHIPYARD_STATE", "shipyard_state");
        const Widget *state = yard.state;
        // Version 1 of the engine's shipyard context; anything else is not understood, and a
        // state that is not ready (frames still settling) is drawn but offers nothing.
        yard.ready = state && state->index == 1 && state->value == "1" && Context(state, "ready") == "1";
        yard.mode = Context(state, "mode");
        yard.revision = admitted.model ? admitted.model->revision : 0;
        if (state && state->index != 1)
            bindings.ErrorOnce("version " + std::to_string(state->index),
                               "unknown shipyard state version " + std::to_string(state->index));
        // Frames settle in a moment; a state that stays unusable is reported, with the engine's reason.
        if (yard.ready || !state)
        {
            not_ready_since_ = -1.0;
            reported_ = false;
        }
        else
        {
            if (not_ready_since_ < 0.0)
                not_ready_since_ = bindings.Now();
            const double waited = bindings.Now() - not_ready_since_;
            if (waited < kSettling)
                bindings.RebuildAfter(kSettling - waited);
            else if (!reported_)
            {
                reported_ = true;
                bindings.Error(
                    "the engine's shipyard state is not usable (" +
                    (state->unavailable_reason.empty() ? std::string("not ready") : state->unavailable_reason) + ")");
            }
        }
        yard.main = yard.mode == "main" || yard.mode.empty();
        // Whose ship: the fleet slots.
        for (const Widget *slot : admitted.All("SHIPS_SCROLL"))
            if (slot->type == "choice")
                yard.fleet.push_back(slot);
        for (size_t i = 0; i < yard.fleet.size(); ++i)
            if (yard.fleet[i]->value == "1")
                yard.current = i;
        yard.owned = yard.fleet.empty() ? nullptr : yard.fleet[yard.current];
        yard.empty_slot = Context(yard.owned, "ownerKind") == "EMPTY_SLOT";
        yard.has_owned_ship = Context(yard.owned, "ownerKind") == "OWNER_SHIP";
        // A captain remains in the slot after selling. Its old ship facts must not survive
        // until a later purchase reuses that slot, even if no owned card is drawn meanwhile.
        if (!yard.has_owned_ship)
            own_figures_.erase(Context(yard.owned, "slot"));
        yard.yard_preview = Context(state, "previewKind") == "yard";
        return yard;
    }

    static std::string SlotName(const Bindings &bindings, const Widget *slot)
    {
        return Context(slot, "ownerKind") == "EMPTY_SLOT" ? bindings.Text("yard.empty_slot")
               : !Context(slot, "shipName").empty()       ? Context(slot, "shipName")
                                                          : slot->label;
    }

    // Our ships: every slot that holds one, and one free place (the chosen slot when it is free, else
    // the first). The chosen slot is where a purchase goes: a ship there is traded in.
    static Json Fleet(Bindings &bindings, const Yard &yard, const Sender &send, const Offered &offers)
    {
        const std::vector<const Widget *> &fleet = yard.fleet;
        size_t free_place = fleet.size();
        for (size_t i = 0; i < fleet.size(); ++i)
            if (Context(fleet[i], "ownerKind") == "EMPTY_SLOT" && (free_place == fleet.size() || i == yard.current))
                free_place = i;
        Json ours = Json::array();
        for (size_t i = 0; i < fleet.size(); ++i)
        {
            const Widget *slot = fleet[i];
            const bool free = Context(slot, "ownerKind") == "EMPTY_SLOT";
            if (free && i != free_place)
                continue;
            const std::string type = Context(slot, "type"), name = SlotName(bindings, slot);
            const std::string id = "fleet-" + std::to_string(i);
            ours.push_back(Json{{"id", id},
                                {"title", free || type.empty() ? name : type + " «" + name + "»"},
                                {"meta", ClassLine(bindings, Context(slot, "class"))},
                                {"value", KeepTogether(Context(slot, "saleQuote"))},
                                {"image", free ? Json() : PictureOf(slot->FirstImage())},
                                {"icon", free ? "plus" : "ship"},
                                {"selected", i == yard.current},
                                {"disabled", !yard.main || !offers(slot, "select")}});
            bindings.On(id, "", [send, slot](const Event &) { send(slot, "select"); });
        }
        return ours;
    }

    // The yard's offers: ship, class, price (the card on the right has the rest). `chosen`: the offer on the card.
    static Json OfferRows(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                          const Offered &offers, const Widget *&chosen)
    {
        Json rows = Json::array();
        size_t offer_index = 0;
        for (const Widget *offer : admitted.All("TABLE_SHIPYARD"))
        {
            if (offer->type != "choice")
                continue;
            const auto [type, name] = TypeAndName(offer->CellText(0));
            const Cell *first = nullptr;
            for (const Cell &cell : offer->cells)
                if (cell.column == 0)
                    first = &cell;
            const bool selected = yard.yard_preview && offer->value == "1";
            if (selected)
                chosen = offer;
            const std::string id = "offer-" + std::to_string(offer_index++);
            rows.push_back(
                Json{{"id", id},
                     {"title", name.empty() ? type : type + " «" + name + "»"},
                     {"meta", offer->CellText(1).empty() ? std::string()
                                                         : bindings.Text("yard.class") + " " + offer->CellText(1)},
                     {"value", Grouped(offer->CellText(4))},
                     {"image", PictureOf(first && !first->images.empty() ? &first->images.front() : nullptr)},
                     {"selected", selected},
                     {"disabled", !offers(offer, "select")},
                     {"reason", offer->unavailable_reason}});
            bindings.On(id, "", [send, offer, main = yard.main](const Event &) {
                if (main && offer->value != "1")
                    send(offer, "select");
            });
        }
        return rows;
    }

    // The card of the ship being looked at: its figures and the deal at its bottom.
    Json Detail(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                const Offered &offers, const Widget *chosen)
    {
        std::string name = yard.yard_preview && chosen ? TypeAndName(chosen->CellText(0)).second : std::string();
        if (!yard.yard_preview)
            if (const Widget *edit = Typed(admitted, "EDIT_BOX", "edit"))
                name = edit->value;
        const std::string type = StripWhitespace(admitted.Label("FRAME_INFO_CAPTION"));
        const std::string rank = StripWhitespace(admitted.Label("SHIP_RANK"));
        const Widget *cannon_type = nullptr;
        Json facts = Figures(admitted, yard, cannon_type);
        const Widget *picture = Control(admitted, "SHIP_BIG_PICTURE", -2);
        Json detail{
            {"title", name.empty() ? type : type + " «" + name + "»"},
            {"sub", rank.empty() ? std::string() : bindings.Text("yard.class") + " " + rank},
            {"image", PictureOf(Typed(admitted, "SHIP_BIG_PICTURE", "image")
                                    ? Typed(admitted, "SHIP_BIG_PICTURE", "image")->FirstImage()
                                    : nullptr)},
            {"help", offers(picture, "help")},
            {"helpLabel", bindings.Text("common.description")},
            {"facts", facts},
            {"against", yard.yard_preview && yard.has_owned_ship ? SlotName(bindings, yard.owned) : std::string()},
            {"text", Lines(admitted.Label("INFO_TEXT"))},
            {"actions", Json::array()}};
        bindings.On("detail-help", "", [send, picture](const Event &) { send(picture, "help"); });
        DetailActions(admitted, bindings, yard, send, offers, chosen, cannon_type, detail);
        return detail;
    }

    // The figures, by the game's row: icon, caption, value. Those of our chosen ship are kept while
    // it is on the card, and shown beside an offer's: better green, worse red where a larger last
    // number is plainly better (hull, speed, turn rate, deadweight, crew). `cannon_type`: the cannons' row.
    Json Figures(const Admission &admitted, const Yard &yard, const Widget *&cannon_type)
    {
        struct Figure
        {
            const char *row;
            const char *icon;
            bool larger_is_better;
        };
        static const Figure figures[] = {{"Hull", "hull", true},           {"Sails", "sail-full", false},
                                         {"Speed", "speed", true},         {"Maneuver", "maneuver", true},
                                         {"AgainstWind", "upwind", false}, {"Capacity", "capacity", true},
                                         {"Crew", "crew", true},           {"sCannons", "caliber", false}};
        const std::string slot_key = Context(yard.owned, "slot");
        if (!yard.yard_preview)
            own_figures_[slot_key].clear();
        const auto last_number = [](const std::string &text) {
            size_t end = text.find_last_of("0123456789");
            if (end == std::string::npos)
                return -1.0;
            size_t begin = end;
            while (begin > 0 && (std::isdigit(static_cast<unsigned char>(text[begin - 1])) || text[begin - 1] == '.'))
                --begin;
            return ParseNumber<double>(std::string_view(text).substr(begin, end - begin + 1)).value_or(-1.0);
        };
        Json facts = Json::array();
        for (const Widget *row : admitted.All("TABLE_OTHER"))
        {
            if (row->row_id == "CannonType")
            {
                cannon_type = row;
                continue;
            }
            if (row->CellText(1).empty())
                continue;
            const Figure *figure = nullptr;
            for (const Figure &known : figures)
                if (row->row_id == known.row)
                    figure = &known;
            const std::string value = row->CellText(2);
            Json fact{{"k", row->CellText(1)},
                      {"v", Decimal(value, admitted.model->language)},
                      {"icon", figure ? figure->icon : "ship"}};
            if (!yard.yard_preview)
                own_figures_[slot_key][row->row_id] = value;
            else if (const auto kept = own_figures_.find(slot_key);
                     yard.has_owned_ship && kept != own_figures_.end() && kept->second.count(row->row_id))
            {
                const std::string &ours_value = kept->second[row->row_id];
                fact["own"] = Decimal(ours_value, admitted.model->language);
                const double theirs = last_number(value), mine = last_number(ours_value);
                if (figure && figure->larger_is_better && theirs >= 0 && mine >= 0 && theirs != mine)
                    fact["tone"] = theirs > mine ? "ok" : "danger";
            }
            facts.push_back(std::move(fact));
        }
        return facts;
    }

    // The deal at the card's bottom. Our ship: repair, cannons, sell (held, with its price). An offer:
    // buy into the free place, or trade our chosen ship in with the difference; held as well.
    void DetailActions(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                       const Offered &offers, const Widget *chosen, const Widget *cannon_type, Json &detail)
    {
        const auto action = [&](const char *id, const Widget *button, const std::string &verb, std::string label,
                                std::string sub, const char *variant, bool hold) {
            detail["actions"].push_back(Json{{"id", id},
                                             {"label", std::move(label)},
                                             {"sub", std::move(sub)},
                                             {"variant", variant},
                                             {"hold", hold},
                                             {"disabled", !yard.main || !offers(button, verb)},
                                             {"reason", button->unavailable_reason},
                                             {"busy", bindings.Pending(id)}});
        };
        if (!yard.yard_preview)
        {
            if (const Widget *repair = Typed(admitted, "BUTTON_REPAIR", "button"))
            {
                action("repair", repair, "press", repair->label, {}, "secondary", false);
                bindings.On("repair", "", [send, repair](const Event &) { send(repair, "press"); });
            }
            if (cannon_type)
            {
                action("cannons", cannon_type, "activate", cannon_type->CellText(1), cannon_type->CellText(2),
                       "secondary", false);
                bindings.On("cannons", "", [send, cannon_type](const Event &) { send(cannon_type, "activate"); });
            }
            if (const Widget *sell = Typed(admitted, "BUTTON_SELL", "button"))
            {
                action("sell", sell, "press", sell->label, KeepTogether(Context(yard.owned, "saleQuote")), "danger",
                       true);
                bindings.On("sell", "", [this, &bindings, send, sell](const Event &) {
                    if (send(sell, "press"))
                        held_deal_ = "ShipSell", held_at_ = bindings.Now();
                });
            }
        }
        else if (const Widget *buy = Typed(admitted, "BUTTON_BUY", "button"))
        {
            // shipyard.c ShowMessageInfo: a ship in the chosen slot is traded in for the difference.
            std::string label = buy->label, sub = chosen ? Grouped(chosen->CellText(4)) : std::string();
            if (yard.has_owned_ship && chosen)
            {
                const int price = Amount(bindings, chosen->CellText(4), "the offer's price (TABLE_SHIPYARD column 4)");
                const int quote = Amount(bindings, Context(yard.owned, "saleQuote"), "saleQuote");
                label = bindings.Text(price >= quote ? StringKey("yard.trade_pay") : StringKey("yard.trade_get"));
                sub = Grouped(std::to_string(std::abs(price - quote)));
            }
            action("primary", buy, "press", std::move(label), std::move(sub), "primary", true);
            bindings.On("primary", "", [this, &bindings, send, buy](const Event &) {
                if (send(buy, "press"))
                    held_deal_ = "ShipBuy", held_at_ = bindings.Now();
            });
        }
    }

    // The game asks to confirm a deal it opened unasked.
    Json ConfirmSheet(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                      const Offered &offers, Close &close)
    {
        held_deal_.clear();
        close.widget = Typed(admitted, "MSG_CANCEL", "button");
        const Widget *ok = Typed(admitted, "MSG_OK", "button");
        const bool selling = Context(yard.state, "messageMode") == "ShipSell";
        const Widget *meaning = Typed(admitted, selling ? "BUTTON_SELL" : "BUTTON_BUY", "button");
        Json actions = Json::array();
        if (close.widget)
            actions.push_back(Json{{"id", "sheet-cancel"},
                                   {"label", close.widget->label},
                                   {"variant", "quiet"},
                                   {"disabled", !offers(close.widget, "press")}});
        if (ok)
            actions.push_back(Json{{"id", "sheet-ok"},
                                   {"label", meaning && !meaning->label.empty() ? meaning->label : ok->label},
                                   {"variant", selling ? "danger" : "primary"},
                                   {"disabled", !offers(ok, "press")},
                                   {"reason", ok->unavailable_reason}});
        bindings.On("sheet-ok", "", [send, ok](const Event &) { send(ok, "press"); });
        return Json{{"kind", "confirm"},
                    {"title", StripWhitespace(admitted.Label("MSG_WINDOW_CAPTION"))},
                    {"text", StripWhitespace(admitted.Label("MSG_WINDOW_TEXT"))},
                    {"actions", actions},
                    {"width", 400}};
    }

    // The adapter's costs (android_hero.c AndroidYardRepair); `facts` null: none.
    static YardRepair RepairPlan(Bindings &bindings, const Widget *facts)
    {
        const auto costs = [&bindings, facts](const char *key) {
            std::vector<int> list;
            const std::string text = Context(facts, key);
            for (size_t at = 0; at < text.size();)
            {
                const size_t comma = std::min(text.find(',', at), text.size());
                list.push_back(Amount(bindings, std::string_view(text).substr(at, comma - at), key));
                at = comma + 1;
            }
            return list;
        };
        YardRepair plan;
        if (facts)
            plan = YardRepair{Amount(bindings, Context(facts, "money"), "money"),
                              {Amount(bindings, Context(facts, "hull"), "hull"), costs("hullcost")},
                              {Amount(bindings, Context(facts, "sails"), "sails"), costs("sailscost")}};
        return plan;
    }

    // The repair window: the sliders run over the whole 0…100 %, start at the ship's state, cannot go below it nor
    // past what the purse pays for the other part as it is set.
    Json RepairSheet(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                     const Offered &offers, Close &close)
    {
        close.widget = Typed(admitted, "REPAIR_CANCEL", "button");
        const Widget *facts = admitted.One("ANDROID_YARDREPAIR");
        if (!facts && last_mode_ != "repair")
            bindings.Error("shipyard: no ANDROID_YARDREPAIR from the interface adapter: repair costs unknown");
        const YardRepair plan = RepairPlan(bindings, facts);
        int shown[2] = {0, 0};
        bool stepping = false;
        const Json parts = RepairParts(admitted, bindings, yard, send, offers, facts, plan, shown, stepping);
        const Widget *all = Typed(admitted, "REPAIR_ALL", "button");
        const Widget *ok = Typed(admitted, "REPAIR_OK", "button");
        // The order's price: the adapter's costs of the amounts set (the game's text lags behind the steps).
        std::string price = StripWhitespace(admitted.Label("REPAIR_WINDOW_TEXT"));
        if (facts)
            price = hero::Thousands(plan.Cost(shown[0], shown[1]));
        Json actions = Json::array();
        if (all)
        {
            const bool more = facts ? plan.All() != std::pair{shown[0], shown[1]} : offers(all, "press");
            actions.push_back(
                Json{{"id", "sheet-all"},
                     {"label", bindings.Text("common.all")}, // the word of every fill-to-the-end in the port
                     {"disabled", !yard.ready || !more},
                     {"reason", !facts || !yard.ready               ? all->unavailable_reason
                                : plan.All() == std::pair{100, 100} ? std::string()
                                                                    : bindings.Text("trade.no_money")}});
        }
        if (ok)
            actions.push_back(
                Json{{"id", "sheet-ok"},
                     {"label", bindings.Text("repair.do")}, // as the self repair names it (the game says «Ок»)
                     {"sub", price},
                     {"variant", "primary"},
                     {"disabled", !offers(ok, "press") || (shown[0] <= hull_.base && shown[1] <= sails_.base)},
                     {"reason", ok->unavailable_reason}});
        // «Все»: as far as the purse goes, the hull first and the sails with the rest (the game's own sets the
        // sails first).
        if (facts)
            bindings.On("sheet-all", "", [this, &bindings, plan](const Event &) {
                const auto [hull, sails] = plan.All();
                hull_.target = hull;
                sails_.target = sails;
                bindings.Rebuild();
            });
        else
            bindings.On("sheet-all", "", [send, all](const Event &) { send(all, "press"); });
        // OK repairs what the game has set: while steps toward the amounts are still on their way it waits for
        // them.
        if (confirm_after_steps_ && !stepping && send(ok, "press"))
            confirm_after_steps_ = false;
        bindings.On("sheet-ok", "", [this, &bindings, send, ok, stepping](const Event &) {
            if (stepping)
            {
                confirm_after_steps_ = true;
                bindings.Rebuild();
            }
            else
                send(ok, "press");
        });
        return Json{{"kind", "repair"},   {"title", StripWhitespace(admitted.Label("REPAIR_WINDOW_CAPTION"))},
                    {"closable", true},   {"parts", parts},
                    {"actions", actions}, {"width", 460}};
    }

    // The hull's and the sails' sliders; `shown`: the amounts they show. The game moves a part by its arrow buttons
    // only: one step toward the amount asked for goes out per model revision (`stepping` while one is due).
    Json RepairParts(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                     const Offered &offers, const Widget *facts, const YardRepair &plan, int shown[2], bool &stepping)
    {
        Json parts = Json::array();
        int value_of[2] = {-1, -1};
        for (int k = 0; k < 2; ++k)
        {
            const char kind = k == 0 ? 'H' : 'S';
            const std::string caption = std::string("REPAIR_QTY_") + kind;
            const int value = Amount(bindings, admitted.Label(caption), caption.c_str(), -1); // "55%"; -1: unknown
            Repair &part = k == 0 ? hull_ : sails_;
            if (last_mode_ != "repair")
            {
                part = Repair();
                part.base =
                    facts ? (k == 0 ? plan.hull : plan.sails).now : value; // the window opens with nothing ordered yet
            }
            value_of[k] = value;
            shown[k] = part.target >= 0 ? part.target : std::max(0, value);
        }
        if (confirm_after_steps_ && last_mode_ != "repair")
            confirm_after_steps_ = false;
        for (int k = 0; k < 2; ++k)
        {
            const char kind = k == 0 ? 'H' : 'S';
            const char *stat = k == 0 ? "Hull" : "Sails";
            const int value = value_of[k];
            Repair &part = k == 0 ? hull_ : sails_;
            const int floor = std::max(0, part.base);
            const int ceiling = facts ? (k == 0 ? plan.HullCeiling(shown[1]) : plan.SailsCeiling(shown[0])) : 100;
            std::string label;
            for (const Widget *row : admitted.All("TABLE_OTHER"))
                if (row->row_id == stat)
                    label = row->CellText(1);
            const std::string id = k == 0 ? "hull" : "sails";
            Json q{{"value", shown[k]},
                   {"min", 0},
                   {"max", 100},
                   {"floor", floor},
                   {"unit", "%"},
                   {"label", label},
                   {"steps", Json::array({1, 5})},
                   {"disabled", !yard.ready}};
            if (facts)
            {
                q["ceiling"] = ceiling;
                q["moreReason"] = ceiling < 100 ? bindings.Text("trade.no_money") : std::string();
            }
            parts.push_back(Json{{"id", id}, {"q", q}});
            const auto set = [this, &bindings, k, floor, ceiling](const Event &event) {
                int wanted = 0;
                if (!EventNumber(bindings, event, wanted))
                    return;
                if (wanted > ceiling && bindings.Now() - reason_at_ > 1.5)
                {
                    reason_at_ = bindings.Now();
                    bindings.Toast(bindings.Text("trade.no_money"));
                }
                (k == 0 ? hull_ : sails_).target = std::clamp(wanted, floor, std::max(floor, ceiling));
                bindings.Rebuild();
            };
            bindings.On(id, "change", set);
            bindings.On(id, "commit", set);
            // One step toward the amount asked for per model revision: +-5 while far, then +-1.
            if (part.target >= 0 && value >= 0 && part.target != value)
            {
                stepping = true;
                if (part.stepped == yard.revision)
                {
                    // the step of this revision is in flight
                }
                else if (part.last_value == value && part.stepped != 0)
                    part.target = -1; // the game did not move (money, full repair): stop there
                else
                {
                    const int delta = part.target - value;
                    const std::string side = std::string(delta > 0 ? "REPAIR_RIGHT_" : "REPAIR_LEFT_") + kind;
                    const bool five = std::abs(delta) >= 5;
                    const Widget *step = Control(admitted, side, five ? -5 : -1);
                    if (send(step, five ? "step5" : "press"))
                    {
                        part.stepped = yard.revision;
                        part.last_value = value;
                    }
                    else if (!offers(step, five ? "step5" : "press"))
                        part.target = -1;
                }
            }
            else if (part.target == value)
                part.target = -1;
        }
        return parts;
    }

    // The captain of the chosen ship (from the passengers) or the hero's officer card (rpg).
    static Json CaptainSheet(const Admission &admitted, Bindings &bindings, const Yard &yard, const Sender &send,
                             const Offered &offers, Close &close)
    {
        const bool rpg = yard.mode == "rpg";
        const std::string prefix = rpg ? "RPG_" : "";
        Json candidates = Json::array();
        if (!rpg)
        {
            close.widget = Typed(admitted, "CANCEL_ADD_OFFICER", "button");
            size_t i = 0;
            for (const Widget *person : admitted.All("PASSENGERSLIST"))
            {
                const std::string id = "candidate-" + std::to_string(i++);
                candidates.push_back(Json{{"id", id},
                                          {"title", person->label},
                                          {"image", PictureOf(person->FirstImage())},
                                          {"selected", person->value == "1"},
                                          {"disabled", !offers(person, "select")},
                                          {"reason", person->unavailable_reason}});
                bindings.On(id, "", [send, person](const Event &) {
                    if (person->value != "1")
                        send(person, "select");
                });
            }
        }
        else
        {
            close.widget = Control(admitted, "ANDROID_SHIPYARD_CLOSE", -1);
            close.verb = "close";
        }
        const bool vacant = !rpg && Context(yard.state, "captain").empty();
        Json facts = Json::array();
        Json skills = Json::array();
        if (!vacant)
        {
            for (const Widget *row : admitted.All(prefix + "TABLE_SMALLOTHER"))
                if (!row->CellText(1).empty())
                    facts.push_back(Json{{"k", row->CellText(1)}, {"v", row->CellText(2)}});
            skills = ShipSkills(admitted.All(prefix + "TABLE_SMALLSKILL"), bindings, "shipyard");
        }
        Json actions = Json::array();
        const Widget *accept = rpg ? nullptr : Typed(admitted, "ACCEPT_ADD_OFFICER", "button");
        if (accept)
            actions.push_back(Json{{"id", "sheet-ok"},
                                   {"label", accept->label},
                                   {"variant", "primary"},
                                   {"disabled", !offers(accept, "press")},
                                   {"reason", accept->unavailable_reason}});
        if (accept)
            bindings.On("sheet-ok", "", [send, accept](const Event &) { send(accept, "press"); });
        return Json{{"kind", yard.mode},
                    {"title", StripWhitespace(admitted.Label(prefix + "OFFICERS_WINDOW_CAPTION"))},
                    {"closable", true},
                    {"candidates", candidates},
                    {"empty", !rpg && candidates.empty() ? bindings.Text("yard.no_captains") : std::string()},
                    {"name", vacant ? std::string() : StripWhitespace(admitted.Label(prefix + "OFFICER_NAME"))},
                    {"facts", facts},
                    {"skills", skills},
                    {"actions", actions},
                    {"width", rpg ? 440 : 640}}; // c-captain-picker: the candidates beside the facts
    }

    // The game's own window: its deck plan (CANNONS_SHIP, interfaces\gundeck: the bow to the left) with the
    // guns of each side where the stock window puts them: bow left, stern right, starboard (cannonr) above,
    // port (cannonl) below (shipyard.c CannonsMenuRefresh, shipyard.ini CANNONS_QTY_*).
    static Json CannonsSheet(const Admission &admitted, Bindings &bindings, Close &close)
    {
        close.widget = Typed(admitted, "CANNONS_CANCEL", "button");
        const auto side = [&](const char *key, const char *node) {
            return Json{{"label", bindings.Text(key)}, {"value", StripWhitespace(admitted.Label(node))}};
        };
        Json plan = Json();
        if (const Widget *ship = admitted.One("CANNONS_SHIP"))
            plan = TextureOf(ship->FirstImage());
        if (plan.is_object() && !plan["uv"].get<std::string>().empty())
        {
            // The ship lies in the middle band of the square picture (the stock window stretches it to 584 x 412).
            float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            std::sscanf(plan["uv"].get<std::string>().c_str(), "%f,%f,%f,%f", &uv[0], &uv[1], &uv[2], &uv[3]);
            char band[96];
            std::snprintf(band, sizeof band, "%g,%g,%g,%g", uv[0], uv[1] + (uv[3] - uv[1]) * 0.31f, uv[2],
                          uv[1] + (uv[3] - uv[1]) * 0.75f);
            plan["uv"] = band;
        }
        else
            bindings.ErrorOnce("deck plan", "shipyard: the deck plan (CANNONS_SHIP) has no game texture");
        return Json{{"kind", "cannons"},
                    {"title", StripWhitespace(admitted.Label("CANNONS_CAPTION"))},
                    {"subtitle", StripWhitespace(admitted.Label("CANNONS_TEXT"))},
                    {"closable", true},
                    {"plan", plan},
                    {"bow", side(StringKey("yard.bow"), "CANNONS_QTY_F")},
                    {"stern", side(StringKey("yard.stern"), "CANNONS_QTY_B")},
                    {"port", side(StringKey("yard.port"), "CANNONS_QTY_L")},
                    {"starboard", side(StringKey("yard.starboard"), "CANNONS_QTY_R")},
                    {"actions", Json::array()},
                    {"width", 480}};
    }

    static Json HelpSheet(const Admission &admitted, Bindings &bindings, Close &close)
    {
        close.widget = Control(admitted, "ANDROID_SHIPYARD_CLOSE", -1);
        close.verb = "close";
        Json lines = Json::array();
        for (int i = 1; i <= 4; ++i)
            if (const std::string text = StripWhitespace(Lines(admitted.Label("TOOLTIP_TEXT" + std::to_string(i))));
                !text.empty())
                lines.push_back(text);
        const std::string heading = StripWhitespace(admitted.Label("TOOLTIP_TITILE"));
        return Json{{"kind", "help"},      {"title", heading.empty() ? bindings.Text("common.description") : heading},
                    {"tone", "parchment"}, {"closable", true},
                    {"lines", lines},      {"actions", Json::array()},
                    {"width", 560}};
    }

    static const Widget *Typed(const Admission &admitted, const std::string &node, const char *type)
    {
        for (const Widget *widget : admitted.All(node))
            if (widget->type == type)
                return widget;
        return nullptr;
    }
    static const Widget *Control(const Admission &admitted, const std::string &node, int index)
    {
        for (const Widget *widget : admitted.All(node))
            if (widget->type == "shipyard_control" && widget->index == index)
                return widget;
        return nullptr;
    }

    uint64_t sent_revision_ = UINT64_MAX; // a token was sent for this model revision
    static constexpr double kSettling = 2.0;
    double not_ready_since_ = -1.0;
    bool reported_ = false;
    std::string last_mode_;
    Repair hull_, sails_;
    bool confirm_after_steps_ = false; // OK was pressed while steps toward the amounts were on their way
    double reason_at_ = -10.0;         // when "no money" was last said for a slider push
    // Our ships' figures by fleet slot, kept from the last time each was on the card.
    std::map<std::string, std::map<std::string, std::string>> own_figures_;
    // A deal held on its button (the game's message mode it will ask about) and when.
    static constexpr double kHeldDeal = 3.0;
    std::string held_deal_;
    double held_at_ = -100.0;
};

} // namespace

std::unique_ptr<Screen> CreateShipyardScreen()
{
    return std::make_unique<ShipyardScreen>();
}

} // namespace storm::ui::screens
