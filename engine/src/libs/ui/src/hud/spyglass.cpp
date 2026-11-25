// The spyglass overlay (on a ship or a fort): while the telescope
// is raised, the card of its target (ship or fort, its captain) lies over the HUD of any
// scene as the pack part <s-hud-spyglass>; the lens and the zoom stay the engine's. The Hud
// lays it (hud.cpp).

#include "hud.h"

#include <cmath>
#include <cstdio>

namespace storm::ui::hud
{

namespace
{

constexpr const char *kSkillIcons[] = {"sabre", "cannon", "accuracy", "wheel", "grapple"};
constexpr const char *kSkillKeys[] = {screens::StringKey("spy.skill_fencing"), screens::StringKey("spy.skill_cannons"),
                                      screens::StringKey("spy.skill_accuracy"),
                                      screens::StringKey("spy.skill_navigation"),
                                      screens::StringKey("spy.skill_boarding")};
// The game's nation order (ENGLAND, FRANCE, SPAIN, HOLLAND, PIRATE).
constexpr const char *kNations[] = {"england", "france", "spain", "holland", "pirates"};
constexpr const char *kNationKeys[] = {
    screens::StringKey("menu.nation_england"), screens::StringKey("menu.nation_france"),
    screens::StringKey("menu.nation_spain"), screens::StringKey("menu.nation_holland"),
    screens::StringKey("menu.nation_pirates")};
// The script's charge order (ispyglass.c: balls, bombs, chain, grape).
constexpr const char *kCharges[] = {screens::StringKey("spy.charge_balls"), screens::StringKey("spy.charge_bombs"),
                                    screens::StringKey("spy.charge_chain"), screens::StringKey("spy.charge_grape")};

std::string Decimal(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return buffer;
}

} // namespace

Json SpyglassProps(const Spyglass &s, Hud &hud)
{
    if (!s.target)
        return Json{{"empty", true}};
    const bool fort = s.kind == "fort";
    Json props{{"empty", false}, {"fort", fort}, {"icon", fort ? "fort" : "ship"}, {"name", s.name}};
    std::string sub;
    if (s.nation >= 0)
    {
        props["nation"] = std::string("nation-") + kNations[s.nation];
        sub = hud.Text(kNationKeys[s.nation]);
    }
    else
        props["nation"] = "";
    std::string type = fort ? hud.Text("spy.fort") : s.type;
    if (s.ship_class > 0)
        type += (type.empty() ? "" : ", ") + std::to_string(s.ship_class) + " " + hud.Text("yard.class");
    if (!type.empty())
        sub += (sub.empty() ? "" : " · ") + type;
    props["sub"] = sub;

    Json bars = Json::array();
    if (s.hull >= 0)
        bars.push_back(Json{{"icon", "hull"}, {"label", hud.Text("spy.hull")}, {"value", s.hull}, {"tone", "health"}});
    if (s.sails >= 0)
        bars.push_back(
            Json{{"icon", "sail-full"}, {"label", hud.Text("spy.sails")}, {"value", s.sails}, {"tone", "sails"}});
    props["bars"] = bars;

    Json facts = Json::array();
    if (s.cannons >= 0 && s.cannons_max >= 0)
        facts.push_back(
            Json{{"k", hud.Text("spy.cannons")},
                 {"v", std::to_string(s.cannons) + " " + hud.Text("common.of") + " " + std::to_string(s.cannons_max)}});
    if (s.speed >= 0.0)
        facts.push_back(
            Json{{"k", hud.Text("spy.speed")}, {"v", Decimal(s.speed) + " " + hud.Text("common.knots_short")}});
    if (s.distance >= 0.0)
        facts.push_back(Json{{"k", hud.Text("spy.distance")}, {"v", Decimal(s.distance)}});
    if (s.charge >= 0)
        facts.push_back(Json{{"k", hud.Text("spy.charge")}, {"v", hud.Text(kCharges[s.charge])}});
    if (s.crew >= 0)
        facts.push_back(Json{{"k", hud.Text("spy.crew")}, {"v", std::to_string(s.crew)}});
    props["facts"] = facts;

    Json skills = Json::array();
    for (int i = 0; i < 5; ++i)
        if (s.skills[i] >= 0)
            skills.push_back(Json{
                {"icon", kSkillIcons[i]}, {"label", hud.Text(kSkillKeys[i])}, {"value", std::to_string(s.skills[i])}});
    // A captain the telescope cannot make out: one line instead of the card.
    if (s.captain.empty())
    {
        props["captain"] = nullptr;
        props["unknown"] = hud.Text(fort ? "spy.no_commandant" : "spy.no_captain");
    }
    else
    {
        props["captain"] = Json{{"name", s.captain},
                                {"role", hud.Text(fort ? "spy.commandant" : "spy.captain")},
                                {"portrait", s.portrait},
                                {"skills", skills}};
        props["unknown"] = "";
    }
    return props;
}

} // namespace storm::ui::hud
