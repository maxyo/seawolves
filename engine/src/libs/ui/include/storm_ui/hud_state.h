// The HUD's state channels as the engine fills them: one struct per channel, handed to the UI host in the same
// process (Host::SetState(const HudState &)), only when it changed. JSON is their fixture format for tools and
// checks (Host::SetState(channel, json) reads it into these structs, src/hud/states.cpp).
//
// Ratios are 0…1 and a negative number means the engine does not know the value, unless a field says otherwise.
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <iterator>
#include <variant>
#include <vector>

namespace storm::ui::hud
{

// Channel "gameplay": the engine's mode and the controls bound in it.
struct Gameplay
{
    uint64_t revision = 0;
    std::string mode = "unknown"; // explore, melee, sailing, naval, worldmap, unknown
    // Mode "unknown" only: why (the engine's GameplaySnapshot::blocked): starting, paused,
    // loading, transition (the fader of a scene change), interface, dialogue, cutscene, video, commands, no-scene,
    // no-land-interface, no-player, group, error; empty before the first state.
    std::string blocked;
    std::string language;
    std::string input_owner; // land only: one player in one loaded scene
    bool analog_movement = false;
    uint64_t movement_generation = 0;
    std::set<std::string> capabilities;

    bool Has(const std::string &control) const
    {
        return capabilities.count(control) != 0;
    }
    bool operator==(const Gameplay &) const = default;
};

// Channel "combat": the land actions the engine offers and the pistol.
struct Combat
{
    bool known = false;
    uint64_t gameplay_revision = 0, activity = 0;
    std::string player, mode = "unknown", interact_kind = "interact"; // interact_kind: dialog | boarding | interact
    std::string interact_name;   // whom the talk button talks to ("" = the engine has no name)
    std::string interact_dialog; // the talk target's dialog file stem, lower case ("common_store"; "" = none)
    // Offered action -> its availability generation, which a command for it carries.
    std::map<std::string, uint64_t> actions;
    std::string pistol = "unknown"; // unknown, no_weapon, unloaded, loaded
    int pistol_progress = 0;        // 0…20 while unloaded

    bool Has(const std::string &action) const
    {
        return actions.count(action) != 0;
    }
    bool operator==(const Combat &) const = default;
};

// An active ability: in `ship` at sea, in `vitals` on land. Everything is the scripts' state; the HUD only shows it
// and launches it.
struct Ability
{
    std::string id;      // the perk / command name: ImmediateReload, LightRepair, InstantRepair, Turn180, Rush
    std::string label;   // the game's own name of it ("" = not given: the pack's name for the id)
    std::string state;   // ready, active, recovery, blocked
    double fill = 0.0;   // active: what is left of the effect; recovery: how far it recovered
    int left = 0;        // active / recovery: whole seconds the scripts still count
    std::string command; // the land menu event that launches it ("" = none: a sea ability)
    bool operator==(const Ability &) const = default;
};

// A picture of the player's own game files for c-game-image: a texture-namespace name and the
// rectangle {left, top, right, bottom} (0…1) of it. Empty texture = none.
struct Picture
{
    std::string texture;
    std::array<float, 4> uv{0.f, 0.f, 1.f, 1.f};
    bool operator==(const Picture &) const = default;
};

// Channel "vitals": the land hero's bars and loaded charges, and his status: effects, potions, officers, dash
// cooldown. known = false: the engine draws no land hero now.
struct Vitals
{
    struct Officer
    {
        int id = -1;
        std::string name, face;            // face: the game's FaceId
        Picture picture;                   // what the land interface draws for him (two.tga without a face)
        double health = 0.0, energy = 0.0; // 0…1
        bool operator==(const Officer &) const = default;
    };
    bool known = false;
    int hero = -1;                                   // the hero's character index (-1: not given)
    std::string name, face;                          // the hero's name and FaceId
    Picture picture;                                 // the hero's land interface picture
    double health = 0.0, energy = 0.0, reload = 0.0; // 0…1
    int loaded = 0, capacity = 0;
    std::vector<std::string> effects; // poison, drunk, healing, walkonly
    int potions = 0;                  // the game's potions carried
    std::vector<Officer> officers;
    double dash = -1.0;             // backstep cooldown left 0…1; -1 outside fight mode
    std::vector<Ability> abilities; // Rush, while the scripts offer it or it runs
    // The enemy captain of a boarding cabin fight while alive.
    std::string opponent;         // name; empty = none
    double opponent_health = 0.0; // 0…1
    // The boarding deck fight (not the cabin): each side's men now and at the fight's start.
    struct Boarding
    {
        int ours = 0, theirs = 0, ours_start = 0, theirs_start = 0;
        bool operator==(const Boarding &) const = default;
    };
    std::optional<Boarding> boarding;
    bool operator==(const Vitals &) const = default;
};

// Channel "worldInfo": the lines the engine prints as the location text ("\n" between them).
struct WorldInfo
{
    std::string text;
    bool operator==(const WorldInfo &) const = default;
};

// Channel "contextAction": the name of the script's quick action ("" = none).
struct ContextAction
{
    std::string name;
    bool operator==(const ContextAction &) const = default;
};

// Channel "reloadPlace": the door the hero stands at. `place` names where its reload locator leads, in the game's
// words (its LocLables), "" = at no door the game names; `locked`: the game keeps it closed now.
struct Door
{
    std::string place;
    bool locked = false;
    bool operator==(const Door &) const = default;
};

// An entry of a HUD menu (land commands, sea orders).
struct MenuItem
{
    std::string id, kind, label, source;
    std::string picture; // "<texture stem>:<cell>" of the game's picture, "" = not reported
    bool enabled = false;
    double cooldown = 0.0;
    bool operator==(const MenuItem &) const = default;
};

namespace detail
{
inline bool OneOf(const std::string &text, std::initializer_list<const char *> options)
{
    for (const char *option : options)
        if (text == option)
            return true;
    return false;
}
} // namespace detail

// What the land and the sea menus share: the engine's menu, drawn as a touch sheet of its items. known = false: no
// menu now.
struct Menu
{
    using Item = MenuItem;
    bool known = false;
    uint64_t scene = 0, revision = 0;
    bool active = false, available = false, can_back = false, can_close = false;
    bool complete = false; // everything is known and drawable; otherwise the engine keeps its own menu
    std::string stage = "commands", title, selected; // stage: the land's or the sea's (LandCommands, SeaOrders)
    std::vector<Item> items;

