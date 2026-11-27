// The HUD's state channels: the typed states the engine hands over (Hud::Set), and their fixture
// format (JSON), read into the same structs for tools and checks (Host::SetState(channel, json)).
// A fixture that does not have its channel's shape is rejected aloud and leaves that channel
// without data (its controls disappear), never half-read.
#include "hud.h"
#include "app_ui_names.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <tuple>

namespace storm::ui::hud
{

void Hud::Set(const HudState &state)
{
    States &states = Edit();
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Gameplay>)
                states.gameplay = value;
            else if constexpr (std::is_same_v<T, Combat>)
                states.combat = value;
            else if constexpr (std::is_same_v<T, Vitals>)
                states.vitals = value;
            else if constexpr (std::is_same_v<T, WorldInfo>)
                states.world_info = value.text;
            else if constexpr (std::is_same_v<T, ContextAction>)
                states.context_action = value.name;
            else if constexpr (std::is_same_v<T, Door>)
                states.door = value;
            else if constexpr (std::is_same_v<T, LandCommands>)
                states.land_commands = value;
            else if constexpr (std::is_same_v<T, Ship>)
                states.ship = value;
            else if constexpr (std::is_same_v<T, SeaOrders>)
                states.sea_orders = value;
            else if constexpr (std::is_same_v<T, SeaQuick>)
                states.sea_quick = value;
            else if constexpr (std::is_same_v<T, WorldMap>)
                states.world_map = value;
            else if constexpr (std::is_same_v<T, Presentation>)
                states.presentation = value;
            else
                states.spyglass = value;
        },
        state);
}

namespace
{

using screens::Router;
using screens::StateConsumer;

std::optional<uint64_t> Whole(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number())
        return std::nullopt;
    const double number = found->get<double>();
    if (!std::isfinite(number) || number < 0.0 || number != std::floor(number))
        return std::nullopt;
    return static_cast<uint64_t>(number);
}

std::optional<std::string> Text(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return std::nullopt;
    return found->get<std::string>();
}

std::optional<bool> Flag(const Json &object, const char *key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_boolean())
        return std::nullopt;
    return found->get<bool>();
}

using detail::OneOf;

// A finite number that is not negative; negative when the key is absent or something else.
double Amount(const Json &object, const char *key)
{
    const auto found = object.find(key);
    const double number = found != object.end() && found->is_number() ? found->get<double>() : -1.0;
    return std::isfinite(number) && number >= 0.0 ? number : -1.0;
}

double Ratio(const Json &object, const char *key)
{
    const auto found = object.find(key);
    const double number = found != object.end() && found->is_number() ? found->get<double>() : 0.0;
    return std::isfinite(number) ? std::clamp(number, 0.0, 1.0) : 0.0;
}

// The optional "abilities" list of a channel (ship, vitals): the known ids, each with a known state; label and
// left are optional.
std::string ReadAbilities(const Json &state, std::initializer_list<const char *> ids, std::vector<Ability> &out)
{
    out.clear();
    const auto abilities = state.find("abilities");
    if (abilities == state.end())
        return {};
    if (!abilities->is_array())
        return "abilities is not a list";
    for (const Json &source : *abilities)
    {
        if (!source.is_object())
            return "an ability is not an object";
        Ability ability;
        ability.id = Text(source, "id").value_or("");
        ability.label = Text(source, "label").value_or("");
        ability.state = Text(source, "state").value_or("");
        ability.fill = Ratio(source, "fill");
        ability.left = static_cast<int>(Whole(source, "left").value_or(0));
        if (!OneOf(ability.id, ids) || !OneOf(ability.state, {"ready", "active", "recovery", "blocked"}))
            return "an unknown ability or ability state";
        out.push_back(std::move(ability));
    }
    return {};
}

std::string ReadPicture(const Json &value, Picture &out)
{
    const auto texture = value.is_object() ? value.find("texture") : value.end();
    const auto uv = value.is_object() ? value.find("uv") : value.end();
    if (texture == value.end() || !texture->is_string() || uv == value.end() || !uv->is_array() || uv->size() != 4)
        return "picture: expected {texture, uv: [left, top, right, bottom]}";
    Picture read{texture->get<std::string>()};
    for (size_t i = 0; i < 4; ++i)
    {
        if (!(*uv)[i].is_number())
            return "picture: uv is not four numbers";
        read.uv[i] = (*uv)[i].get<float>();
    }
    out = std::move(read);
    return {};
}

