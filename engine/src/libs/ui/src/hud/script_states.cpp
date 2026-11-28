// The HUD's state channels as a scene script reads them (UiState) and the readings the runtime lays into a scene's
// view frame by frame. A reading is a value that moves with the game every frame or several times a second (the
// course, the clock): it is never handed to a script, which only says where it goes; the script's states carry
// `readings.<name>` = 1 for each one the engine knows now.
#include "hud.h"
#include "../screens/format.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace storm::ui::hud
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

int Whole(double value)
{
    return static_cast<int>(std::lround(value));
}

// A share 0…100 as a whole percent, -1 unknown.
int Percent(double share, double scale)
{
    return share < 0.0 ? -1 : Whole(std::clamp(share * scale, 0.0, 100.0));
}

// c-game-image's rectangle as text: "left,top,right,bottom", each the shortest that reads back.
std::string Uv(const Picture &picture)
{
    std::string uv;
    for (const float side : picture.uv)
    {
        char text[32];
        const auto written = std::to_chars(text, std::end(text), side);
        uv.append(uv.empty() ? "" : ",").append(text, written.ptr);
    }
    return uv;
}

Json GameplayState(const Gameplay &gameplay, Hud &hud)
{
    if ((gameplay.mode == "explore" || gameplay.mode == "melee") && !gameplay.analog_movement)
        hud.Error("land HUD: no analog movement from the engine (inputOwner or movementGeneration missing)");
    Json controls = Json::object();
    for (const std::string &control : gameplay.capabilities)
        controls[control] = true;
    return {{"mode", gameplay.mode},
            {"blocked", gameplay.blocked},
            {"controls", controls},
            {"analogMovement", gameplay.analog_movement}};
}

// The combat state of the gameplay the HUD shows (`current`: of its revision and mode; otherwise its actions are not
// sent). The actions' generations are the runtime's, which a "combat" command carries.
Json CombatState(const States &states)
{
    const Combat &combat = states.combat;
    if (!combat.known)
        return {{"known", false}};
    Json actions = Json::object();
    for (const auto &[action, generation] : combat.actions)
        actions[action] = true;
    return {{"known", true},
            {"current", CombatCurrent(states)},
            {"mode", combat.mode},
            {"actions", actions},
            {"pistol", combat.pistol},
            {"interactKind", combat.interact_kind},
            {"interactName", combat.interact_name},
            {"interactDialog", combat.interact_dialog},
            {"readings", combat.pistol == "unloaded" ? Json{{"reload", true}} : Json::object()}};
}

// A party member's portrait: the game's 128 px face by FaceId; without one the land interface's own figure, which the
// engine sends as `picture` when the game has it (none: the placeholder). Both are said once per person.
Json Portrait(const std::string &face, const Picture &picture, const std::string &name, int character, Hud &hud)
{
    if (!face.empty())
        return {{"texture", "INTERFACES/PORTRAITS/128/face_" + face}, {"uv", "0,0,1,1"}};
    hud.Log("land HUD: " + name + " (character " + std::to_string(character) + ") has no FaceId: " +
            (picture.texture.empty() ? "the game's two.tga fallback is missing, the placeholder stays"
                                     : "the game's own figure " + picture.texture));
    return {{"texture", picture.texture}, {"uv", Uv(picture)}};
}