    const Item *Find(const std::string &id) const
    {
        for (const Item &item : items)
            if (item.id == id)
                return &item;
        return nullptr;
    }
    bool operator==(const Menu &) const = default;
};

// Channel "landCommands": the engine's land action menu; stage: commands, travel, potions, abilities.
struct LandCommands : Menu
{
    // The engine's answer to the last command of the menu (and the last few): what it did with it.
    struct Activation
    {
        uint64_t serial = 0, scene = 0, revision = 0;
        std::string id, reason;
        bool accepted = false;
        bool operator==(const Activation &) const = default;
    } activation;
    std::vector<Activation> activations;
    std::vector<std::string> offered; // root command events the scripts enable now, open or not
    // The travel stage's place the hero stands in (the game leaves it out of the list): its picture as
    // Item::picture and its name; "" = none.
    std::string here_picture, here_label;

    // What the touch sheet can draw truthfully: items of the kinds it knows, every command with its script source,
    // and, open, Back, Close and exactly one cancel item. Otherwise the menu is not `complete`.
    bool Drawable() const
    {
        int cancels = 0;
        for (const Item &item : items)
        {
            const bool cancel = item.kind == "cancel";
            cancels += cancel ? 1 : 0;
            if (!detail::OneOf(item.kind, {"command", "ability", "travel", "potion", "cancel"}) ||
                (!cancel && item.source.find_first_not_of(' ') == std::string::npos))
                return false;
        }
        return !active || (can_back && can_close && cancels == 1);
    }
    bool operator==(const LandCommands &) const = default;
};

// Channel "ship": the player's ship at sea, its companions and what the engine's own sea indicators read.
// known = false: the engine has no sea scene to show (the revisions still count).
struct Ship
{
    struct Vessel
    {
        int character = -1;
        std::string entity, name, type;
        int ship_class = 0;
        double hull = -1.0, crew = -1.0, sails = -1.0;
        Picture picture; // the battle interface's ship icon
        bool operator==(const Vessel &) const = default;
    };
    bool known = false;
    uint64_t gameplay_revision = 0, sequence = 0, hud_revision = 0;
    Vessel own;
    std::vector<Vessel> fleet; // companions, the flagship is `own`
    bool captions = false;     // the engine draws its text readouts now: ours may replace them
    bool navigator = false, zoom_in = false, zoom_out = false;
    uint64_t navigator_generation = 0, navigator_layout = 0; // layout = the aperture the engine drew last
    double speed = -1.0, wind = -1.0, cannon_charge = -1.0;
    double time_scale = -1.0; // the world's time scale the scripts set (x1 = 1)
    // Battle abilities (instant repair, immediate reload) the ship's people can use.
    std::vector<Ability> abilities; // ImmediateReload, LightRepair, InstantRepair, Turn180
    std::string date_time, location;
    std::string ammo; // balls, grapes, knippels, bombs; "" = unknown
    double ammo_count = -1.0;
    bool ammo_low = false;                            // the engine's own "not enough shot" warning
    bool powder_out = false;                          // … and its "no powder" warning
    double ammo_counts[4] = {-1.0, -1.0, -1.0, -1.0}; // by kind, in the order above
    double sail_state = -1.0;                         // 0, 0.5, 1
    bool can_set_sails = false;
    bool operator==(const Ship &) const = default;
};

// Channel "seaOrders": the engine's sea command menu (orders to the own ship and companions); stage: commands,
// options, targets.
struct SeaOrders : Menu
{
    struct Recipient
    {
        std::string id, name, entity;
        int character = -1;
        bool main = false;
        bool operator==(const Recipient &) const = default;
    };
    std::string recipient;
    std::vector<Recipient> recipients;