std::string ReadGameplay(const Json &state, Gameplay &next)
{
    const auto revision = state.is_object() ? Whole(state, "revision") : std::nullopt;
    const auto mode = state.is_object() ? Text(state, "mode") : std::nullopt;
    if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1} || !revision || *revision == 0 || !mode)
        return "expected {schemaVersion: 1, revision > 0, mode}";
    next.revision = *revision;
    next.language = Text(state, "language").value_or("");
    // A mode this runtime does not know is no gameplay it can draw: the host keeps it.
    if (OneOf(*mode, {"explore", "melee", "sailing", "naval", "worldmap"}))
    {
        next.mode = *mode;
        if (const auto capabilities = state.find("capabilities");
            capabilities != state.end() && capabilities->is_array())
            for (const Json &name : *capabilities)
                if (name.is_string())
                    next.capabilities.insert(name.get<std::string>());
    }
    std::string problem;
    if (next.mode != *mode && *mode != "unknown")
        problem = "unknown gameplay mode '" + *mode + "'";
    if (*mode == "unknown")
    {
        next.blocked = Text(state, "blocked").value_or("");
        if (!OneOf(next.blocked, {"starting", "paused", "loading", "transition", "interface", "dialogue", "cutscene",
                                  "video", "commands", "no-scene", "no-land-interface", "no-player", "group", "error"}))
            problem = "gameplay mode unknown without a known reason (blocked '" + next.blocked + "')";
    }
    if (next.mode == "explore" || next.mode == "melee")
    {
        next.input_owner = Text(state, "inputOwner").value_or("");
        const auto generation = Whole(state, "movementGeneration");
        next.analog_movement =
            Flag(state, "analogMovement").value_or(false) && !next.input_owner.empty() && generation && *generation > 0;
        next.movement_generation = next.analog_movement ? *generation : 0;
    }
    return problem;
}

std::string ReadCombat(const Json &state, Combat &out)
{
    if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{2})
        return "expected {schemaVersion: 2, …}";
    const auto revision = Whole(state, "gameplayRevision"), activity = Whole(state, "activityGeneration");
    const auto player = Text(state, "playerId"), mode = Text(state, "mode");
    const auto actions = state.find("actions");
    if (!revision || !activity || !player || !mode || actions == state.end() || !actions->is_object())
        return "lacks gameplayRevision, activityGeneration, playerId, mode or actions";
    if (!OneOf(*mode, {"unknown", "explore", "melee"}))
        return "bad mode";
    for (const auto &entry : actions->items())
    {
        const std::string &name = entry.key();
        if (!OneOf(name, {"weapon", "potion", "force", "fast", "break", "parry", "feint", "round", "pistol", "block",
                          "freelook", "interact", "dash"}))
            return "unknown action '" + name + "'";
        const auto generation = Whole(*actions, name.c_str());
        if (!generation || *generation == 0)
            return "action '" + name + "' lacks its generation";
        out.actions[name] = *generation;
        if (*mode == "explore" && !OneOf(name, {"weapon", "potion", "freelook", "interact"}))
            return "action '" + name + "' does not belong to explore";
        if (*mode != "explore" && name == "interact")
            return "interact outside explore";
    }
    if (*mode != "unknown" ? (player->empty() || *activity == 0) : !out.actions.empty())
        return "mode and owner do not agree";
    if (const auto pistol = state.find("pistol"); pistol != state.end())
    {
        const auto kind = pistol->is_object() ? Text(*pistol, "state") : std::nullopt;
        const auto progress = pistol->is_object() ? Whole(*pistol, "progress") : std::nullopt;
        if (!kind || !progress || !OneOf(*kind, {"unknown", "no_weapon", "unloaded", "loaded"}) || *progress > 20)
            return "bad pistol";
        if ((*kind == "loaded" && *progress != 20) || (*kind == "unloaded" && *progress >= 20) ||
            ((*kind == "unknown" || *kind == "no_weapon") && *progress != 0))
            return "pistol state and progress do not agree";
        out.pistol = *kind;
        out.pistol_progress = static_cast<int>(*progress);
    }
    out.known = true;
    out.gameplay_revision = *revision;
    out.activity = *activity;
    out.player = *player;
    out.mode = *mode;
    const std::string kind = Text(state, "interactKind").value_or("");
    out.interact_kind = kind == "dialog" || kind == "boarding" ? kind : "interact";
    out.interact_name = Text(state, "interactName").value_or("");
    out.interact_dialog = Text(state, "interactDialog").value_or("");
    return {};
}

