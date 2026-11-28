// What the HUD asks of the engine: one struct per command, handed to the engine in the same process with the
// action that produced it (Action::command). The revisions in them are those of the state the HUD drew the
// control from (hud_state.h): a tap on a HUD the engine has changed since does not act. A command with a
// `request` number (the UI's request ledger, src/requests.h) is answered by the engine: applied with the revision
// of the state that shows it, or refused; the HUD holds the tapped choice until then. Continuous controls (sticks,
// held buttons) are not requests.
//
// to_string gives each one line of text for logs, captures and checks ("hold:9\tShip_TurnLeft\t1\t40").
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace storm::ui::hud
{

// A control the gameplay binds, activated once (gameplay revision).
struct GameplayControl
{
    uint64_t revision = 0;
    std::string control;
    bool operator==(const GameplayControl &) const = default;
};

// A control held until its release; `strength` 1…100 is the analog part (0: a button).
struct Hold
{
    uint64_t revision = 0;
    std::string control;
    bool down = false;
    int strength = 0;
    bool operator==(const Hold &) const = default;
};

// A land combat action: `activity` is the combat channel's input session, `generation` the action's
// availability in it. Phase: tap, down, up; action "all" with phase "cancel" lets go of everything.
struct CombatAction
{
    uint64_t activity = 0, generation = 0;
    std::string action, phase;
    bool operator==(const CombatAction &) const = default;
};

// The land walk stick of the gameplay's input owner and movement generation: -1000…1000 each way.
struct Move
{
    std::string owner;
    uint64_t generation = 0;
    bool active = false;
    int x = 0, y = 0;
    bool operator==(const Move &) const = default;
};

// The walk stick on deck (gameplay revision), -100…100 each way; 0, 0 ends the walk.
struct DeckWalk
{
    uint64_t revision = 0;
    int x = 0, y = 0;
    bool operator==(const DeckWalk &) const = default;
};

// A step in a HUD menu (land commands, sea orders) of the menu's revision: open, selectRecipient, selectItem,
// activateItem, back, close; at sea also "shown" (id "1" or "0": the sheet is drawn, or not any more).
struct MenuStep
{
    enum class Menu
    {
        Land,
        Sea
    };
    Menu menu = Menu::Land;
    uint64_t revision = 0;
    std::string verb, id;
    uint64_t request = 0;
    bool operator==(const MenuStep &) const = default;
};

// The sea quick action of the offer's revision: its action, or Map beside it.
struct SeaQuickTap
{
    uint64_t revision = 0;
    std::string action;
    bool operator==(const SeaQuickTap &) const = default;
};

// A world map command of the map's revision.
struct MapCommand
{
    uint64_t revision = 0;
    std::string id;
    uint64_t request = 0;
    bool operator==(const MapCommand &) const = default;
};

// The box the HUD's compass covers on the world map, in the display's pixels: display width and height,
// left, top, width, height; nothing = no compass.
struct MapLabelArea
{
    uint64_t revision = 0;
    std::optional<std::array<uint32_t, 6>> area;
    bool operator==(const MapLabelArea &) const = default;
};

// The sail position 0, 1, 2 (gameplay revision).
struct Sails
{
    uint64_t revision = 0;
    int position = 0;
    uint64_t request = 0;
    bool operator==(const Sails &) const = default;
};

// The engine's radar in the minimap disc, for the ship channel's gameplay revision, HUD revision and navigator
// generation. Zoom in/out; prepare: the disc of layout `layout` (display width, height, centre x, y, radius,
// density x1000, russian 1/0); visible: our readouts are on the screen around layout `layout`; clear: none.
struct Navigator
{
    enum class Verb
    {
        In,
        Out,
        Prepare,
        Visible,
        Clear
    };
    uint64_t revision = 0, hud_revision = 0, generation = 0;
    Verb verb = Verb::Clear;
    uint64_t layout = 0;
    std::array<long, 7> geometry{};
    bool operator==(const Navigator &) const = default;
};

// The script's quick action on land by its name.
struct Context
{
    std::string name;
    bool operator==(const Context &) const = default;
};

// A HUD the touch UI presents ("land", "sea", "worldmap", "spyglass").
struct Presenter
{
    std::string name;
    bool operator==(const Presenter &) const = default;
};

// The game is in a dialogue that no screen shows: the engine ends it (the report says what was seen).
struct DialogueLost
{
    std::string report;
    bool operator==(const DialogueLost &) const = default;
};

// For the platform host, not the engine: open a game menu ("mainmenu", "character", …) through the host's
// shortcut path, and whether the HUD is on the screen ("shown", "menu", "hidden").
struct Shortcut
{
    std::string name;
    bool operator==(const Shortcut &) const = default;
};
struct Presence
{
    std::string state;
    bool operator==(const Presence &) const = default;
};

using Command = std::variant<GameplayControl, Hold, CombatAction, Move, DeckWalk, MenuStep, SeaQuickTap, MapCommand,
                             MapLabelArea, Sails, Navigator, Context, Presenter, DialogueLost, Shortcut, Presence>;

// "<verb>:<fields, tab-separated>": the command as logs and checks show it.
inline std::string to_string(const Command &command)
{
    const auto n = [](auto value) { return std::to_string(value); };
    return std::visit(
        [&](const auto &c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, GameplayControl>)
                return "gameplay:" + n(c.revision) + "\t" + c.control;
            else if constexpr (std::is_same_v<T, Hold>)
                return "hold:" + n(c.revision) + "\t" + c.control + "\t" + (c.down ? "1" : "0") +
                       (c.down && c.strength ? "\t" + n(c.strength) : std::string());
            else if constexpr (std::is_same_v<T, CombatAction>)
                return "combat:" + n(c.activity) + "\t" + n(c.generation) + "\t" + c.action + "\t" + c.phase;
            else if constexpr (std::is_same_v<T, Move>)
                return "move:" + c.owner + "\t" + n(c.generation) + "\t" + (c.active ? "1" : "0") + "\t" + n(c.x) +
                       "\t" + n(c.y);
            else if constexpr (std::is_same_v<T, DeckWalk>)
                return "deckwalk:" + n(c.revision) + "\t" + n(c.x) + "\t" + n(c.y);
            else if constexpr (std::is_same_v<T, MenuStep>)
                return std::string(c.menu == MenuStep::Menu::Land ? "landcommands:" : "orders:") + n(c.revision) +
                       "\t" + c.verb + "\t" + c.id;
            else if constexpr (std::is_same_v<T, SeaQuickTap>)
                return "seaquick:" + n(c.revision) + "\t" + c.action;
            else if constexpr (std::is_same_v<T, MapCommand>)
                return "worldmap:" + n(c.revision) + "\tactivate\t" + c.id;
            else if constexpr (std::is_same_v<T, MapLabelArea>)
            {
                std::string area = c.area ? std::string() : std::string("none");
                if (c.area)
                    for (size_t i = 0; i < c.area->size(); ++i)
                        area += (i ? ":" : "") + n((*c.area)[i]);
                return "worldmap:" + n(c.revision) + "\tlabelarea\t" + area;
            }
            else if constexpr (std::is_same_v<T, Sails>)
                return "sails:" + n(c.revision) + "\t" + n(c.position);
            else if constexpr (std::is_same_v<T, Navigator>)
            {
                static constexpr const char *kVerbs[] = {"in", "out", "prepare", "visible", "clear"};
                std::string out = "navigator:" + n(c.revision) + "\t" + n(c.hud_revision) + "\t" + n(c.generation) +
                                  "\t" + kVerbs[static_cast<int>(c.verb)];
                if (c.verb == Navigator::Verb::Prepare || c.verb == Navigator::Verb::Visible)
                    out += "\t" + n(c.layout);
                if (c.verb == Navigator::Verb::Prepare)
                    for (const long value : c.geometry)
                        out += "\t" + n(value);
                return out;
            }
            else if constexpr (std::is_same_v<T, Context>)
                return "context:" + c.name;
            else if constexpr (std::is_same_v<T, Presenter>)
                return "hudowner:" + c.name;
            else if constexpr (std::is_same_v<T, DialogueLost>)
                return "dialogue-lost:" + c.report;
            else if constexpr (std::is_same_v<T, Shortcut>)
                return "shortcut:" + c.name;
            else
                return "presence:" + c.state;
        },
        command);
}

} // namespace storm::ui::hud