    // What the touch sheet can draw truthfully: items of the kinds it knows, at most one cancel item and, open, Back
    // and Close. Otherwise the menu is not `complete`.
    bool Drawable() const
    {
        int cancels = 0;
        for (const Item &item : items)
        {
            cancels += item.kind == "cancel" ? 1 : 0;
            if (!detail::OneOf(item.kind,
                               {"command", "charge", "option", "ability", "ship", "fort", "land", "town", "cancel"}))
                return false;
        }
        return cancels <= 1 && (!active || (can_back && can_close));
    }
    bool operator==(const SeaOrders &) const = default;
};

// Channel "seaQuickAction": the one quick action the engine offers at sea.
struct SeaQuick
{
    bool known = false;
    uint64_t revision = 0;
    bool available = false;
    std::string action, label; // Map, Moor, Board, LandTroops, Reload; the engine's caption
    bool map = false;          // the world map can be entered too, beside `action`
    std::string map_label;     // the engine's caption of Map
    bool operator==(const SeaQuick &) const = default;
};

// Channel "worldMap": the world map's instruments, fleet and commands. Absent numbers are
// negative / empty; angles are radians as the engine measures them.
struct WorldMap
{
    struct Vessel
    {
        std::string id, name, type;
        double hull = -1.0, sails = -1.0; // 0…100
        double crew = -1.0;               // 0…1 of the ship's crew capacity
        Picture picture;                  // the map sign's ship icon
        bool operator==(const Vessel &) const = default;
    };
    struct Command
    {
        std::string id, label; // EnterToSea, EnterToIsland, EnterToShip, EnterToStorm, EnterToAttack, EnterToEnemy
        bool operator==(const Command &) const = default;
    };
    bool known = false, available = false;
    uint64_t revision = 0, activity = 0, gameplay_revision = 0;
    unsigned mask = 0; // what the engine lets a touch HUD replace (instruments 1, ships 2, commands 4)
    bool instruments = false, ships_visible = false, commands_ready = false;
    std::string date, coordinates;
    double hour = -1.0, wind_force = -1.0, morale = -1.0, speed = -1.0;
    double time_scale = -1.0; // the world's time scale the scripts set (x1 = 1)
    double wind_angle = 0.0, heading = 0.0;
    bool angles = false; // wind_angle and heading are known
    int food_days = -1, rum_days = -1, flag = -1, storm = -1;
    int moon_phase = -1;      // 0 new … 4 full … 7 (the game's moon textures, day / 4)
    int dawn = -1, dusk = -1; // the hours night ends and begins in the game's weathers
    std::vector<Vessel> fleet;
    std::vector<Command> commands;
    bool operator==(const WorldMap &) const = default;
};

// Channel "presentation": what covers the game (an interface, the loading screen, a cinematic, a
// dialogue) and whether the player can act at all. The engine's host composes it from the engine's
// facts and the platform's `live`.
struct Presentation
{
    bool known = false;
    std::string screen; // interface id open in the game ("" = none), ported or not
    bool loading = false, cinematic = false, dialogue = false;
    bool live = false; // the game is in front and running: input may be sent
    bool operator==(const Presentation &) const = default;
};

// Channel "spyglass": the telescope and what it shows of its target. A field the telescope cannot show at its
// level is -1 / empty.
struct Spyglass
{
    bool known = false, on = false, target = false;
    std::string kind, name, type, captain, cannon_type, portrait; // kind: ship | fort
    int ship_class = -1, hull = -1, sails = -1, crew = -1, cannons = -1, cannons_max = -1, charge = -1, nation = -1,
        sail = -1;
    double speed = -1.0, distance = -1.0;
    int skills[5] = {-1, -1, -1, -1, -1}; // fencing, cannons, accuracy, navigation, boarding
    bool operator==(const Spyglass &) const = default;
};

} // namespace storm::ui::hud

namespace storm::ui
{
// One state of a HUD channel; the channel is the type.
using HudState = std::variant<hud::Gameplay, hud::Combat, hud::Vitals, hud::WorldInfo, hud::ContextAction, hud::Door,
                              hud::LandCommands, hud::Ship, hud::SeaOrders, hud::SeaQuick, hud::WorldMap,
                              hud::Presentation, hud::Spyglass>;
// The channel's name in the fixture format, by the index of the state's type.
inline const char *HudChannel(const HudState &state)
{
    static constexpr const char *kNames[] = {"gameplay",    "combat",       "vitals",  "worldInfo", "contextAction",
                                             "reloadPlace", "landCommands", "ship",    "seaOrders", "seaQuickAction",
                                             "worldMap",    "presentation", "spyglass"};
    static_assert(std::size(kNames) == std::variant_size_v<HudState>);
    return kNames[state.index()];
}
} // namespace storm::ui