// null = the engine draws no land hero bars now.
std::string ReadVitals(const Json &state, Vitals &next)
{
    if (state.is_null())
        return {};
    if (!state.is_object())
        return "expected an object or null";
    next.known = true;
    next.health = Ratio(state, "health");
    next.energy = Ratio(state, "energy");
    next.reload = Ratio(state, "reload");
    next.capacity = static_cast<int>(Whole(state, "capacity").value_or(0));
    next.loaded = std::min(static_cast<int>(Whole(state, "loaded").value_or(0)), next.capacity);
    // The hero of the party row: character index, name and FaceId, all or none.
    if (state.contains("id") || state.contains("name") || state.contains("face"))
    {
        const auto id = Whole(state, "id");
        const auto name = Text(state, "name"), face = Text(state, "face");
        if (!id || !name || !face)
            return "id, name, face: expected the hero's character index, name and FaceId";
        next.hero = static_cast<int>(*id);
        next.name = *name;
        next.face = *face;
        if (const auto found = state.find("picture"); found != state.end() && !found->is_null())
            if (std::string problem = ReadPicture(*found, next.picture); !problem.empty())
                return "hero " + problem;
    }
    if (const auto effects = state.find("effects"); effects != state.end())
    {
        if (!effects->is_array())
            return "effects: expected an array";
        for (const Json &effect : *effects)
        {
            if (!effect.is_string() ||
                !OneOf(effect.get<std::string>(), {"poison", "drunk", "healing", "overload", "cantrun"}))
                return "effects: unknown effect " + effect.dump();
            next.effects.push_back(effect.get<std::string>());
        }
    }
    if (state.contains("potions"))
    {
        const auto potions = Whole(state, "potions");
        if (!potions)
            return "potions: expected a whole number";
        next.potions = static_cast<int>(*potions);
    }
    if (const auto officers = state.find("officers"); officers != state.end())
    {
        if (!officers->is_array())
            return "officers: expected an array";
        for (const Json &officer : *officers)
        {
            const auto id = officer.is_object() ? Whole(officer, "id") : std::nullopt;
            if (!id || !officer.contains("name") || !officer["name"].is_string() || !officer.contains("face") ||
                !officer["face"].is_string() || !officer.contains("health") || !officer["health"].is_number() ||
                !officer.contains("energy") || !officer["energy"].is_number())
                return "officers: expected {id, name, face, health, energy}";
            Picture picture;
            if (const auto found = officer.find("picture"); found != officer.end() && !found->is_null())
                if (std::string problem = ReadPicture(*found, picture); !problem.empty())
                    return "officers: " + problem;
            next.officers.push_back({static_cast<int>(*id), officer["name"].get<std::string>(),
                                     officer["face"].get<std::string>(), std::move(picture), Ratio(officer, "health"),
                                     Ratio(officer, "energy")});
        }
    }
    if (const auto dash = state.find("dash"); dash != state.end())
    {
        if (!dash->is_number())
            return "dash: expected a number";
        next.dash = Ratio(state, "dash");
    }
    if (std::string problem = ReadAbilities(state, {"Rush"}, next.abilities); !problem.empty())
        return problem;
    if (const auto opponent = state.find("opponent"); opponent != state.end())
    {
        if (!opponent->is_object() || !opponent->contains("name") || !(*opponent)["name"].is_string() ||
            (*opponent)["name"].get<std::string>().empty() || !opponent->contains("health") ||
            !(*opponent)["health"].is_number())
            return "opponent: expected {name, health}";
        next.opponent = (*opponent)["name"].get<std::string>();
        next.opponent_health = Ratio(*opponent, "health");
    }
    if (const auto boarding = state.find("boarding"); boarding != state.end())
    {
        const auto count = [&](const char *key) {
            const auto found = boarding->find(key);
            return found != boarding->end() && found->is_number_integer() && found->get<int64_t>() >= 0
                       ? found->get<int>()
                       : -1;
        };
        if (!boarding->is_object())
            return "boarding: expected {ours, theirs, oursStart, theirsStart}";
        Vitals::Boarding read{count("ours"), count("theirs"), count("oursStart"), count("theirsStart")};
        if (read.ours < 0 || read.theirs < 0 || read.ours_start <= 0 || read.theirs_start <= 0)
            return "boarding: expected {ours, theirs, oursStart, theirsStart} as counts of men, the starts above 0";
        next.boarding = read;
    }
    return {};
}

std::string ReadDoor(const Json &state, Door &door)
{
    if (state.is_null())
        return {};
    const auto place = state.is_object() ? Text(state, "place") : std::nullopt;
    const auto locked = state.is_object() ? Flag(state, "locked") : std::nullopt;
    if (!place || !locked)
        return "expected null or {place, locked: bool}";
    door = Door{*place, *locked};
    return {};
}

