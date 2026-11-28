// The HUD's state channels as a scene script reads them (UiState) and the readings the runtime lays into a scene's
// view frame by frame. A reading is a value that moves with the game every frame or several times a second (the
// course, the clock): it is never handed to a script, which only says where it goes; the script's states carry
// `readings.<name>` = 1 for each one the engine knows now.
#include "hud.h"
#include "../screens/controls.h"

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

Json GameplayState(const Gameplay &gameplay)
{
    Json controls = Json::object();
    for (const std::string &control : gameplay.capabilities)
        controls[control] = true;
    return {{"mode", gameplay.mode}, {"blocked", gameplay.blocked}, {"controls", controls}};
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

Value Read(const std::string &name, const States &states, Hud &hud)
{
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

Json ScriptState(const std::string &channel, const States &states, Hud &hud)
{
    if (channel == "gameplay")
        return GameplayState(states.gameplay);
    if (channel == "worldMap")
        return WorldMapState(states, hud);
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
