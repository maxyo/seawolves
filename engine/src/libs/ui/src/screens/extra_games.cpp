// Tavern mini-games (interfaces "boal_dice" and "boal_cards"):
// the 2D felt of the original with our own dice and cards. The game draws
// its pieces with script images and keeps the turn in script variables; the engine exports
// both (xinterface mobile_table_game.hpp): images by their script name, the money lines,
// and the marker ANDROID_TABLE_GAME whose context holds the facts read here. Fills <s-table-game>.
//
// The rules stay in the script. A tap on our die is B_HeroDice<n>, which charges the stake
// at once (ClickHeroDice) — the marked die rises with «−stake» and cannot be unmarked.
// B_PACK throws the cup / takes a card / starts another round, B_ICON_1 keeps the dice /
// stands, EXIT_BTN leaves (the script lets it only between rounds).
#include "controls.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace storm::ui::screens
{

namespace
{

constexpr NodeRule kNodes[] = {
    {"B_PACK", "button", true}, {"ANDROID_TABLE_GAME", "table_game", true},
    {"EXIT_BTN", "button"},     {"B_ICON_1", "button"},
    {"B_ICON_2", "button"}, // the hero's portrait: no command in either game
    {"B_HeroDice*", "button"},  {"INFO_TEXT", "text"},
    {"Money", "text"},          {"MoneyInChest", "text"},
    {"ICON_*", "image"},        {"HeroDice*", "image"},
    {"CompDice*", "image"},     {"Dice*", "image"}, // dice rolling over the felt: shown as they settle in the rows
    {"DiceCup", "image"},       {"PCard*", "image"},
    {"Pack", "image"},          {"BLANK", "image"}, // the card flying from the deck
    {"Money_*", "image"},       {"GOLD", "image"},
};

int Int(Bindings &bindings, const Widget &marker, const char *key)
{
    const auto found = marker.context.find(key);
    return found == marker.context.end() ? 0 : Amount(bindings, found->second, key);
}

std::string Str(const Widget &marker, const char *key)
{
    const auto found = marker.context.find(key);
    return found == marker.context.end() ? std::string() : found->second;
}

std::vector<std::string> List(const std::string &text)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (!text.empty() && at <= text.size())
    {
        const size_t end = std::min(text.find(',', at), text.size());
        out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
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
    return value < 0 ? "−" + out : out;
}

// "кубик / кубика / кубиков": the Russian three forms; other languages one / many.
std::string Counted(Bindings &bindings, const std::string &language, int n, const char *one, const char *few,
                    const char *many)
{
    const char *key = many;
    if (language == "russian")
    {
        const int tens = n % 100, units = n % 10;
        key = tens >= 11 && tens <= 14 ? many : units == 1 ? one : units >= 2 && units <= 4 ? few : many;
    }
    else if (n == 1)
        key = one;
    return std::to_string(n) + " " + bindings.Text(key);
}

// Pip centres (% of the die) of faces 1…6.
Json Pips(int face)
{
    static const std::vector<std::array<int, 2>> kFaces[6] = {
        {{50, 50}},
        {{28, 28}, {72, 72}},
        {{26, 26}, {50, 50}, {74, 74}},
        {{26, 26}, {74, 26}, {26, 74}, {74, 74}},
        {{26, 26}, {74, 26}, {50, 50}, {26, 74}, {74, 74}},
        {{28, 24}, {72, 24}, {28, 50}, {72, 50}, {28, 76}, {72, 76}},
    };
    Json pips = Json::array();
    if (face >= 1 && face <= 6)
        for (const auto &pip : kFaces[face - 1])
            pips.push_back(Json::array({pip[0], pip[1]}));
    return pips;
}

constexpr int kTilt[] = {-3, 2, -2, 3, -1, 2, -3, 1};

const Widget *Image(const Admission &admitted, const std::string &node)
{
    const Widget *image = admitted.One(node);
    return image && image->type == "image" ? image : nullptr;
}

// "dice_3_1" → 3.
int DieFace(Bindings &bindings, const std::string &picture)
{
    return picture.starts_with("dice_") ? Amount(bindings, std::string_view(picture).substr(5), "die picture") : 0;
}

class TableGameScreen final : public Screen
{
  public:
    explicit TableGameScreen(bool dice) : dice_(dice)
    {
    }
    const char *Component() const override
    {
        return "s-table-game";
    }
    std::span<const NodeRule> Nodes() const override
    {
        return kNodes;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        const Widget &marker = *admitted.One("ANDROID_TABLE_GAME");
        if (!marker.unavailable_reason.empty())
            bindings.ErrorOnce(marker.unavailable_reason, "table game: " + marker.unavailable_reason);
        const Round round = RoundOf(admitted, bindings, marker);

        Json view = Json::object();
        view["kind"] = dice_ ? "dice" : "cards";
        view["title"] = bindings.Text(dice_ ? "games.dice" : "games.cards");
        view["stats"] = Json::array({{{"label", bindings.Text("games.stake")}, {"value", Spaced(round.rate)}},
                                     {{"label", bindings.Text("games.money")}, {"value", Spaced(round.money)}}});
        view["locked"] = !round.open;
        view["table"] = Table(admitted, bindings, marker, round);

        Buttons buttons{bindings, round};
        Json pack_pieces = Json::array();
        if (dice_)
            Dice(admitted, bindings, marker, round, buttons, pack_pieces, view);
        else
            Cards(admitted, bindings, marker, round, buttons, pack_pieces, view);
        view["pack"] = pack_pieces;
        view["buttons"] = buttons.list;

        bindings.On("again", "", [&bindings, pack = round.pack](const Event &) { bindings.Send(*pack, "press"); });
        bindings.On("leave", "", [&bindings, exit = round.exit](const Event &) { bindings.Back(exit); });
        bindings.On("header-back", "", [&bindings, exit = round.exit, open = round.open](const Event &) {
            if (open)
                bindings.Back(exit);
            else
                bindings.Toast(bindings.Text("games.round"));
        });
        bindings.Ignore("top", "");
        return view;
    }

  private:
    // What the adapter says of the round (ANDROID_TABLE_GAME) and the game's buttons.
    struct Round
    {
        std::string language;
        int rate = 0, money = 0, bank = 0; // the stake, our money, the bank
        int stage = 0, turn = 0, first = 0;
        bool open = false; // the table is open: between rounds
        const Widget *pack = nullptr, *pass = nullptr, *exit = nullptr;
        std::string wait; // why a control waits: the opponent's turn
    };
    static Round RoundOf(const Admission &admitted, Bindings &bindings, const Widget &marker)
    {
        Round round;
        round.language = admitted.model->language;
        round.rate = Int(bindings, marker, "rate");
        round.money = Int(bindings, marker, "moneyHero");
        round.bank = Int(bindings, marker, "bank");
        round.stage = Int(bindings, marker, "stage");
        round.turn = Int(bindings, marker, "turn");
        round.first = Int(bindings, marker, "first");
        round.open = Int(bindings, marker, "open") != 0;
        round.pack = admitted.One("B_PACK");
        round.pass = admitted.One("B_ICON_1");
        round.exit = admitted.One("EXIT_BTN");
        round.wait = bindings.Text("games.wait");
        return round;
    }

    // The bar's buttons.
    struct Buttons
    {
        Bindings &bindings;
        const Round &round;
        Json list = Json::array();
        void Add(const char *id, const std::string &label, const std::string &sub, const char *variant, bool enabled,
                 const std::string &reason)
        {
            list.push_back({{"id", id},
                            {"label", label},
                            {"sub", sub},
                            {"variant", variant},
                            {"disabled", !enabled},
                            {"reason", enabled ? std::string() : reason},
                            {"busy", bindings.Pending(id)}});
        }
        // Between rounds: another round (B_PACK) or leave; the script says when the opponent quits.
        void Between(bool again)
        {
            const std::string stake = bindings.Text("games.stake_short") + " " + Spaced(round.rate);
            if (again)
            {
                Add("leave", bindings.Text("games.leave"), "", "secondary", round.exit != nullptr, "");
                Add("again", bindings.Text("games.again"), stake, "primary", true, "");
            }
            else
            {
                Add("again", bindings.Text("games.again"), "", "secondary", false, "");
                Add("leave", bindings.Text("games.leave"), "", "primary", round.exit != nullptr, "");
            }
        }
    };

    // The bank, and after a round what it came to: we remember our money after the round's stakes went out (the
    // lowest of the round: rerolls and cards cost more stakes) and its bank (the dice script empties the bank when
    // the round ends). The script pays the winner (EndGameCount) frames before it opens the table (openExit), so
    // the money may rise while the round is still closed: that rise is the win, not a new round's money.
    Json Table(const Admission &admitted, Bindings &bindings, const Widget &marker, const Round &round)
    {
        const std::string opponent = Str(marker, "opponent");
        Json table = {
            {"opponent", opponent}, {"line", admitted.Label("INFO_TEXT")}, {"bankLabel", ""}, {"bankTone", "gold"}};
        if (const std::string face = Str(marker, "opponentFace"); !face.empty())
            table["portrait"] = "INTERFACES/PORTRAITS/128/face_" + face;
        table["coins"] = "silver";
        for (const Widget *widget : admitted.known)
            if (widget->type == "image" && Upper(widget->node).rfind("MONEY_", 0) == 0)
            {
                table["coins"] = widget->label == "gold" ? "gold" : "silver";
                break;
            }
        if (!round.open)
        {
            if (round_open_ || round_money_ < 0)
            {
                round_money_ = round.money;
                round_bank_ = 0;
            }
            round_money_ = std::min(round_money_, round.money);
            if (round.bank > 0)
                round_bank_ = round.bank;
            table["bank"] = round.bank > 0 ? Spaced(round.bank) : "";
        }
        else if (round_money_ >= 0 && round.money > round_money_)
        {
            table["bankLabel"] = bindings.Text("games.won");
            table["bank"] = "+" + Spaced(round.money - round_money_);
            table["bankTone"] = "win";
        }
        else if (round_money_ >= 0 && round_bank_ > 0)
        {
            table["bankLabel"] =
                bindings.Text("games.taken") + (opponent.empty() ? "" : " " + opponent.substr(0, opponent.find(' ')));
            table["bank"] = Spaced(round_bank_);
            table["bankTone"] = "loss";
        }
        else
            table["bank"] = "";
        round_open_ = round.open;
        return table;
    }

    void Dice(const Admission &admitted, Bindings &bindings, const Widget &marker, const Round &round, Buttons &buttons,
              Json &pack_pieces, Json &view)
    {
        const bool locked = Int(bindings, marker, "locked") != 0;
        const std::vector<std::string> hero = List(Str(marker, "heroDice")), mix = List(Str(marker, "heroMix"));
        const bool throw_first = !round.open && round.turn == 1 && !locked && round.stage < 2;
        const bool reroll = !round.open && round.turn == 1 && !locked &&
                            ((round.first == 1 && round.stage == 2) || (round.first == -1 && round.stage == 3));
        int marked = 0;
        Json ours = Json::array();
        bool all_ours = true;
        for (int n = 1; n <= 5; ++n)
        {
            const Widget *image = Image(admitted, "HeroDice" + std::to_string(n));
            const bool in_cup = n <= static_cast<int>(mix.size()) && mix[n - 1] == "1";
            const int remembered = n <= static_cast<int>(hero.size()) ? Amount(bindings, hero[n - 1], "heroDice") : 0;
            if (in_cup && reroll)
                ++marked;
            if (!image && !(reroll && in_cup && remembered))
            {
                all_ours = false;
                continue;
            }
            Json die = {{"kind", "die"}, {"rot", kTilt[n - 1]}};
            if (image)
            {
                die["pips"] = Pips(DieFace(bindings, image->label));
                const Widget *hit = admitted.One("B_HeroDice" + std::to_string(n));
                if (hit && !round.open)
                {
                    die["action"] = "mark";
                    die["value"] = std::to_string(n);
                    const bool can = reroll && round.money >= round.rate;
                    die["disabled"] = !can;
                    die["reason"] = !reroll ? round.wait : bindings.Text("games.no_money");
                }
            }
            else
            {
                die["pips"] = Pips(remembered);
                die["lift"] = true;
                die["cost"] = "−" + Spaced(round.rate);
                all_ours = false;
            }
            ours.push_back(std::move(die));
        }
        if (throw_first)
            marked = 5;
        Json theirs = Json::array();
        bool all_theirs = true;
        for (int n = 1; n <= 5; ++n)
            if (const Widget *image = Image(admitted, "CompDice" + std::to_string(n)))
                theirs.push_back(
                    {{"kind", "die"}, {"rot", kTilt[n + 2]}, {"pips", Pips(DieFace(bindings, image->label))}});
            else
                all_theirs = false;
        const auto combo = [&](const char *key) {
            const int type = Int(bindings, marker, key);
            return type >= 1 && type <= 8 ? bindings.Text("games.combo_" + std::to_string(type)) : std::string();
        };
        view["top"] = {
            {"pieces", theirs}, {"label", all_theirs ? combo("compType") : ""}, {"size", 44}, {"below", true}};
        view["bottom"] = {
            {"pieces", ours}, {"label", all_ours ? combo("heroType") : ""}, {"size", 60}, {"costs", true}};

        const bool roll = (throw_first || reroll) && marked > 0;
        const bool keep = reroll && marked == 0;
        pack_pieces.push_back({{"kind", "cup"},
                               {"action", "press"},
                               {"value", ""},
                               {"disabled", !roll && !(round.open && !locked)},
                               {"reason", round.open ? "" : round.wait}});
        if (round.open)
            buttons.Between(!locked);
        else if (keep)
            buttons.Add("throw", bindings.Text("games.keep"), "", "primary", round.pass != nullptr, round.wait);
        else
            buttons.Add(
                "throw", bindings.Text("games.roll"),
                roll ? Counted(bindings, round.language, marked, "games.die_one", "games.die_few", "games.die_many")
                     : "",
                "primary", roll, round.wait);

        std::array<const Widget *, 5> hits{};
        for (int n = 1; n <= 5; ++n)
            hits[n - 1] = admitted.One("B_HeroDice" + std::to_string(n));
        bindings.On("bottom", "mark", [&bindings, hits](const Event &event) {
            int n = 0;
            if (!EventNumber(bindings, event, n))
                return;
            if (n >= 1 && n <= 5 && hits[n - 1])
                bindings.Send(*hits[n - 1], "press");
        });
        bindings.On("throw", "", [&bindings, pack = round.pack, pass = round.pass, keep](const Event &) {
            const Widget *target = keep ? pass : pack;
            if (target)
                bindings.Send(*target, "press");
        });
        bindings.On("pack", "", [&bindings, pack = round.pack](const Event &) { bindings.Send(*pack, "press"); });
    }

    void Cards(const Admission &admitted, Bindings &bindings, const Widget &marker, const Round &round,
               Buttons &buttons, Json &pack_pieces, Json &view)
    {
        const bool moving = Int(bindings, marker, "cardMove") != 0;
        const bool playing = !round.open && round.turn == 1 && round.stage >= 2 && round.stage != 100;
        Json ours = Json::array(), theirs = Json::array();
        int our_points = 0, their_points = 0;
        bool hidden = false;
        for (int n = 0; n < 36; ++n)
        {
            const Widget *image = Image(admitted, "PCard" + std::to_string(n));
            if (!image)
                continue;
            Json &row = n < 18 ? ours : theirs;
            const std::string &picture = image->label;
            const size_t cut = picture.find('_');
            Json card = {{"rot", kTilt[row.size() % 8] * 2}};
            if (cut == std::string::npos)
            {
                card["kind"] = "back";
                hidden = hidden || n >= 18;
            }
            else
            {
                const std::string suit = picture.substr(0, cut), rank = picture.substr(cut + 1);
                card["kind"] = "card";
                card["suit"] = suit;
                card["red"] = suit == "hearts" || suit == "diamonds";
                card["rank"] = rank.size() == 1 && !std::isdigit(static_cast<unsigned char>(rank[0]))
                                   ? bindings.Text("games.rank_" + rank)
                                   : rank;
                // What the game counts the card (the engine's points: NullCharacter.Cards.<c>.count).
                if (const auto points = image->context.find("points"); points != image->context.end())
                    (n < 18 ? our_points : their_points) += Amount(bindings, points->second, "card points");
                else
                    bindings.ErrorOnce("card points " + picture,
                                       "table game: the game counts no points for card " + picture);
            }
            row.push_back(std::move(card));
        }
        const auto points = [&](int n) {
            return Counted(bindings, round.language, n, "games.point_one", "games.point_few", "games.point_many");
        };
        view["top"] = {
            {"pieces", theirs}, {"label", !theirs.empty() && !hidden ? points(their_points) : ""}, {"size", 90}};
        view["bottom"] = {{"pieces", ours}, {"label", ours.empty() ? "" : points(our_points)}, {"size", 90}};
        const bool take = playing && !moving && round.money >= round.rate;
        if (Image(admitted, "Pack"))
            pack_pieces.push_back({{"kind", "deck"},
                                   {"action", "press"},
                                   {"value", ""},
                                   {"disabled", !take && !round.open},
                                   {"reason", playing ? bindings.Text("games.no_money") : round.wait}});
        if (round.open)
            buttons.Between(round.stage != 100);
        else
        {
            buttons.Add("stand", bindings.Text("games.enough"), "", "secondary", playing && round.pass, round.wait);
            buttons.Add("take", bindings.Text("games.take"), take ? "−" + Spaced(round.rate) : "", "primary", take,
                        playing ? bindings.Text("games.no_money") : round.wait);
        }
        bindings.On("take", "", [&bindings, pack = round.pack](const Event &) { bindings.Send(*pack, "press"); });
        bindings.On("stand", "", [&bindings, pass = round.pass](const Event &) {
            if (pass)
                bindings.Send(*pass, "press");
        });
        bindings.On("pack", "", [&bindings, pack = round.pack](const Event &) { bindings.Send(*pack, "press"); });
    }

    bool dice_;
    int round_money_ = -1; // our money after the stakes and the bank of the last round
    int round_bank_ = 0;
    bool round_open_ = false; // the table was open (between rounds) at the last build
};

} // namespace

std::unique_ptr<Screen> CreateDiceScreen()
{
    return std::make_unique<TableGameScreen>(true);
}

std::unique_ptr<Screen> CreateCardsScreen()
{
    return std::make_unique<TableGameScreen>(false);
}

} // namespace storm::ui::screens