// The items of a HUD menu fixture; `source` may be left out where `needs_source` is false.
std::string ReadItems(const Json &items, bool needs_source, std::vector<MenuItem> &out)
{
    std::set<std::string> ids;
    for (const Json &source : items)
    {
        MenuItem item;
        const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
        const auto kind = source.is_object() ? Text(source, "kind") : std::nullopt;
        const auto label = source.is_object() ? Text(source, "label") : std::nullopt;
        const auto origin = source.is_object() ? Text(source, "source") : std::nullopt;
        const auto enabled = source.is_object() ? Flag(source, "enabled") : std::nullopt;
        const auto cooldown = source.is_object() ? source.find("cooldown") : Json::const_iterator();
        if (!id || !kind || !label || (needs_source && !origin) || !enabled || cooldown == source.end() ||
            !cooldown->is_number())
            return "an item lacks id, kind, label, source, enabled or cooldown";
        if (!ids.insert(*id).second)
            return "item id '" + *id + "' is listed twice";
        item.id = *id;
        item.kind = *kind;
        item.label = *label;
        item.source = origin.value_or("");
        item.picture = Text(source, "picture").value_or("");
        item.enabled = *enabled;
        item.cooldown = std::clamp(cooldown->get<double>(), 0.0, 1.0);
        out.push_back(std::move(item));
    }
    return {};
}

// The fields both menus have (a non-null object of schema 1); `complete` stays the engine's word, Drawable decides.
std::string ReadMenu(const Json &state, std::initializer_list<const char *> stages, bool needs_source, Menu &out)
{
    if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1})
        return "expected {schemaVersion: 1, …}";
    const auto scene = Whole(state, "sceneEpoch"), revision = Whole(state, "revision");
    const auto active = Flag(state, "active"), available = Flag(state, "available"), back = Flag(state, "canBack"),
               close = Flag(state, "canClose");
    const auto stage = Text(state, "stage"), selected = Text(state, "selectedItemId"),
               title = Text(state, "pendingLabel");
    const auto items = state.find("items");
    if (!scene || !revision || !active || !available || !back || !close || !stage || !selected || !title ||
        items == state.end() || !items->is_array())
        return "a required field is missing or mistyped";
    if (!OneOf(*stage, stages))
        return "unknown stage '" + *stage + "'";
    if (std::string problem = ReadItems(*items, needs_source, out.items); !problem.empty())
        return problem;
    if (!selected->empty() && !out.Find(*selected))
        return "selectedItemId names no item";
    out.known = true;
    out.scene = *scene;
    out.revision = *revision;
    out.active = *active;
    out.available = *available;
    out.can_back = *back;
    out.can_close = *close;
    out.stage = *stage;
    out.title = *title;
    out.selected = *selected;
    out.complete = Flag(state, "complete").value_or(false);
    return {};
}

std::string ReadLandCommands(const Json &state, LandCommands &out)
{
    if (state.is_null()) // the engine has no land menu now
        return {};
    if (std::string problem = ReadMenu(state, {"commands", "travel", "potions", "abilities"}, true, out);
        !problem.empty())
        return problem;
    const auto offered = state.find("offered");
    if (offered == state.end() || !offered->is_array())
        return "a required field is missing or mistyped";
    const auto readReceipt = [](const Json &receipt, LandCommands::Activation &out) {
        if (!receipt.is_object())
            return false;
        const auto serial = Whole(receipt, "serial"), scene = Whole(receipt, "scene"),
                   revision = Whole(receipt, "revision");
        const auto id = Text(receipt, "id"), reason = Text(receipt, "reason");
        const auto accepted = Flag(receipt, "accepted");
        if (!serial || !scene || !revision || !id || !reason || !accepted)
            return false;
        out = {*serial, *scene, *revision, *id, *reason, *accepted};
        return true;
    };
    if (const auto receipt = state.find("activation"); receipt != state.end() && !readReceipt(*receipt, out.activation))
        return "bad activation receipt";
    if (const auto receipts = state.find("activations"); receipts != state.end())
    {
        if (!receipts->is_array())
            return "bad activation history";
        for (const auto &value : *receipts)
        {
            LandCommands::Activation receipt;
            if (!readReceipt(value, receipt))
                return "bad activation history receipt";
            out.activations.push_back(std::move(receipt));
        }
    }
    for (const Json &event : *offered)
    {
        if (!event.is_string())
            return "offered holds a bad command event";
        out.offered.push_back(event.get<std::string>());
    }
    if (const auto here = state.find("here"); here != state.end() && !here->is_null())
    {
        const auto picture = here->is_object() ? Text(*here, "picture") : std::nullopt;
        const auto label = here->is_object() ? Text(*here, "label") : std::nullopt;
        if (!picture || !label)
            return "here is not {picture, label}";
        out.here_picture = *picture;
        out.here_label = *label;
    }
    out.complete = out.complete && out.Drawable();
    return {};
}

// current / maximum as a share; negative when the fixture does not give the pair.
double Share(const Json &object, const char *key, const char *maximum)
{
    const double value = Amount(object, key), limit = Amount(object, maximum);
    return value < 0.0 || limit <= 0.0 ? -1.0 : std::min(1.0, value / limit);
}

