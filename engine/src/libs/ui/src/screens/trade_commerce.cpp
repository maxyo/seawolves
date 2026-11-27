// Commerce: the goods of the ship exchange (transfer_main), a free transfer layout (pack screen <s-trade>). Goods
// are one C-TradeList that never moves: filter chips and sortable columns in its head, 44 dp rows; the row whose
// quantity window the game has open shows in the dock beside the list (object, description, C-Quantity with the amount
// on our side after the deal). While the player sets an amount nothing goes to the game: the amount, its limits (stock,
// money, hold), the total and the button's text are computed here from what the open row already says, with the game's
// own formulas (DealMath below mirrors store.c / cannons.c / contraband.c / itemstrade.c ChangeQTY_EDIT), so the
// control answers in the same frame. Only the confirmation goes out (AmountDraft, controls.h): the
// amount and the game's OK in one flush, on «Купить / Продать N» of a money deal and «Взять / Отдать N»
// of a free transfer alike. There is no «Отмена»: another row, or the amount back where
// it was, drops the draft (the game's window of the old row is closed with nothing moved). The hand at a
// row's end takes the whole stack in one tap.
//
// Packs: the game trades goods by the piece. A price in the table is the price of a pack (Goods[].Units
// pieces) and the total is pro rata (store.c: price * qty / units); the weight counts every started pack
// whole (goods.h GetGoodWeightByType). So the amount steps by one piece, and the dock says what the prices
// and the weight are for («за 20 шт.») instead of the game's «пачка 20 шт.».
//
#include "amount.h"
#include "controls.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace storm::ui::screens
{

namespace
{

struct Layout
{
    const char *title;             // string key of the title when the game sends no caption
    int own;                       // amount on our side (hold, character)
    int other;                     // amount on the other side (store, storage, chest)
    int name;                      // name and picture
    int buy = -1;                  // what one pack costs us; -1 = no prices: a free transfer
    int sell = -1;                 // what we get for one pack
    int pack = -1;                 // pack size
    const char *owners;            // node of the "whose" selector
    bool small = false;            // single items: steps of 1 and 10
    int details[3] = {-1, -1, -1}; // extra columns named in the dock (cannons: range, damage, reload)
    bool quick = false;  // the hand at a row's end takes the whole stack (free transfers by the game's amount window)
    int weight = -1;     // what one pack (goods) or one piece (items) weighs; -1 = the deal has no weight
    bool pieces = false; // itemstrade.c: single pieces with fractional weights (else goods in packs, whole cwt)
    const char *carry_icon = "capacity"; // the header figure of what we carry (the hold; a character's weight)
};

// The arithmetic of a money deal exactly as the game's scripts do it (ChangeQTY_EDIT of store.c, cannons.c,
// contraband.c for goods in packs; itemstrade.c for pieces), so that what the player sees while choosing is
// what the game will charge. makeint() truncates; all values here are non-negative.
struct DealMath
{
    bool pieces = false;                // itemstrade.c
    int units = 1;                      // pack size (Goods[].Units); 1 for pieces and cannons
    double weight = 0.0;                // of one pack / one piece
    double load = 0.0, capacity = -1.0; // what we carry of what we can; capacity < 0 = unknown: no weight limit applied
    int money = -1;                     // ours; < 0 = unknown: no money limit applied

    // goods.h GetGoodWeightByType: whole packs, rounded at 0.05; pieces: fWeight * qty.
    double Weight(int amount) const
    {
        if (pieces)
            return weight * amount;
        return static_cast<double>(static_cast<int>(((amount + units - 1) / units) * weight + 0.05));
    }
    // makeint(price * qty / iUnits + 0.5); pieces have no units.
    int Total(int amount, int price) const
    {
        return static_cast<int>(static_cast<double>(price) * amount / (pieces ? 1 : units) + 0.5);
    }
    // The most that can be bought of `stock`: the largest amount the game takes as it is. Its script lets a request
    // through while what we carry with it does not exceed the capacity and its total does not exceed the money
    // (past that it cuts the request down, and by one pack more than needed); both grow with the amount, so the
    // largest request that passes is found by halving.
    int BuyLimit(int stock, int price) const
    {
        const auto passes = [&](int amount) {
            if (capacity >= 0.0 && weight > 0.0 && load + Weight(amount) > capacity)
                return false;
            return !(money >= 0 && price > 0 && Total(amount, price) > money);
        };
        int low = 0, high = std::max(0, stock);
        while (low < high)
        {
            const int middle = low + (high - low + 1) / 2;
            if (passes(middle))
                low = middle;
            else
                high = middle - 1;
        }
        return low;
    }
};

// The goods of the loot / exchange screen (transfer_main): ours, weight, name, theirs, weight.
constexpr Layout kTransfer{StringKey("transfer.title"), 0, 3, 2, -1, -1, -1, "", false};

constexpr NodeRule kNodes[] = {
    {"TABLE_LIST", "choice|table", true},
    {"ANDROID_LOOT", "fact"}, // town loot: what a pack of each good is worth (the adapter)
    {"EXIT_BTN", "button"},
    {"STORECAPTION1", "text"},
    {"TITLE", "heading|text"},
    {"INFO_TEXT", "text"},
    {"STORAGE_INFO", "text"},
    {"SHIPS_SCROLL", "choice"},
    {"CHARACTERS_SCROLL", "choice"},
    {"LEFTSCROLLBUTTON", "button"},
    {"RIGHTSCROLLBUTTON", "button"},
    {"OUR_GOLD", "text"},
    {"CAPACITY", "text"},
    {"STORE_CAPACITY", "text"},
    {"FOOD_SHIP", "text"},
    {"TABBTN_*", "image|button"},
    {"TABSTR_*", "text"},
    {"GETALL_BUTTON", "button"},
    {"ANDROID_TAKE_ALL", "button"}, // the engine runs the game's take-all where the game hides it (a chest)
    {"DELBODY_BUTTON", "button"},
    {"SKULL_BUTTON", "button"},
    {"QTY_EDIT", "edit"},
    {"QTY_EDITBOX", "edit"},
    {"QTY_OK_BUTTON", "button"},
    {"QTY_CANCEL_BUTTON", "button"},
    {"QTY_ADD_BUTTON", "button"},
    {"QTY_REMOVE_BUTTON", "button"},
    {"QTY_ADD_ALL_BUTTON", "button"},
    {"QTY_REMOVE_ALL_BUTTON", "button"},
    {"QTY_*", "text|heading|image"},
    {"MAIN_CHARACTER_PICTURE", "image"},
    {"OTHER_PICTURE", "image"},
};

// What our side and the other side hold of a row: the game writes a number in both columns.
int Ours(Bindings &bindings, const Widget &row, const Layout &columns)
{
    return Amount(bindings, row.CellText(columns.own), "the own amount column");
}

int Theirs(Bindings &bindings, const Widget &row, const Layout &columns)
{
    return Amount(bindings, row.CellText(columns.other), "the other amount column");
}

// The pack of a row (Goods[].Units), at least 1.
int Units(Bindings &bindings, const Widget &row, const Layout &columns)
{
    return std::max(1, Amount(bindings, row.CellText(columns.pack), "the pack column", 1));
}

// A price cell: "-" where the game does not trade the row this way (contraband is not sold back), read as 0.
int Price(const Widget &row, int column)
{
    return ParseAmount(row.CellText(column)).value_or(0);
}

// The trade type, which the game tells only by the colour of the goods name: the tone of
// the row's second line and the string key of its word.
struct TradeType
{
    const char *tone = "";
    const char *word = nullptr;
};
TradeType TradeTypeOf(uint32_t argb)
{
    switch (argb & 0xffffff)
    {
    case 0xc4ffc4:
        return {"ok", StringKey("trade.export")};
    case 0xc4c4ff:
        return {"gold", StringKey("trade.import")};
    case 0xffc462:
        return {"gold", StringKey("trade.demand")};
    case 0xffc4c4:
        return {"danger", StringKey("trade.contraband_goods")};
    default:
        return {};
    }
}

// The game's goods text ends with the line "Вес 1 ц, пачка 20 шт." (store.c, contraband.c, cannons.c, storage.c,
// ColonyCapture.c, transfer_main.c ShowGoodsInfo: the word «пачка» is written into the scripts, the same in every
// language). The dock shows the weight and what the figures are for as its own facts, so that line goes.
std::string WithoutPackLine(const std::string &text, const std::string &units)
{
    if (units.empty())
        return text;
    const std::string mark = ", \xD0\xBF\xD0\xB0\xD1\x87\xD0\xBA\xD0\xB0 " + units; // ", пачка N"
    std::string out;
    size_t begin = 0;
    while (begin <= text.size())
    {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(begin, end - begin);
        if (line.find(mark) == std::string::npos)
            out += (out.empty() ? "" : "\n") + line;
        begin = end + 1;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

bool Profitable(const TradeType &type)
{
    return type.word &&
           (std::string(type.word) == StringKey("trade.export") ||
            std::string(type.word) == StringKey("trade.import") || std::string(type.word) == StringKey("trade.demand"));
}

// The prices' icons wear the colours of the quantity's zones: taking (buying) green, giving (selling) red.
const char *IconTone(const std::string &icon)
{
    return icon == "price-buy" ? "green" : icon == "price-sell" ? "danger" : "brass";
}

// 1500 → "1 500".
std::string Spaced(int value)
{
    std::string digits = std::to_string(std::abs(value)), out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ' ';
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

// "Трюм:\n189 / 199" or "Дедвейт: 189 / 700" → {label, value}; a text without a
// separated value ("Магазин") is no figure.
Json Figure(const std::string &text)
{
    size_t line = text.find('\n');
    if (line == std::string::npos)
    {
        line = text.find(':');
        if (line == std::string::npos || text.find_first_of("0123456789", line) == std::string::npos)
            return Json();
    }
    std::string label = text.substr(0, line);
    while (!label.empty() && (label.back() == ':' || label.back() == ' '))
        label.pop_back();
    const size_t value = text.find_first_not_of(' ', line + 1);
    return Json{{"label", label}, {"value", OneLine(text.substr(value == std::string::npos ? text.size() : value))}};
}

class CommerceScreen final : public Screen
{
  public:
    explicit CommerceScreen(const Layout &layout) : layout_(layout)
    {
    }
    const char *Component() const override
    {
        return "s-trade";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Layout &columns = layout_;
        language_ = admitted.model->language;
        const Open open = OpenOf(admitted);
        const bool holding = FollowGame(admitted, bindings, open);
        const Captions captions = CaptionsOf(bindings, open.goods);
        const Deal deal = DealOf(admitted, bindings, open);
        TakeQuick(bindings, open, deal.theirs);
        AnswerTakeAll(admitted, bindings, open.goods);

        Json deal_view; // the open deal's quantity control (view["deal"]), null without one
        const Json rows = Rows(admitted, bindings, open, deal, captions, deal_view);
        BindList(admitted, bindings, open, deal);

        Json view = Json::object();
        const std::string caption = OneLine(admitted.Label("STORECAPTION1"));
        view["title"] = !admitted.Label("TITLE").empty() ? admitted.Label("TITLE")
                        : caption.empty()                ? bindings.Text(columns.title)
                                                         : caption;
        view["subject"] = Subject(admitted, bindings, open);
        view["stats"] = Stats(admitted, bindings, open, deal);
        view["list"] = List(bindings, open.goods, captions, rows);
        view["deal"] = std::move(deal_view);
        view["list"]["filters"] = Filters(admitted, bindings, captions);
        Info(admitted, bindings, open, view);
        Primary(admitted, bindings, open, deal, view);

        const Widget *exit = admitted.One("EXIT_BTN");
        bindings.On("header-back", "", [this, &bindings, exit, deal = open.deal](const Event &) {
            quick_row_.clear();
            reopen_row_.clear();
            // The game's exit closes its amount window when one is open and leaves only when none is (store.c,
            // cannons.c ProcessCancelExit), and the dock keeps one open: the exit goes twice, as two Esc presses.
            if (deal)
                bindings.Back(exit);
            bindings.Back(exit);
        });
        if (!hold_row_.empty())
            bindings.Await(); // the row's amount window comes in a later game frame
        if (holding)
        {
            // The view kept is the one before the game's step. What it shows of a deal this model has closed (the
            // amount control, its description, the deal's button) takes no input until the game's next model: the
            // game has no window to apply it to.
            if (!open.deal)
                for (const auto &[id, verb] : {std::pair{"goods", "info"}, std::pair{"goods", "change"},
                                               std::pair{"goods", "commit"}, std::pair{"primary", ""}})
                    if (!bindings.Bound(id, verb))
                        bindings.Ignore(id, verb);
            return last_view_;
        }
        last_view_ = view;
        return view;
    }

  private:
    // What a build reads of the game's table: its rows, the selected one and the amount window open on it.
    struct Open
    {
        std::vector<const Widget *> goods;
        const Widget *ok = nullptr, *cancel = nullptr, *edit = nullptr, *selected = nullptr;
        bool deal = false; // the game's quantity window is open
    };
    // The captions and icons of the two amount columns.
    struct Captions
    {
        std::string own, other, own_icon, other_icon;
    };
    // The open deal as the player sets it: the draft's ends and amount, with the game's arithmetic for the row.
    struct Deal
    {
        int ours = 0, theirs = 0;          // what both sides hold of the selected row now
        int origin = 0, other = 0;         // ... and when its window opened (the draft's pivot and the other side)
        int buy_price = 0, sell_price = 0; // of the selected row
        int low = 0, high = 0, most = 0;   // the scale's ends; `most`: what stock, hold and purse let it reach
        int shown = 0, amount = 0;         // the amount on our side; what it moves (> 0 we take, < 0 we give)
        int total = 0;                     // what the move costs or brings
        double weight = 0.0;               // what the move weighs
        DealMath math;                     // the game's arithmetic for the row
        std::string more_reason;           // why the amount cannot go further up
        bool nothing = false;              // nothing at all can be done: the deal shows disabled
        const Widget *balance = nullptr;   // OUR_GOLD
    };
    // «Забрать самое ценное» (town loot).
    Open OpenOf(const Admission &admitted) const
    {
        Open open;
        open.goods = admitted.Rows("TABLE_LIST");
        open.ok = admitted.One("QTY_OK_BUTTON");
        open.cancel = admitted.One("QTY_CANCEL_BUTTON");
        open.edit = admitted.One("QTY_EDIT") ? admitted.One("QTY_EDIT") : admitted.One("QTY_EDITBOX");
        for (const Widget *row : open.goods)
            if (row->value == "1")
                open.selected = row;
        open.deal = open.selected && open.edit && (open.ok || open.cancel);
        return open;
    }

    // The caption of the table's column, "" past the game's headers.
    static std::string Header(const std::vector<const Widget *> &goods, int column)
    {
        const Widget *first = goods.empty() ? nullptr : goods.front();
        return first && column >= 0 && static_cast<size_t>(column) < first->column_headers.size()
                   ? OneLine(first->column_headers[static_cast<size_t>(column)])
                   : std::string();
    }

    std::string WeightText(double value) const
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), layout_.pieces ? "%.1f" : "%.0f", value);
        return Decimal(buffer, language_);
    }

    // The game's steps after a tap, a confirmed deal or an owner switch: the rows' windows they open (OpenRow) and
    // whether this model is one of those steps, not to be drawn (true: the last view stays).
    bool FollowGame(const Admission &admitted, Bindings &bindings, const Open &open)
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        const Widget *selected = open.selected;
        const bool deal = open.deal;
        // A tapped row: the game selects it in one loop and opens its window in the next. The frame
        // between (the row selected, no deal yet) is not shown: the screen stays as it was until the
        // deal arrives, so the selection and its deal appear together. A row whose window does not
        // come is shown selected after kSelectHold. The handlers are bound from this model all the
        // same (← still closes what the game has open).
        bool holding = false;
        if (!hold_row_.empty())
        {
            // the game's window opened after the tap (the tapped row's, or the one it chose); the
            // window still open from before the tap is not its answer
            const bool arrived = deal && (selected->row_id == hold_row_ || admitted.model->revision > hold_revision_);
            // The game refused the tap (Router::Answer logged why): no window comes, the wait ends now and the
            // screen shows the game's own selection.
            const bool refused = !arrived && bindings.AnsweredSince(hold_floor_) == Bindings::Answers::Refused;
            const bool still = !arrived && !refused && !last_view_.is_null() && bindings.Now() - hold_at_ < kSelectHold;
            if (still)
            {
                holding = true;
                bindings.RebuildAfter(hold_at_ + kSelectHold - bindings.Now());
            }
            else if (!arrived && !refused)
                bindings.Error(std::string(columns.title) + ": the game opened no window for " + hold_row_ +
                               " within " + std::to_string(kSelectHold) + " s; shown selected");
            if (!still)
                hold_row_.clear();
        }
        // A shop opens, and switches ship or character, with a row selected and its amount window
        // closed (on a PC Enter or a double click opens it): that row's deal is opened as a tap
        // would, so the selected row comes with its deal. The free windows (storage, loot, chests)
        // open on their take-all instead. The list is decided once a row is selected: cannons.c
        // selects its first row in its first frame, after the list was already sent.
        std::string owner;
        for (const Widget *one : admitted.All(columns.owners))
            if (one->value == "1")
                owner = one->row_id;
        // An arrow tapped while a row's window was on its way: the switch goes out once that window has come (or
        // its wait ended), so it is closed first like any open deal (SwitchOwner).
        if (switch_to_ && hold_row_.empty() && quick_row_.empty())
        {
            const int to = *std::exchange(switch_to_, std::nullopt);
            const Widget *target = nullptr;
            for (const Widget *one : admitted.All(columns.owners))
                if (one->index == to)
                    target = one;
            if (!target || !target->Offers("select"))
                bindings.Error(std::string(columns.title) + ": the owner " + std::to_string(to) +
                               " chosen while a row's window was on its way is gone; not switched");
            else if (target->value != "1")
                SwitchOwner(bindings, *target, deal, open.cancel, selected);
        }
        // A confirmed deal closes the game's amount window and leaves its row selected: that row's deal is opened
        // again, as on opening, so the dock keeps its amount and button for the next deal of the same row (else it
        // showed the row's name with nothing to do until the row was tapped again). A deal left with ← is not.
        if (!reopen_row_.empty() && !deal && admitted.model->revision > reopen_revision_)
        {
            if (money && selected && selected->row_id == reopen_row_ && hold_row_.empty() &&
                selected->Offers("activate"))
            {
                OpenRow(bindings, *selected, admitted.model->revision);
                // This model (the row selected, its window closed) is a step of the game's answer to the confirmed
                // deal, not a state to draw: the deal's view stays until the reopened window replaces it.
                holding = !last_view_.is_null();
            }
            reopen_row_.clear();
        }
        // Another ship or character: the selector moved in the model, but the game binds the new owner (refCharacter,
        // its table refilled, the first row selected) in its ProcessFrame of the next frame, so the model the move
        // came in still has the old owner's rows. The deal is asked for on the next model (the game's own switch has
        // run by then) or, when none comes within kSelectHold, on this one: the game's switch changed no widget, so
        // these rows are the new owner's. The row whose deal was open at the switch is opened again when the new
        // owner's list has it (by its name), else the row the game selected.
        // A switch the game refused never comes: row taps stop waiting for it.
        if (switching_ && bindings.AnsweredSince(switch_floor_) == Bindings::Answers::Refused)
        {
            switching_ = false;
            follow_row_.reset();
        }
        if (selected && opened_for_ != owner)
        {
            const bool switched = opened_for_.has_value();
            opened_for_ = owner;
            switching_ = false;
            if (switched)
            {
                settle_revision_ = admitted.model->revision;
                settle_at_ = bindings.Now();
                settling_ = true;
            }
            else if (money && !deal && selected->Offers("activate"))
                OpenRow(bindings, *selected, admitted.model->revision);
        }
        if (settling_ && selected)
        {
            if (admitted.model->revision > settle_revision_ || bindings.Now() - settle_at_ >= kSelectHold)
            {
                settling_ = false;
                const Widget *row = selected;
                if (follow_row_)
                    for (const Widget *one : open.goods)
                        if (one->CellText(columns.name) == *follow_row_)
                            row = one;
                const bool reopen = follow_row_ ? true : money;
                follow_row_.reset();
                if (reopen && !deal && hold_row_.empty() && row->Offers("activate"))
                    OpenRow(bindings, *row, admitted.model->revision);
            }
            else
                bindings.RebuildAfter(settle_at_ + kSelectHold - bindings.Now());
        }
        return holding;
    }

    // The two amount columns: the game's captions; «У вас» / «Здесь» where the game names both sides alike.
    Captions CaptionsOf(Bindings &bindings, const std::vector<const Widget *> &goods) const
    {
        const Layout &columns = layout_;
        Captions out{Header(goods, columns.own), Header(goods, columns.other)};
        // Personal stock, a merchant/store or a container/other ship are distinct at a glance.
        out.own_icon = columns.small ? "hero" : "capacity";
        out.other_icon = &columns == &kTransfer ? "ship" : "store";
        if (out.own.empty() || out.own == out.other)
        {
            out.own = bindings.Text("trade.yours");
            out.other = bindings.Text("trade.theirs");
        }
        else
        {
            if (out.other.rfind(out.own, 0) == 0) // "Предметов" / "Предметов торговца"
                out.own = bindings.Text("trade.yours");
        }
        return out;
    }

    // The open deal is the player's draft until it is confirmed: what we hold and what the other side has
    // are taken when the game opens the row's amount window (the pivot of the control and its two ends),
    // and the amount starts at the pivot; nothing goes to the game while it is chosen.
    Deal DealOf(const Admission &admitted, Bindings &bindings, const Open &open)
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        const Widget *selected = open.selected;
        Deal d;
        d.ours = selected ? Ours(bindings, *selected, columns) : 0;
        d.theirs = selected ? Theirs(bindings, *selected, columns) : 0;
        if (!open.deal)
            draft_.Close();
        else
            draft_.Follow(selected->row_id, d.ours, d.theirs, bindings);
        d.origin = draft_.Origin(), d.other = draft_.Other();
        d.buy_price = selected && money ? Price(*selected, columns.buy) : 0;
        d.sell_price = selected && money ? Price(*selected, columns.sell) : 0;
        const bool can_take = !selected || !money || selected->CellText(columns.buy) != "-";
        const bool can_give = !selected || !money || selected->CellText(columns.sell) != "-";
        // The scale's left end: everything we hold given away; what we hold is the pivot.
        d.low = can_give ? 0 : d.origin;
        d.high = can_take ? d.origin + d.other : d.origin;

        // The game's arithmetic for this row; the limits stop the knob where the game would.
        DealMath &math = d.math;
        math.pieces = columns.pieces;
        if (selected)
        {
            if (columns.pack >= 0)
                math.units = Units(bindings, *selected, columns);
            if (columns.weight >= 0)
                math.weight = Number(bindings, selected->CellText(columns.weight), "TABLE_LIST weight");
        }
        if (const Json carried = Figure(admitted.Label("CAPACITY")); carried.is_object())
        {
            const std::string value = carried["value"];
            if (const size_t slash = value.find('/'); slash != std::string::npos)
            {
                math.load = Number(bindings, std::string_view(value).substr(0, slash), "CAPACITY");
                math.capacity = Number(bindings, std::string_view(value).substr(slash + 1), "CAPACITY");
            }
        }
        d.balance = admitted.One("OUR_GOLD");
        if (d.balance)
            math.money = Amount(bindings, d.balance->label, "OUR_GOLD", -1);
        d.most = money ? std::min(d.high, d.origin + math.BuyLimit(d.other, d.buy_price)) : d.high;
        // Why the amount cannot go further up although the other side has more: the purse or the room. A tap or
        // a drag past the end says it; with nothing at all to do the deal shows as disabled.
        if (open.deal && money && can_take && d.most < d.high)
        {
            DealMath unpaid = math;
            unpaid.money = -1; // the limit of the room alone
            d.more_reason =
                bindings.Text(unpaid.BuyLimit(d.other, d.buy_price) > d.most - d.origin ? StringKey("trade.no_money")
                              : columns.pieces                                          ? StringKey("trade.no_carry")
                                                                                        : StringKey("trade.no_room"));
        }
        d.nothing = open.deal && money && d.low == std::max(d.low, d.most) && !d.more_reason.empty();
        d.shown = open.deal ? draft_.Value(d.low, d.most) : d.ours;
        d.amount = d.shown - d.origin;
        d.total = money ? math.Total(std::abs(d.amount), d.amount > 0 ? d.buy_price : d.sell_price) : 0;
        d.weight = math.Weight(std::abs(d.amount));
        return d;
    }

    // The quick take waits for the game to open the row's amount window, then takes all of it.
    void TakeQuick(Bindings &bindings, const Open &open, int theirs)
    {
        if (quick_row_.empty())
            return;
        if (open.deal && open.selected->row_id == quick_row_ && open.edit->Offers("edit") && open.ok)
        {
            bindings.SendAfter(0.0, *open.edit, "edit", std::to_string(theirs), open.selected->row_id,
                               std::string("0"));
            bindings.SendAfter(0.0, *open.ok, "press", {}, open.selected->row_id);
            quick_row_.clear();
        }
        else if (bindings.Now() - quick_at_ > 3.0)
        {
            bindings.Error(std::string(layout_.title) + ": the game did not open the amount window of " + quick_row_ +
                           "; nothing was taken");
            quick_row_.clear();
        }
        else
            bindings.RebuildAfter(quick_at_ + 3.0 - bindings.Now());
    }

    // «Взять всё» of a chest answered: the game's own take-all ran (ANDROID_TAKE_ALL) and
    // stopped at what the hero can carry; anything still there is too heavy.
    void AnswerTakeAll(const Admission &admitted, Bindings &bindings, const std::vector<const Widget *> &goods)
    {
        const Widget *take_all = admitted.One("ANDROID_TAKE_ALL");
        if (take_all_revision_ && admitted.model->revision > take_all_revision_ &&
            !(take_all && bindings.Waiting(*take_all, "press")))
        {
            take_all_revision_ = 0;
            if (std::any_of(goods.begin(), goods.end(),
                            [&](const Widget *row) { return Theirs(bindings, *row, layout_) > 0; }))
                bindings.Toast(bindings.Text("trade.too_heavy"));
        }
    }

    // The order of the rows: the game's, or by the column whose caption the player tapped.
    std::vector<size_t> Order(Bindings &bindings, const std::vector<const Widget *> &goods) const
    {
        const Layout &columns = layout_;
        std::vector<size_t> order(goods.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        const int sort_columns[4] = {columns.own, columns.other, columns.buy, columns.sell};
        if (sort_ >= 0 && sort_ < 4 && sort_columns[sort_] >= 0)
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                const int column = sort_columns[sort_];
                const bool price = column == columns.buy || column == columns.sell;
                const auto key = [&](const Widget &row) {
                    return price                   ? Price(row, column)
                           : column == columns.own ? Ours(bindings, row, columns)
                                                   : Theirs(bindings, row, columns);
                };
                const int left = key(*goods[a]), right = key(*goods[b]);
                return sort_descending_ ? left > right : left < right;
            });
        return order;
    }

    // The list's rows in their order and filter; the open deal's quantity control goes to `deal_view`.
    Json Rows(const Admission &admitted, Bindings &bindings, const Open &open, const Deal &deal,
              const Captions &captions, Json &deal_view) const
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        const std::vector<const Widget *> &goods = open.goods;
        Json rows = Json::array();
        for (const size_t i : Order(bindings, goods))
        {
            const Widget *row = goods[i];
            const int own = Ours(bindings, *row, columns);
            if (filter_ == 1 && own == 0)
                continue;
            const Cell *name = nullptr;
            for (const Cell &cell : row->cells)
                if (cell.column == columns.name)
                    name = &cell;
            const TradeType type = name ? TradeTypeOf(name->color_argb) : TradeType();
            const char *tone = type.tone;
            // «Выгодно»: what this place sells cheap (export) and buys dear (import, high demand), the game's own
            // trade types of its goods (refStore.Goods.<good>.TradeType, told by the colour of the name).
            if (filter_ == 2 && (money ? !Profitable(type) : Theirs(bindings, *row, columns) == 0))
                continue;
            // Beside the name: the trade type only (a 44 dp row keeps the name whole); the dock's second line
            // adds the details (cannons: range, damage, reload); prices, weight and the pack are the dock's facts.
            const std::string meta = type.word ? bindings.Text(type.word) : std::string();
            std::string sub = meta;
            for (const int detail : columns.details)
                if (detail >= 0 && !row->CellText(detail).empty())
                    sub += (sub.empty() ? "" : " · ") + Header(goods, detail) + " " + row->CellText(detail);
            Json cells = Json::array({row->CellText(columns.own), row->CellText(columns.other)});
            if (money)
            {
                cells.push_back(Json{{"text", row->CellText(columns.buy)}, {"bold", true}});
                cells.push_back(Json{{"text", row->CellText(columns.sell)}, {"bold", true}});
            }
            const std::string pack = columns.pack >= 0 ? row->CellText(columns.pack) : std::string();
            Json view{{"id", std::to_string(i)},
                      {"name", row->CellText(columns.name)},
                      {"image", PictureOf(name && !name->images.empty() ? &name->images.front() : row->FirstImage())},
                      {"meta", meta},
                      {"sub", sub},
                      {"quick", columns.quick && Theirs(bindings, *row, columns) > 0 && row->Offers("activate")},
                      {"metaTone", tone},
                      {"cells", cells},
                      {"selected", row == open.selected},
                      {"disabled", !row->Offers("activate") && !row->Offers("select")},
                      {"reason", row->unavailable_reason}};
            if (row == open.selected)
                view["facts"] = Facts(bindings, *row, pack);
            if (row == open.selected && open.deal)
            {
                deal_view = Quantity(bindings, open, deal, captions);
                view["text"] = WithoutPackLine(admitted.Label("QTY_GOODS_INFO"), pack);
            }
            rows.push_back(std::move(view));
        }
        return rows;
    }

    // One slider for both directions: what we hold is the origin, the zone to its
    // left gives (sells), the zone to its right takes (buys), each in its colour.
    Json Quantity(Bindings &bindings, const Open &open, const Deal &deal, const Captions &captions) const
    {
        const bool money = layout_.buy >= 0;
        Json quantity{{"value", deal.shown},
                      {"min", deal.low},
                      // The right end is the most this deal can take (stock, hold, purse): fixed while the
                      // deal is open, so the whole rail is usable and the pivot stays where it is.
                      {"max", std::max(deal.low, deal.most)},
                      {"zones", true},
                      {"origin", deal.origin},
                      {"lessLabel", bindings.Text(money ? StringKey("trade.sell") : StringKey("trade.give"))},
                      {"moreLabel", bindings.Text(money ? StringKey("trade.buy") : StringKey("trade.take"))},
                      {"label", captions.own},
                      {"allLabel", bindings.Text("common.all")},
                      {"summary", "-"}, // a money deal's price is on its button (one place)
                      {"moreReason", deal.more_reason},
                      {"reason", deal.nothing ? deal.more_reason : std::string()},
                      {"disabled", !open.edit->Offers("edit") || deal.nothing}};
        if (layout_.small)
            quantity["steps"] = Json::array({1, 10});
        // The deal is the dock's own prop, not the row's: a drag changes it alone, so the list's
        // rows stay as they are and the quantity control is the only component that renders
        // (components.cpp Forward). What the dock's row template defaulted is said here.
        for (const char *key : {"all", "stack"})
            if (!quantity.contains(key))
                quantity[key] = true;
        if (!quantity.contains("surface"))
            quantity["surface"] = "wood";
        return quantity;
    }

    // The list's taps: sorting by a caption, the hand at a row's end, a row, the open deal's control.
    void BindList(const Admission &admitted, Bindings &bindings, const Open &open, const Deal &deal)
    {
        bindings.On("goods", "sort", [this, &bindings](const Event &event) {
            // A caption's taps: largest first, smallest first, the game's order again.
            int column = 0;
            if (!EventNumber(bindings, event, column))
                return;
            if (sort_ != column)
                sort_ = column, sort_descending_ = true;
            else if (sort_descending_)
                sort_descending_ = false;
            else
                sort_ = -1;
            bindings.Rebuild();
        });
        if (layout_.quick)
            bindings.On(
                "goods", "quick",
                [this, &bindings, goods = open.goods, selected = open.selected, deal = open.deal, cancel = open.cancel,
                 edit = open.edit, ok = open.ok, theirs = deal.theirs](const Event &event) {
                    size_t index = 0;
                    if (!EventNumber(bindings, event, index))
                        return;
                    if (index >= goods.size())
                        return;
                    const Widget *row = goods[index];
                    if (deal && row == selected)
                    {
                        if (!edit->Offers("edit") || !ok)
                            return;
                        bindings.SendAfter(0.0, *edit, "edit", std::to_string(theirs), row->row_id, std::string("0"));
                        bindings.SendAfter(0.0, *ok, "press", {}, row->row_id);
                        return;
                    }
                    if (deal && cancel && cancel->Offers("press"))
                        bindings.Send(*cancel, "press", {}, selected->row_id);
                    if (!row->Offers("activate"))
                        return;
                    quick_row_ = row->row_id;
                    quick_at_ = bindings.Now();
                    bindings.Send(*row, "activate", row->row_id);
                });
        bindings.On("goods", "row",
                    [this, &bindings, goods = open.goods, selected = open.selected, deal = open.deal,
                     cancel = open.cancel, revision = admitted.model->revision](const Event &event) {
                        size_t index = 0;
                        if (!EventNumber(bindings, event, index))
                            return;
                        if (index >= goods.size())
                            return;
                        const Widget *row = goods[index];
                        if (switching_ || settling_)
                        {
                            // Another owner was just chosen and the game has not bound it yet: these rows are the old
                            // owner's (under the old or already the new owner's identities), so the game would refuse
                            // the tap. The tapped goods open once the new owner's list is in (Build), as a deal open
                            // at the switch does; the switch has closed the deal already.
                            follow_row_ = row->CellText(layout_.name);
                            return;
                        }
                        if (deal && cancel && cancel->Offers("press"))
                            bindings.Send(*cancel, "press", {}, selected->row_id);
                        quick_row_.clear();
                        reopen_row_.clear();
                        if (deal && row == selected)
                            return; // a tap on the open row closes it
                        if (row->Offers("activate"))
                            OpenRow(bindings, *row, revision); // its window comes a loop after its selection (Build)
                        else if (row->Offers("select"))
                            bindings.Send(*row, "select", row->row_id);
                    });
        if (open.deal)
            draft_.Bind(bindings, "goods", deal.low, deal.most,
                        deal.more_reason); // the control sits inside the list and reports under its id
    }

    // Whose side: the ship or character selector; null with one owner.
    Json Subject(const Admission &admitted, Bindings &bindings, const Open &open)
    {
        const std::vector<const Widget *> owners = admitted.All(layout_.owners);
        if (owners.size() <= 1)
            return Json();
        size_t current = 0;
        for (size_t i = 0; i < owners.size(); ++i)
            if (owners[i]->value == "1")
                current = i;
        // The arrows stay while a deal is open: a switch closes the deal first (SwitchOwner).
        const bool closable = !open.deal || (open.cancel && open.cancel->Offers("press"));
        Json subject{{"label", owners[current]->label},
                     {"sub", std::to_string(current + 1) + " " + bindings.Text("common.of") + " " +
                                 std::to_string(owners.size())},
                     {"prev", current > 0 && closable},
                     {"next", current + 1 < owners.size() && closable},
                     {"list", false}};
        const auto choose = [this, &bindings, owners, deal = open.deal, cancel = open.cancel,
                             selected = open.selected](size_t index) {
            if (index >= owners.size() || !owners[index]->Offers("select"))
                return;
            if (!hold_row_.empty() || !quick_row_.empty())
            {
                switch_to_ = owners[index]->index; // a row's window is on its way: switched once it has come
                return;
            }
            SwitchOwner(bindings, *owners[index], deal, cancel, selected);
        };
        bindings.On("header", "subject-prev", [choose, current](const Event &) { choose(current - 1); });
        bindings.On("header", "subject-next", [choose, current](const Event &) { choose(current + 1); });
        bindings.Ignore("header", "subject-list");
        return subject;
    }

    // Header figures. While a deal is being set they show what it changes, computed here like the deal.
    Json Stats(const Admission &admitted, Bindings &bindings, const Open &open, const Deal &deal) const
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        const DealMath &math = deal.math;
        Json stats = Json::array();
        for (const char *node : {"CAPACITY", "STORE_CAPACITY"})
        {
            Json figure = Figure(admitted.Label(node));
            if (figure.is_null() || stats.size() == 2)
                continue;
            figure["value"] = Decimal(figure["value"].get<std::string>(), language_);
            // What we carry is its icon (the hold, a character's weight) beside the figure, without the game's word.
            if (std::string(node) == "CAPACITY")
            {
                figure.erase("label");
                figure["icon"] = columns.carry_icon;
            }
            // (the preview is there from the moment the deal opens, so the header does not reflow at the first step)
            if (std::string(node) == "CAPACITY" && open.deal && money && math.capacity >= 0.0 && math.weight > 0.0)
            {
                figure["value"] = WeightText(math.load); // as the preview writes it ("34" and "34,0" would differ)
                figure["preview"] = WeightText(math.load + (deal.amount > 0 ? deal.weight : -deal.weight));
                figure["previewTone"] = "neutral";
            }
            stats.push_back(std::move(figure));
        }
        // The storage writes its load as a sentence ("Общая загруженность склада : 0 / 50000 ц.")
        // and names the place in STORE_CAPACITY ("Склад").
        if (Json load = Figure(admitted.Label("STORAGE_INFO")); !load.is_null() && stats.size() < 2)
        {
            const std::string place = OneLine(admitted.Label("STORE_CAPACITY"));
            if (!place.empty() && Figure(place).is_null())
                load["label"] = place;
            stats.push_back(std::move(load));
        }
        const std::string gold = deal.balance ? deal.balance->label : std::string();
        if (const size_t digit = gold.find_first_of("0123456789"); digit != std::string::npos)
        {
            const size_t end = gold.find_first_not_of("0123456789 ", digit);
            std::string purse = gold.substr(digit, end == std::string::npos ? std::string::npos : end - digit);
            while (!purse.empty() && purse.back() == ' ')
                purse.pop_back();
            // Some merchants trade for doubloons; the engine marks the balance then.
            const auto currency = deal.balance->context.find("currency");
            Json figure = currency != deal.balance->context.end() && currency->second == "doubloons"
                              ? Json{{"label", bindings.Text("trade.doubloons")}, {"value", purse}, {"tone", "gold"}}
                              : Json{{"icon", "money"}, {"value", purse}, {"tone", "gold"}};
            if (open.deal && money && math.money >= 0)
                figure["preview"] = Spaced(math.money + (deal.amount > 0 ? -deal.total : deal.total));
            if (stats.size() == 2)
                stats.erase(1);
            stats.push_back(std::move(figure));
        }
        return stats;
    }

    // The component shows column icons and keeps captions as long-press explanations.
    Json List(Bindings &bindings, const std::vector<const Widget *> &goods, const Captions &captions,
              const Json &rows) const
    {
        std::vector<std::pair<std::string, std::string>> heads{{captions.own, captions.own_icon},
                                                               {captions.other, captions.other_icon}};
        if (layout_.buy >= 0)
        {
            heads.emplace_back(bindings.Text("trade.buy"), "price-buy");
            heads.emplace_back(bindings.Text("trade.sell"), "price-sell");
        }
        Json cols = Json::array();
        for (size_t i = 0; i < heads.size(); ++i)
            cols.push_back(Json{{"label", heads[i].first},
                                {"icon", heads[i].second},
                                {"iconTone", IconTone(heads[i].second)},
                                {"sortable", true},
                                {"sort", sort_ == static_cast<int>(i) ? (sort_descending_ ? "desc" : "asc") : ""}});
        return Json{{"label", Header(goods, layout_.name)},
                    {"cols", cols},
                    {"rows", rows},
                    {"quick", layout_.quick ? "hand" : ""}};
    }

    // The game's own category tabs (chests) replace our filters.
    Json Filters(const Admission &admitted, Bindings &bindings, const Captions &captions)
    {
        Json tabs = Json::array();
        std::vector<const Widget *> tab_buttons;
        for (int i = 1; i <= 8; ++i)
        {
            const Widget *button = admitted.One("TABBTN_" + std::to_string(i));
            const Widget *caption = admitted.One("TABSTR_" + std::to_string(i));
            if (!button || !caption || caption->label.empty())
                continue;
            // The script reads the click on the caption over the picture where it has one.
            const Widget *target = caption->Offers("press") ? caption : button;
            tabs.push_back(Json{
                {"label", caption->label}, {"selected", button->value == "1"}, {"disabled", !target->Offers("press")}});
            tab_buttons.push_back(target);
        }
        if (!tabs.empty())
        {
            bindings.On("goods", "filter", [&bindings, tab_buttons](const Event &event) {
                size_t index = 0;
                if (!EventNumber(bindings, event, index))
                    return;
                if (index < tab_buttons.size() && tab_buttons[index]->Offers("press"))
                    bindings.Send(*tab_buttons[index], "press");
            });
            return tabs;
        }
        Json filters = Json::array({Json{{"label", bindings.Text("common.all")}, {"selected", filter_ == 0}},
                                    Json{{"label", captions.own}, {"selected", filter_ == 1}}});
        if (!layout_.small)
            filters.push_back(Json{{"label", layout_.buy >= 0 ? bindings.Text("trade.profitable") : captions.other},
                                   {"selected", filter_ == 2}});
        bindings.On("goods", "filter", [this, &bindings](const Event &event) {
            int filter = 0;
            if (!EventNumber(bindings, event, filter))
                return;
            filter_ = std::clamp(filter, 0, 2);
            bindings.Rebuild();
        });
        return filters;
    }

    // The description of the open deal is in the dock (a tap reads it whole); without a deal the
    // dock offers the place's own text.
    void Info(const Admission &admitted, Bindings &bindings, const Open &open, Json &view) const
    {
        if (open.deal)
        {
            view["info"] = false;
            const std::string units =
                open.selected && layout_.pack >= 0 ? open.selected->CellText(layout_.pack) : std::string();
            bindings.On("goods", "info",
                        [&bindings, text = WithoutPackLine(admitted.Label("QTY_GOODS_INFO"), units),
                         title = admitted.Label("QTY_CAPTION")](const Event &) { bindings.Read(title, text); });
        }
        else
        {
            std::string info = admitted.Label("INFO_TEXT");
            if (!admitted.Label("STORAGE_INFO").empty())
                info += (info.empty() ? "" : "\n") + admitted.Label("STORAGE_INFO");
            view["info"] = !info.empty();
            bindings.On("info", "", [&bindings, info](const Event &) { bindings.Read(std::string(), info); });
        }
        view["infoLabel"] = bindings.Text("common.description");
        view["dealing"] = open.deal; // the dock is the deal's: the place's note waits
    }

    // Bar buttons: the deal of a money trade, else what the place offers (take all, …).
    void Primary(const Admission &admitted, Bindings &bindings, const Open &open, const Deal &deal, Json &view)
    {
        const Layout &columns = layout_;
        const bool money = columns.buy >= 0;
        Json actions = Json::array();
        view["primary"] = Json();
        if (deal.nothing)
            // Not even one can be bought and there is nothing to sell: the deal is there, disabled, and says why.
            view["primary"] = Json{{"label", bindings.Text("trade.buy")},
                                   {"sub", ""},
                                   {"disabled", true},
                                   {"reason", deal.more_reason},
                                   {"busy", false}};
        else if (open.deal && open.ok)
        {
            // The button names the deal in our words with its price (and the weight it moves): the amount and
            // the game's OK go out together when it is pressed, nothing before.
            std::string sub = money ? Spaced(deal.total) : std::string();
            if (deal.math.weight > 0.0)
                sub += (sub.empty() ? "" : " \xC2\xB7 ") + WeightText(deal.weight) +
                       (columns.pieces ? std::string() : " " + bindings.Text("transfer.cwt"));
            const AmountDraft::Labels labels =
                money ? AmountDraft::Labels{StringKey("trade.buy"), StringKey("trade.sell")}
                      : AmountDraft::Labels{StringKey("trade.take"), StringKey("trade.give")};
            view["primary"] =
                draft_.Buttons(bindings, deal.low, deal.most, labels, sub,
                               !open.ok->Offers("press") || !open.edit->Offers("edit"), open.ok->unavailable_reason,
                               [this, &bindings, edit = open.edit, ok = open.ok, row = open.selected->row_id,
                                revision = admitted.model->revision](int delta) {
                                   // The game's edit box holds the amount to take, a negative one the amount to give;
                                   // once it has turned to giving it reads every later number so, and zero leaves it:
                                   // zero goes first, same flush.
                                   bindings.SendAfter(0.0, *edit, "edit", std::to_string(delta), row, std::string("0"));
                                   bindings.SendAfter(0.0, *ok, "press", {}, row);
                                   reopen_row_ = row; // its deal again once the game has closed this one (Build)
                                   reopen_revision_ = revision;
                               });
        }
        else if (!open.deal)
            PlaceButtons(admitted, bindings, open.goods, view, actions);
        view["actions"] = actions;
    }

    // Without a deal: the place's own buttons (take all, bury, …).
    void PlaceButtons(const Admission &admitted, Bindings &bindings, const std::vector<const Widget *> &goods,
                      Json &view, Json &actions)
    {
        for (const char *node : {"DELBODY_BUTTON", "SKULL_BUTTON"})
            if (const Widget *button = admitted.One(node))
                actions.push_back(
                    ButtonView(bindings, *button, "action-" + std::to_string(actions.size()), "secondary"));
        if (const Widget *all = admitted.One("GETALL_BUTTON"))
        {
            view["primary"] = Json{{"label", all->label},
                                   {"sub", ""},
                                   {"disabled", !all->Offers("press")},
                                   {"reason", all->unavailable_reason},
                                   {"busy", bindings.Pending("primary")}};
            bindings.Forward("primary", *all, "press");
        }
        else if (const Widget *take = admitted.One("ANDROID_TAKE_ALL");
                 take && std::any_of(goods.begin(), goods.end(),
                                     [&](const Widget *row) { return Theirs(bindings, *row, layout_) > 0; }))
        {
            // A chest or a barrel: the game keeps its take-all hidden there; the engine runs it
            // for us in one script call (ANDROID_TAKE_ALL), with its weight cap and one refresh.
            view["primary"] = Json{{"label", bindings.Text("trade.take_all")},
                                   {"sub", ""},
                                   {"disabled", !take->Offers("press")},
                                   {"busy", false}};
            const uint64_t revision = admitted.model->revision;
            bindings.On("primary", "", [this, &bindings, take, revision](const Event &) {
                take_all_revision_ = revision;
                bindings.Send(*take, "press");
            });
        }
    }

    // A tap on a row as the player's: its window comes a loop after its selection (see Build).
    void OpenRow(Bindings &bindings, const Widget &row, uint64_t revision)
    {
        hold_row_ = row.row_id;
        hold_at_ = bindings.Now();
        hold_revision_ = revision;
        hold_floor_ = bindings.RequestFloor();
        bindings.Send(row, "activate", row.row_id);
        bindings.RebuildAfter(kSelectHold);
    }

    // Another ship or character, chosen with the header's arrows. The game's own switch runs (the selector's
    // select: ProcessFrame binds the owner, refills the table and selects its first row). An open deal is closed
    // first with the game's cancel: the scripts' ProcessFrame hides the amount window on a switch but leaves
    // bShowChangeWin set (store.c, cannons.c, itemstrade.c, itemsbox.c), and with it set the next opened row would
    // confirm the old window's amount (ShowItemInfo → TransactionOK) instead of opening. Its row is remembered and
    // opened again for the new owner (Build).
    void SwitchOwner(Bindings &bindings, const Widget &owner, bool deal, const Widget *cancel, const Widget *selected)
    {
        quick_row_.clear();
        reopen_row_.clear();
        follow_row_.reset();
        if (deal)
        {
            if (!cancel || !cancel->Offers("press"))
            {
                bindings.Error(
                    std::string(layout_.title) +
                    ": the open deal cannot be closed (no QTY_CANCEL_BUTTON press); the owner is not switched");
                return;
            }
            bindings.Send(*cancel, "press", {}, selected->row_id);
            follow_row_ = selected->CellText(layout_.name);
        }
        switching_ = true; // until the selector has moved in a model (Build), the rows shown are the old owner's
        switch_floor_ = bindings.RequestFloor();
        bindings.Send(owner, "select", owner.row_id);
    }

    // The facts of the open row, in the dock under its name: the two prices, the weight (an icon, no word) and
    // what they are for («за 20 шт.»: a pack of goods; the game sells it by the piece, see the top).
    Json Facts(Bindings &bindings, const Widget &row, const std::string &pack) const
    {
        const Layout &columns = layout_;
        Json facts = Json::array();
        if (columns.buy >= 0)
            for (const auto &[column, icon] :
                 {std::pair{columns.buy, "price-buy"}, std::pair{columns.sell, "price-sell"}})
            {
                // "-": the game does not trade it this way here (contraband is not sold back)
                const std::optional<int> price = ParseAmount(row.CellText(column));
                facts.push_back(
                    !price
                        ? Json{{"icon", icon}, {"iconTone", IconTone(icon)}, {"v", "\xE2\x80\x94"}, {"tone", "dim"}}
                        : Json{{"icon", icon}, {"iconTone", IconTone(icon)}, {"v", Spaced(*price)}, {"tone", "gold"}});
            }
        if (columns.weight >= 0)
        {
            if (const std::string weight = OneLine(row.CellText(columns.weight)); !weight.empty())
                facts.push_back(Json{
                    {"icon", "weight"},
                    {"v", Decimal(columns.pieces ? weight : weight + " " + bindings.Text("transfer.cwt"), language_)}});
        }
        if (!facts.empty() && !pack.empty() && pack != "1")
            facts.push_back(Json{{"v", Fill(bindings.Text("trade.per_units"), {pack})}, {"tone", "dim"}});
        return facts;
    }

    static constexpr double kSelectHold = 0.5; // s: how long a tapped row may wait for its window
    const Layout &layout_;
    std::string language_;         // the game's, for its decimal points (Decimal)
    Json last_view_;               // what the screen shows (kept while a tapped row waits for its window)
    std::string hold_row_;         // the tapped row whose window has not come yet
    std::string reopen_row_;       // the row of a confirmed deal: its deal is opened again once the game closed it
    uint64_t reopen_revision_ = 0; // the model revision the deal was confirmed in
    double hold_at_ = 0.0;
    uint64_t hold_revision_ = 0; // the model revision the row was tapped in
    uint64_t hold_floor_ = 0;    // the request number of the tap's activate (Bindings::AnsweredSince)
    std::optional<std::string>
        opened_for_;               // the ship / character whose list was opened (its selected row's deal asked for)
    bool switching_ = false;       // a switch went out and the model has not shown the new owner yet
    uint64_t switch_floor_ = 0;    // the request number of that switch's select (Bindings::AnsweredSince)
    bool settling_ = false;        // another owner was chosen: its deal waits for the game's own switch (Build)
    uint64_t settle_revision_ = 0; // the model the new owner came in
    double settle_at_ = 0.0;
    std::optional<std::string> follow_row_; // the name of the row whose deal was open at a switch (opened again)
    std::optional<int> switch_to_;          // the owner (its index) chosen while a row's window was on its way
    int filter_ = 0;                        // 0 all, 1 on our side, 2 profitable (export goods) / on the other side
    int sort_ = -1; // the column the rows are ordered by (own, other, buy, sell); -1 = the game's order
    bool sort_descending_ = true;
    std::string quick_row_;          // the row whose whole stack is taken once the game opens its amount window
    uint64_t take_all_revision_ = 0; // «Взять всё» of a chest went out from this revision (0 = none)
    double quick_at_ = 0.0;
    AmountDraft draft_; // the open deal: its row, what both sides held when the game opened it, the amount set
};

} // namespace

// Used inside the loot / exchange screen (extra_transfer.cpp), which adds its windows.
std::unique_ptr<Screen> CreateTransferGoods()
{
    return std::make_unique<CommerceScreen>(kTransfer);
}

} // namespace storm::ui::screens