Json VitalsState(const Vitals &vitals, Hud &hud)
{
    if (!vitals.known)
        return {{"known", false}};
    Json effects = Json::object();
    for (const std::string &effect : vitals.effects)
        effects[effect] = true;
    Json officers = Json::array();
    for (const Vitals::Officer &officer : vitals.officers)
        officers.push_back({{"id", officer.id},
                            {"name", officer.name},
                            {"portrait", Portrait(officer.face, officer.picture, officer.name, officer.id, hud)}});
    Json abilities = Json::array();
    Json readings = {{"health", true}, {"energy", true}};
    bool running = false;
    for (const Ability &ability : vitals.abilities)
    {
        abilities.push_back(
            {{"id", ability.id}, {"label", ability.label}, {"state", ability.state}, {"command", ability.command}});
        if (ability.command.empty())
            hud.Error("land HUD: ability " + ability.id + " has no land menu command; it cannot be launched");
        if (ability.state == "active" || ability.state == "recovery")
            readings["ability." + ability.id] = true;
        running = running || ability.state == "active";
    }
    if (running)
        readings["abilitiesRunning"] = true;
    if (vitals.dash > 0.0)
        readings["dash"] = true;
    if (!vitals.opponent.empty())
        readings["opponent"] = true;
    if (vitals.boarding)
        readings["boarding"] = true;
    return {{"known", true},
            {"hero", vitals.hero},
            {"name", vitals.name},
            {"portrait", Portrait(vitals.face, vitals.picture, vitals.name, vitals.hero, hud)},
            {"effects", effects},
            {"potions", vitals.potions},
            {"loaded", vitals.loaded},
            {"capacity", vitals.capacity},
            {"officers", officers},
            {"dash", vitals.dash < 0.0   ? "none"
                     : vitals.dash > 0.0 ? "cooling"
                                         : "ready"},
            {"abilities", abilities},
            {"opponent", vitals.opponent},
            {"boarding", vitals.boarding.has_value()},
            {"readings", readings}};
}

// The engine's land menu: what it offers, the stage open and its rows by the engine's ids (`cooldown`: the whole
// percent a recovering row has still to go). Its revision is the runtime's, which a "menu" command carries.
Json LandCommandsState(const LandCommands &commands)
{
    if (!commands.known)
        return {{"known", false}};
    Json items = Json::array();
    for (const LandCommands::Item &item : commands.items)
        items.push_back({{"id", item.id},
                         {"kind", item.kind},
                         {"label", item.label},
                         {"source", item.source},
                         {"picture", item.picture},
                         {"enabled", item.enabled},
                         {"cooldown", item.enabled ? 0 : Whole((1.0 - item.cooldown) * 100.0)}});
    Json offered = Json::object();
    for (const std::string &command : commands.offered)
        offered[command] = true;
    Json out = {{"known", true},
                {"active", commands.active},
                {"available", commands.available},
                {"complete", commands.complete},
                {"stage", commands.stage},
                {"title", commands.title},
                {"selected", commands.selected},
                {"canBack", commands.can_back},
                {"canClose", commands.can_close},
                {"items", items},
                {"offered", offered}};
    if (!commands.here_picture.empty())
        out["here"] = {{"picture", commands.here_picture}, {"label", commands.here_label}};
    return out;
}

// The location text's names: the engine lists island, town and place and then the date and the time, one per line,
// without saying which is which; the calendar lines are the ones with digits.
Json WorldInfoState(const std::string &text)
{
    Json names = Json::array();
    for (size_t at = 0; at <= text.size();)
    {
        const size_t end = std::min(text.find('\n', at), text.size());
        const std::string line = text.substr(at, end - at);
        if (!line.empty() && line.find_first_of("0123456789") == std::string::npos)
            names.push_back(line);
        at = end + 1;
    }
    return {{"names", names}};
}