std::string ReadVessel(const Json &object, Ship::Vessel &out)
{
    const auto character = object.find("characterIndex");
    const auto entity = Text(object, "entityId");
    if (character == object.end() || !character->is_number_integer() || !entity)
        return "a ship lacks characterIndex or entityId";
    out.character = character->get<int>();
    out.entity = *entity;
    out.name = Text(object, "name").value_or("");
    out.type = Text(object, "type").value_or("");
    out.ship_class = static_cast<int>(Whole(object, "shipClass").value_or(0));
    out.hull = Share(object, "hull", "hullMax");
    out.crew = Share(object, "crew", "crewMax");
    out.sails = Share(object, "sailIntegrity", "sailIntegrityMax");
    if (const auto picture = object.find("picture"); picture != object.end())
        return ReadPicture(*picture, out.picture);
    return {};
}

// `valid: false` = the engine has no sea scene to show.
std::string ReadShip(const Json &state, Ship &out)
{
    if (!state.is_object())
        return "expected an object, got " + std::string(state.type_name());
    if (Whole(state, "schemaVersion") != uint64_t{1})
        return "expected {schemaVersion: 1, …}";
    const auto revision = Whole(state, "gameplayRevision"), sequence = Whole(state, "sequence"),
               hud = Whole(state, "hudRevision");
    const auto valid = Flag(state, "valid");
    if (!revision || !sequence || !hud || !valid)
        return "lacks gameplayRevision, sequence, hudRevision or valid";
    out.gameplay_revision = *revision;
    out.sequence = *sequence;
    out.hud_revision = *hud;
    if (!*valid)
        return {};
    if (std::string problem = ReadVessel(state, out.own); !problem.empty())
        return problem;
    const auto fleet = state.find("fleet");
    if (fleet == state.end() || !fleet->is_array())
        return "lacks the fleet list";
    for (const Json &source : *fleet)
    {
        Ship::Vessel vessel;
        if (std::string problem = source.is_object() ? ReadVessel(source, vessel) : "a fleet entry is not an object";
            !problem.empty())
            return problem;
        out.fleet.push_back(std::move(vessel));
    }
    out.captions = Flag(state, "captionsVisible").value_or(false);
    out.navigator = Flag(state, "navigatorAvailable").value_or(false);
    out.zoom_in = Flag(state, "navigatorZoomIn").value_or(false);
    out.zoom_out = Flag(state, "navigatorZoomOut").value_or(false);
    out.navigator_generation = Whole(state, "navigatorGeneration").value_or(0);
    out.navigator_layout = Whole(state, "navigatorLayoutId").value_or(0);
    // The speed may be negative (a ship making sternway): any finite number.
    const auto speed = state.find("shipSpeedKnots");
    out.speed = speed != state.end() && speed->is_number() && std::isfinite(speed->get<double>())
                    ? std::abs(speed->get<double>())
                    : -1.0;
    out.wind = Amount(state, "windSpeed");
    out.cannon_charge = Amount(state, "cannonCharge") < 0.0 ? -1.0 : std::min(1.0, Amount(state, "cannonCharge"));
    out.time_scale = Amount(state, "timeScale");
    if (std::string problem =
            ReadAbilities(state, {"ImmediateReload", "LightRepair", "InstantRepair", "Turn180"}, out.abilities);
        !problem.empty())
        return problem;
    out.date_time = Text(state, "dateTimeText").value_or("");
    out.location = Text(state, "locationText").value_or("");
    out.ammo = Text(state, "ammo").value_or("");
    if (!out.ammo.empty() && !OneOf(out.ammo, {"balls", "grapes", "knippels", "bombs"}))
        return "unknown ammo '" + out.ammo + "'";
    out.ammo_count = Amount(state, "ammoCount");
    out.ammo_low = Flag(state, "navigatorAmmoWarning").value_or(false);
    out.powder_out = Flag(state, "navigatorPowderWarning").value_or(false);
    if (const auto counts = state.find("ammoCounts"); counts != state.end() && counts->is_object())
    {
        const char *const kinds[] = {"balls", "grapes", "knippels", "bombs"};
        for (size_t i = 0; i < std::size(kinds); ++i)
            out.ammo_counts[i] = Amount(*counts, kinds[i]);
    }
    out.sail_state = Amount(state, "sailState") > 1.0 ? -1.0 : Amount(state, "sailState");
    out.can_set_sails = Flag(state, "canSetSails").value_or(false);
    out.known = true;
    return {};
}

// null = the engine has no sea command menu now.
std::string ReadSeaOrders(const Json &state, SeaOrders &out)
{
    if (state.is_null())
        return {};
    if (std::string problem = ReadMenu(state, {"commands", "options", "targets"}, false, out); !problem.empty())
        return problem;
    const auto recipient = Text(state, "recipientId");
    const auto recipients = state.find("recipients");
    if (!Flag(state, "complete") || !recipient || recipients == state.end() || !recipients->is_array())
        return "a required field is missing or mistyped";
    std::set<std::string> ids;
    for (const Json &source : *recipients)
    {
        SeaOrders::Recipient entry;
        const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
        const auto name = source.is_object() ? Text(source, "name") : std::nullopt;
        const auto entity = source.is_object() ? Text(source, "entityId") : std::nullopt;
        const auto main = source.is_object() ? Flag(source, "main") : std::nullopt;
        const auto character = source.is_object() ? source.find("characterIndex") : Json::const_iterator();
        if (!id || !name || !entity || !main || character == source.end() || !character->is_number_integer())
            return "a recipient lacks id, name, entityId, characterIndex or main";
        if (!ids.insert(*id).second)
            return "recipient id '" + *id + "' is listed twice";
        entry.id = *id;
        entry.name = *name;
        entry.entity = *entity;
        entry.character = character->get<int>();
        entry.main = *main;
        out.recipients.push_back(std::move(entry));
    }
    if (!recipient->empty() && !ids.count(*recipient))
        return "recipientId names no recipient";
    if (out.active && (recipient->empty() || out.recipients.empty()))
        return "an open menu without a recipient";
    out.recipient = *recipient;
    out.complete = out.complete && out.Drawable();
    return {};
}

std::string ReadSeaQuick(const Json &state, SeaQuick &next)
{
    const auto revision = state.is_object() ? Whole(state, "revision") : std::nullopt;
    const auto available = state.is_object() ? Flag(state, "available") : std::nullopt;
    const auto action = state.is_object() ? Text(state, "action") : std::nullopt;
    const auto label = state.is_object() ? Text(state, "label") : std::nullopt;
    const auto map = state.is_object() ? Flag(state, "map") : std::nullopt;
    const auto map_label = state.is_object() ? Text(state, "mapLabel") : std::nullopt;
    if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1} || !revision || !available || !action ||
        !label)
        return "expected {schemaVersion: 1, revision, available, action, label}";
    if (map.value_or(false) && (!*available || *action == "Map"))
        return "the world map offered beside no other action ('" + *action + "')";
    if (map.value_or(false) && !map_label)
        return "the world map offered without its caption";
    if (*available && !OneOf(*action, {"Map", "Moor", "Board", "LandTroops", "Reload"}))
        return "unknown sea quick action '" + *action + "'";
    next.known = true;
    next.revision = *revision;
    next.available = *available;
    next.action = *available ? *action : std::string();
    next.label = *available ? *label : std::string();
    next.map = map.value_or(false);
    next.map_label = next.map ? *map_label : std::string();
    return {};
}

// A finite number; nothing when absent or null.
std::optional<double> Number(const Json &object, const char *key, bool &bad)
{
    const auto found = object.find(key);
    if (found == object.end() || found->is_null())
        return std::nullopt;
    if (!found->is_number() || !std::isfinite(found->get<double>()))
    {
        bad = true;
        return std::nullopt;
    }
    return found->get<double>();
}