// The world map of the gameplay the HUD shows: `current` = false while the engine shows none (or another gameplay's).
Json WorldMapState(const States &states, Hud &hud)
{
    const WorldMap &map = states.world_map;
    const bool current = map.known && map.available && map.gameplay_revision == states.gameplay.revision;
    Json out = {{"current", current}};
    if (!current)
        return out;
    Json fleet = Json::array();
    for (const WorldMap::Vessel &vessel : map.fleet)
        fleet.push_back({{"id", vessel.id},
                         {"name", vessel.name},
                         {"type", vessel.type},
                         {"hull", Percent(vessel.hull, 1.0)},
                         {"crew", Percent(vessel.crew, 100.0)},
                         {"sails", Percent(vessel.sails, 1.0)},
                         {"texture", vessel.picture.texture},
                         {"uv", Uv(vessel.picture)}});
    Json commands = Json::array();
    for (const WorldMap::Command &command : map.commands)
        commands.push_back({{"id", command.id}, {"label", command.label}});
    Json readings = Json::object();
    if (map.angles)
        readings["course"] = readings["windFrom"] = true;
    if (map.wind_force >= 0.0)
        readings["wind"] = true;
    if (map.hour >= 0.0)
        readings["clock"] = readings["sky"] = true;
    if (map.speed >= 0.0)
        readings["speed"] = true;
    out.update({{"instruments", map.instruments},
                {"shipsVisible", map.ships_visible},
                {"commandsReady", map.commands_ready},
                {"date", map.date},
                {"coordinates", map.coordinates},
                {"morale", Percent(map.morale, 1.0)},
                {"foodDays", map.food_days},
                {"rumDays", map.rum_days},
                {"flag", map.flag},
                {"storm", map.storm},
                {"moonPhase", map.moon_phase},
                {"fleet", fleet},
                {"commands", commands},
                {"readings", readings}});
    if (map.storm < 0)
        hud.Error("world-map HUD: the engine gives no stormState; the compass draws no weather");
    // The scale the engine runs the world at, as a figure ("2.5"); absent: none.
    if (map.time_scale >= 0.0)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%.2f", map.time_scale);
        std::string figure = text;
        while (figure.back() == '0')
            figure.pop_back();
        if (figure.back() == '.')
            figure.pop_back();
        out["timeScale"] = figure;
    }
    return out;
}

// --- readings -------------------------------------------------------------------------------

// A reading's value now: a number (radians for angles, hours for the clock, a share 0…1) or a word; none while the
// engine does not know it (its `readings` flag is absent then).
struct Value
{
    std::optional<double> number;
    std::optional<std::string> word;
};

// The sky over the world map's compass: the sun by day, the moon by night, at an angle on its dome that rises at the
// east end (+90°), is highest at the top and sets at the west end (-90°); the game's day runs from `dawn` to `dusk`.
Value Sky(const WorldMap &map, bool angle, Hud &hud)
{
    if (map.hour < 0.0)
        return {};
    if (map.dawn < 0 || map.dusk <= map.dawn)
    {
        hud.Error("world-map HUD: the engine gives no dawn and dusk; the compass draws no sky");
        return angle ? Value{0.0, {}} : Value{{}, "none"};
    }
    const bool day = map.hour >= map.dawn && map.hour < map.dusk;
    if (!day && map.moon_phase < 0)
    {
        hud.Error("world-map HUD: the engine gives no moonPhase at night; the compass draws no moon");
        return angle ? Value{0.0, {}} : Value{{}, "none"};
    }
    const double share = day ? (map.hour - map.dawn) / (map.dusk - map.dawn)
                             : std::fmod(map.hour - map.dusk + 24.0, 24.0) / (24.0 - map.dusk + map.dawn);
    if (angle)
        return {90.0 - 180.0 * share, {}};
    return {{}, day ? "sun" : "moon"};
}

// The number after `prefix` in `name` ("vitals.officerHealth.2" → 2), -1 for another name.
int Index(const std::string &name, std::string_view prefix)
{
    if (!name.starts_with(prefix))
        return -1;
    int index = -1;
    const char *end = name.data() + name.size();
    const auto parsed = std::from_chars(name.data() + prefix.size(), end, index);
    return parsed.ec == std::errc() && parsed.ptr == end ? index : -1;
}

const Ability *AbilityOf(const Vitals &vitals, const std::string &id)
{
    for (const Ability &ability : vitals.abilities)
        if (ability.id == id)
            return &ability;
    return nullptr;
}