std::string ReadWorldMap(const Json &state, WorldMap &out)
{
    if (!state.is_object())
        return "expected an object, got " + std::string(state.type_name());
    if (Whole(state, "schemaVersion") != uint64_t{1})
        return "expected {schemaVersion: 1, …}";
    const auto revision = Whole(state, "revision"), activity = Whole(state, "activity"),
               gameplay = Whole(state, "gameplayRevision"), mask = Whole(state, "mask");
    const auto available = Flag(state, "available"), ready = Flag(state, "commandsReady"),
               ships = Flag(state, "shipsVisible"), instruments = Flag(state, "instrumentsVisible");
    const auto date = Text(state, "date"), coordinates = Text(state, "coordinates");
    const auto fleet = state.find("ships"), commands = state.find("commands");
    if (!revision || !activity || !gameplay || !mask || !available || !ready || !ships || !instruments || !date ||
        !coordinates || fleet == state.end() || !fleet->is_array() || commands == state.end() || !commands->is_array())
        return "a required field is missing or mistyped";
    bool bad = false;
    const auto hour = Number(state, "hour", bad), force = Number(state, "windForce", bad),
               wind = Number(state, "windAngle", bad), heading = Number(state, "heading", bad),
               morale = Number(state, "morale", bad), speed = Number(state, "speed", bad),
               time_scale = Number(state, "timeScale", bad), food = Number(state, "foodDays", bad),
               rum = Number(state, "rumDays", bad), flag = Number(state, "flag", bad),
               storm = Number(state, "stormState", bad), moon = Number(state, "moonPhase", bad);
    if (bad)
        return "an instrument value is not a number";
    // Indices into the HUD's tables of flags, weathers and moon pictures.
    const auto index = [](const std::optional<double> &value, int last) {
        return !value || (*value >= 0.0 && *value <= last && std::floor(*value) == *value);
    };
    for (const auto &[value, last, name] : {std::tuple{flag, 4, "flag"}, std::tuple{storm, 2, "stormState"},
                                            std::tuple{moon, 7, "moonPhase"}})
        if (!index(value, last))
            return std::string(name) + " is not 0…" + std::to_string(last);
    for (const Json &source : *fleet)
    {
        WorldMap::Vessel vessel;
        const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
        const auto name = source.is_object() ? Text(source, "name") : std::nullopt;
        if (!id || !name)
            return "a ship lacks id or name";
        vessel.id = *id;
        vessel.name = *name;
        vessel.type = Text(source, "type").value_or("");
        vessel.hull = Number(source, "hull", bad).value_or(-1.0);
        vessel.sails = Number(source, "sails", bad).value_or(-1.0);
        const auto crew = Number(source, "crew", bad), most = Number(source, "crewMax", bad);
        if (bad)
            return "a ship has a bad hull, sails or crew";
        vessel.crew = crew && most && *most > 0.0 ? std::min(1.0, *crew / *most) : -1.0;
        if (const auto picture = source.find("picture"); picture != source.end())
            if (std::string problem = ReadPicture(*picture, vessel.picture); !problem.empty())
                return "a ship's " + problem;
        out.fleet.push_back(std::move(vessel));
    }
    std::set<std::string> ids;
    for (const Json &source : *commands)
    {
        const auto id = source.is_object() ? Text(source, "id") : std::nullopt;
        const auto label = source.is_object() ? Text(source, "label") : std::nullopt;
        if (!id || !label ||
            !OneOf(*id,
                   {"EnterToSea", "EnterToIsland", "EnterToShip", "EnterToStorm", "EnterToAttack", "EnterToEnemy"}))
            return "unknown or unlabelled command";
        if (!ids.insert(*id).second)
            return "command '" + *id + "' is listed twice";
        out.commands.push_back({*id, *label});
    }
    out.known = true;
    out.available = *available;
    out.revision = *revision;
    out.activity = *activity;
    out.gameplay_revision = *gameplay;
    out.mask = static_cast<unsigned>(*mask);
    out.instruments = *instruments;
    out.ships_visible = *ships;
    out.commands_ready = *ready;
    out.date = *date;
    out.coordinates = *coordinates;
    out.hour = hour.value_or(-1.0);
    out.wind_force = force.value_or(-1.0);
    out.morale = morale.value_or(-1.0);
    out.speed = speed.value_or(-1.0);
    out.time_scale = time_scale.value_or(-1.0);
    out.angles = wind && heading;
    out.wind_angle = wind.value_or(0.0);
    out.heading = heading.value_or(0.0);
    out.food_days = food ? static_cast<int>(*food) : -1;
    out.rum_days = rum ? static_cast<int>(*rum) : -1;
    out.flag = flag ? static_cast<int>(*flag) : -1;
    out.storm = storm ? static_cast<int>(*storm) : -1;
    out.moon_phase = moon ? static_cast<int>(*moon) : -1;
    return {};
}

std::string ReadPresentation(const Json &state, Presentation &next)
{
    const auto screen = state.is_object() ? Text(state, "screen") : std::nullopt;
    const auto loading = state.is_object() ? Flag(state, "loading") : std::nullopt;
    const auto cinematic = state.is_object() ? Flag(state, "cinematic") : std::nullopt;
    const auto dialogue = state.is_object() ? Flag(state, "dialogue") : std::nullopt;
    const auto live = state.is_object() ? Flag(state, "live") : std::nullopt;
    if (!screen || !loading || !cinematic || !dialogue || !live)
        return "expected {screen, loading, cinematic, dialogue, live}";
    next = {true, *screen, *loading, *cinematic, *dialogue, *live};
    return {};
}

// A whole number of the target in [low, high] (an index into the HUD's tables), or -1 when absent.
bool SpyNumber(const Json &object, const char *key, int low, int high, int &out)
{
    out = -1;
    const auto found = object.find(key);
    if (found == object.end())
        return true;
    if (!found->is_number_integer() || found->get<int64_t>() < low || found->get<int64_t>() > high)
        return false;
    out = found->get<int>();
    return true;
}

// {schemaVersion: 1, on, target: null | {kind: ship|fort, name, type, class, hull, sails, crew,
// cannons, cannonsMax, charge 0…3, nation 0…4, sail, speed, distance, captain, cannonType, portrait, skills:
// {fencing, cannons, accuracy, navigation, boarding}}}; a field the telescope cannot show is left out.
std::string ReadSpyglass(const Json &state, Spyglass &next)
{
    const auto on = state.is_object() ? Flag(state, "on") : std::nullopt;
    if (!state.is_object() || Whole(state, "schemaVersion") != uint64_t{1} || !on)
        return "expected {schemaVersion: 1, on, target}";
    next.on = *on;
    if (const auto found = state.find("target"); found != state.end() && !found->is_null())
    {
        const Json &t = *found;
        if (!next.on)
            return "a target while the telescope is down";
        const auto kind = t.is_object() ? Text(t, "kind") : std::nullopt;
        if (!kind || (*kind != "ship" && *kind != "fort"))
            return "target.kind must be ship or fort";
        next.kind = *kind;
        next.name = Text(t, "name").value_or("");
        next.type = Text(t, "type").value_or("");
        next.captain = Text(t, "captain").value_or("");
        next.cannon_type = Text(t, "cannonType").value_or("");
        next.portrait = Text(t, "portrait").value_or("");
        const int most = std::numeric_limits<int>::max();
        if (!SpyNumber(t, "class", 1, 7, next.ship_class) || !SpyNumber(t, "hull", 0, most, next.hull) ||
            !SpyNumber(t, "sails", 0, most, next.sails) || !SpyNumber(t, "crew", 0, most, next.crew) ||
            !SpyNumber(t, "cannons", 0, most, next.cannons) || !SpyNumber(t, "cannonsMax", 0, most, next.cannons_max) ||
            !SpyNumber(t, "charge", 0, 3, next.charge) || !SpyNumber(t, "nation", 0, 4, next.nation) ||
            !SpyNumber(t, "sail", 0, most, next.sail))
            return "a target number has the wrong type or range";
        next.speed = Amount(t, "speed");
        next.distance = Amount(t, "distance");
        if (const auto skills = t.find("skills"); skills != t.end())
        {
            const char *const names[] = {"fencing", "cannons", "accuracy", "navigation", "boarding"};
            for (int i = 0; i < 5; ++i)
                if (!skills->is_object() || !SpyNumber(*skills, names[i], 0, most, next.skills[i]))
                    return std::string("target.skills.") + names[i] + " is not a whole number";
        }
        next.target = true;
    }
    next.known = true;
    return {};
}

// One HUD channel in its fixture format. A fixture that does not fit leaves the channel empty; the gameplay
// channel keeps what it read of it (a mode it does not know is "unknown").
template <class T, auto Read> class Consumer final : public StateConsumer
{
  public:
    explicit Consumer(const char *channel) : channel_(channel)
    {
    }
    const char *Channel() const override
    {
        return channel_;
    }
    void OnState(const Json &state, Router &router) override
    {
        Hud *hud = router.Hud();
        if (!hud)
        {
            router.ReportError(std::string("state channel ") + channel_ + ": this host has no HUD");
            return;
        }
        T next;
        const std::string problem = Read(state, next);
        hud->Set(problem.empty() || std::is_same_v<T, Gameplay> ? next : T());
        if (!problem.empty())
            hud->Reject(channel_, problem);
    }

  private:
    const char *channel_;
};

std::string ReadWorldInfo(const Json &state, WorldInfo &out)
{
    if (!state.is_string())
        return "expected a string";
    out.text = state.get<std::string>();
    return {};
}

std::string ReadContextAction(const Json &state, ContextAction &out)
{
    if (!state.is_string())
        return "expected a string";
    out.name = state.get<std::string>();
    return {};
}

} // namespace

void AddStateConsumers(std::vector<std::unique_ptr<screens::StateConsumer>> &consumers)
{
    consumers.push_back(std::make_unique<Consumer<Gameplay, ReadGameplay>>("gameplay"));
    consumers.push_back(std::make_unique<Consumer<Combat, ReadCombat>>("combat"));
    consumers.push_back(std::make_unique<Consumer<Vitals, ReadVitals>>("vitals"));
    consumers.push_back(std::make_unique<Consumer<WorldInfo, ReadWorldInfo>>("worldInfo"));
    consumers.push_back(std::make_unique<Consumer<ContextAction, ReadContextAction>>("contextAction"));
    consumers.push_back(std::make_unique<Consumer<Door, ReadDoor>>("reloadPlace"));
    consumers.push_back(std::make_unique<Consumer<LandCommands, ReadLandCommands>>("landCommands"));
    consumers.push_back(std::make_unique<Consumer<Ship, ReadShip>>("ship"));
    consumers.push_back(std::make_unique<Consumer<SeaOrders, ReadSeaOrders>>("seaOrders"));
    consumers.push_back(std::make_unique<Consumer<SeaQuick, ReadSeaQuick>>("seaQuickAction"));
    consumers.push_back(std::make_unique<Consumer<WorldMap, ReadWorldMap>>("worldMap"));
    consumers.push_back(std::make_unique<Consumer<Presentation, ReadPresentation>>(app::channel::presentation));
    consumers.push_back(std::make_unique<Consumer<Spyglass, ReadSpyglass>>("spyglass"));
}

} // namespace storm::ui::hud