// The land readings: the party's bars, the fight's cooldowns, the boarding's men.
Value ReadLand(const std::string &name, const States &states)
{
    const Vitals &vitals = states.vitals;
    if (name == "combat.reload")
        return states.combat.known && states.combat.pistol == "unloaded"
                   ? Value{states.combat.pistol_progress / 20.0, {}}
                   : Value{};
    if (!vitals.known)
        return {};
    const auto share = [](int men, int start) { return Value{static_cast<double>(men) / start, {}}; };
    if (name == "vitals.health")
        return {vitals.health, {}};
    if (name == "vitals.energy")
        return {vitals.energy, {}};
    if (const int k = Index(name, "vitals.officerHealth."); k >= 0)
        return k < static_cast<int>(vitals.officers.size()) ? Value{vitals.officers[k].health, {}} : Value{};
    if (const int k = Index(name, "vitals.officerEnergy."); k >= 0)
        return k < static_cast<int>(vitals.officers.size()) ? Value{vitals.officers[k].energy, {}} : Value{};
    if (name == "vitals.dash")
        return vitals.dash > 0.0 ? Value{vitals.dash, {}} : Value{};
    if (name == "vitals.opponent")
        return vitals.opponent.empty() ? Value{} : Value{vitals.opponent_health, {}};
    if (vitals.boarding && name == "vitals.ours")
        return share(vitals.boarding->ours, vitals.boarding->ours_start);
    if (vitals.boarding && name == "vitals.theirs")
        return share(vitals.boarding->theirs, vitals.boarding->theirs_start);
    if (vitals.boarding && name == "vitals.oursMen")
        return {vitals.boarding->ours, {}};
    if (vitals.boarding && name == "vitals.theirsMen")
        return {vitals.boarding->theirs, {}};
    if (name == "vitals.abilitiesRunning")
    {
        double fill = -1.0;
        for (const Ability &ability : vitals.abilities)
            if (ability.state == "active")
                fill = std::max(fill, ability.fill);
        return fill < 0.0 ? Value{} : Value{fill, {}};
    }
    // vitals.ability.<id>.fill (what is left of the effect, how far it recovered) and .left (the seconds the scripts
    // still count, at least the one being counted).
    if (name.starts_with("vitals.ability."))
    {
        const size_t dot = name.rfind('.');
        const Ability *ability = AbilityOf(vitals, name.substr(15, dot - 15));
        if (!ability || (ability->state != "active" && ability->state != "recovery"))
            return {};
        if (name.ends_with(".fill"))
            return {ability->fill, {}};
        if (name.ends_with(".left"))
            return {static_cast<double>(std::max(1, ability->left)), {}};
    }
    throw std::runtime_error("no reading '" + name + "'");
}

Value Read(const std::string &name, const States &states, Hud &hud)
{
    if (name.starts_with("vitals.") || name.starts_with("combat."))
        return ReadLand(name, states);
    const WorldMap &map = states.world_map;
    if (name == "worldMap.course")
        return map.angles ? Value{map.heading, {}} : Value{};
    if (name == "worldMap.windFrom")
        return map.angles ? Value{map.wind_angle + kPi, {}} : Value{};
    if (name == "worldMap.wind")
        return map.wind_force >= 0.0 ? Value{map.wind_force, {}} : Value{};
    if (name == "worldMap.clock")
        return map.hour >= 0.0 ? Value{map.hour, {}} : Value{};
    if (name == "worldMap.speed")
        return map.speed >= 0.0 ? Value{map.speed, {}} : Value{};
    if (name == "worldMap.sky")
        return Sky(map, false, hud);
    if (name == "worldMap.skyAt")
        return Sky(map, true, hud);
    throw std::runtime_error("no reading '" + name + "'");
}

const char *const kPoints[] = {
    screens::StringKey("hud.map.dir.n"),  screens::StringKey("hud.map.dir.ne"), screens::StringKey("hud.map.dir.e"),
    screens::StringKey("hud.map.dir.se"), screens::StringKey("hud.map.dir.s"),  screens::StringKey("hud.map.dir.sw"),
    screens::StringKey("hud.map.dir.w"),  screens::StringKey("hud.map.dir.nw"),
};

int Degrees(double radians)
{
    const long whole = std::lround(radians * 180.0 / kPi);
    return static_cast<int>(((whole % 360) + 360) % 360);
}

// A reading in a format: "degrees" (an angle in whole degrees 0…359), "degreesText" ("130°"), "point" (the compass
// point of an angle, in words), "percent" (a share as a whole 0…100), "percentText" ("82%"), "clock" (hours as
// "HH:MM"), "whole" (a number rounded), "word" (a word reading as it is).
Json Format(const Value &value, const std::string &as, Hud &hud)
{
    if (as == "word")
    {
        if (!value.word)
            throw std::runtime_error("the reading is a number; format 'word' takes a word");
        return *value.word;
    }
    if (!value.number)
        throw std::runtime_error("the reading is a word; format '" + as + "' takes a number");
    const double number = *value.number;
    if (as == "degrees")
        return Degrees(number);
    if (as == "degreesText")
        return std::to_string(Degrees(number)) + "°";
    if (as == "point")
        return hud.Text(kPoints[((Degrees(number) + 22) % 360) / 45]);
    if (as == "percent")
        return Percent(number, 100.0);
    if (as == "percentText")
        return std::to_string(Percent(number, 100.0)) + "%";
    if (as == "whole")
        return Whole(number);
    if (as == "clock")
    {
        const int minutes = static_cast<int>(std::floor(number * 60.0)) % 1440;
        char text[16];
        std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
        return std::string(text);
    }
    throw std::runtime_error("no reading format '" + as + "'");
}

std::vector<std::string> Words(const std::string &text)
{
    std::vector<std::string> words;
    for (size_t at = 0; at < text.size();)
    {
        const size_t end = std::min(text.find(' ', at), text.size());
        if (end > at)
            words.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return words;
}
} // namespace

bool CombatCurrent(const States &states)
{
    return states.combat.known && states.combat.gameplay_revision == states.gameplay.revision &&
           states.combat.mode == states.gameplay.mode;
}

Json ScriptState(const std::string &channel, const States &states, Hud &hud)
{
    if (channel == "gameplay")
        return GameplayState(states.gameplay, hud);
    if (channel == "worldMap")
        return WorldMapState(states, hud);
    if (channel == "combat")
        return CombatState(states);
    if (channel == "vitals")
        return VitalsState(states.vitals, hud);
    if (channel == "landCommands")
        return LandCommandsState(states.land_commands);
    if (channel == "worldInfo")
        return WorldInfoState(states.world_info);
    if (channel == "contextAction")
        return {{"name", states.context_action}};
    if (channel == "reloadPlace")
        return {{"place", Capitalized(states.door.place)}, {"locked", states.door.locked}}; // as a line starts
    throw std::runtime_error("no HUD state channel '" + channel + "' for scene scripts");
}

bool ReadingRef::Known(const States &states, Hud &hud) const
{
    for (const std::string &name : Words(readings))
    {
        const Value value = Read(name, states, hud);
        if (!value.number && !value.word)
            return false;
    }
    return true;
}

Json ReadingRef::Resolve(const States &states, Hud &hud) const
{
    const std::vector<std::string> names = Words(readings), formats = Words(as);
    if (names.empty() || names.size() != formats.size())
        throw std::runtime_error("reading '" + readings + "' as '" + as + "': one format per reading");
    std::vector<std::string> parts;
    Json single;
    for (size_t i = 0; i < names.size(); ++i)
    {
        const Value value = Read(names[i], states, hud);
        if (!value.number && !value.word)
            throw std::runtime_error("the engine does not give the reading " + names[i] + " now");
        single = Format(value, formats[i], hud);
        parts.push_back(single.is_string() ? single.get<std::string>() : single.dump());
    }
    if (fill.empty())
    {
        if (names.size() != 1)
            throw std::runtime_error("reading '" + readings + "': several readings go into a fill text");
        return single;
    }
    return screens::Fill(fill, std::span<const std::string>(parts));
}

} // namespace storm::ui::hud
