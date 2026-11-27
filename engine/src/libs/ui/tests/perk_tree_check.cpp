// The perk tree's layout (storm_ui/tree_layout.h, script native UiTreeLayout): its invariants on the stock perk
// graphs. No game, no GPU.
#include "fakes.h"
#include "storm_ui/tree_layout.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace storm::ui;
using namespace storm::ui::screens;
namespace tree = storm::ui::tree_layout;
// The stock groups (PROGRAM/interface/perks/perks_init.c: IDs and condition attributes only, in the
// game's order); the personal group's Trustworthy needs the ship perk AdvancedCommerce.
// clang-format off
static const std::vector<std::string> kSelf{
    "BasicDefense", "AdvancedDefense", "CriticalHit", "HPPlus", "Tireless", "EnergyPlus", "Sliding",
    "SwordplayProfessional", "HardHitter", "BladeDancer", "Gunman", "GunProfessional", "Grus", "IronWill", "ShipEscape",
    "SharedExperience", "Medic", "Trustworthy", "Alchemy", "AdvancedCommerce", "BasicCommerce"};
static const std::vector<tree::Edge> kSelfEdges{
    {"BasicDefense", "AdvancedDefense"}, {"AdvancedDefense", "HPPlus"}, {"Tireless", "EnergyPlus"},
    {"AdvancedDefense", "Sliding"}, {"CriticalHit", "SwordplayProfessional"},
    {"AdvancedDefense", "SwordplayProfessional"}, {"Tireless", "SwordplayProfessional"},
    {"SwordplayProfessional", "HardHitter"}, {"SwordplayProfessional", "BladeDancer"}, {"Gunman", "GunProfessional"},
    {"IronWill", "ShipEscape"}, {"AdvancedDefense", "Medic"}, {"AdvancedCommerce", "Trustworthy"},
    {"IronWill", "Trustworthy"}, {"BasicCommerce", "AdvancedCommerce"}};
static const std::vector<std::string> kShip{
    "Brander", "Troopers", "LongRangeGrappling", "GrapplingProfessional", "MusketsShoot", "BasicCommerce",
    "AdvancedCommerce", "ProfessionalCommerce", "FastReload", "ImmediateReload", "HullDamageUp", "SailsDamageUp",
    "CrewDamageUp", "CriticalShoot", "LongRangeShoot", "CannonProfessional", "Carpenter", "LightRepair", "Builder",
    "BasicBattleState", "AdvancedBattleState", "InstantRepair", "SelfRepair", "ShipDefenseProfessional", "ShipSpeedUp",
    "ShipTurnRateUp", "StormProfessional", "WindCatcher", "SailsMan", "Turn180", "SailingProfessional", "Doctor1",
    "Doctor2", "SeaDogProfessional", "FlagPir", "FlagEng", "FlagFra", "FlagSpa", "FlagHol"};
static const std::vector<tree::Edge> kShipEdges{
    {"LongRangeGrappling", "GrapplingProfessional"}, {"MusketsShoot", "GrapplingProfessional"},
    {"CrewDamageUp", "MusketsShoot"}, {"BasicCommerce", "AdvancedCommerce"}, {"BasicCommerce", "ProfessionalCommerce"},
    {"AdvancedCommerce", "ProfessionalCommerce"}, {"FastReload", "ImmediateReload"}, {"HullDamageUp", "CriticalShoot"},
    {"SailsDamageUp", "CriticalShoot"}, {"CrewDamageUp", "CriticalShoot"}, {"CriticalShoot", "CannonProfessional"},
    {"LongRangeShoot", "CannonProfessional"}, {"FastReload", "CannonProfessional"}, {"Carpenter", "LightRepair"},
    {"LightRepair", "Builder"}, {"BasicBattleState", "AdvancedBattleState"}, {"BasicBattleState", "InstantRepair"},
    {"Carpenter", "InstantRepair"}, {"Builder", "SelfRepair"}, {"AdvancedBattleState", "SelfRepair"},
    {"AdvancedBattleState", "ShipDefenseProfessional"}, {"ShipSpeedUp", "Turn180"}, {"ShipTurnRateUp", "Turn180"},
    {"WindCatcher", "Turn180"}, {"SailsMan", "SailingProfessional"}, {"StormProfessional", "SailingProfessional"},
    {"Turn180", "SailingProfessional"}, {"Doctor1", "Doctor2"}, {"SailingProfessional", "SeaDogProfessional"}};
// clang-format on

// What every layout must hold: icons in their own boxes on the canvas, prerequisites above their
// dependents, links that start under their source, end above their target at a point of their own,
// and never run down the same lane as another source's link or through an icon.
static void Invariants(const tree::Layout &layout, const char *name)
{
    char what[256];
    const auto expect = [&](bool ok, const char *rule) {
        std::snprintf(what, sizeof what, "%s: %s", name, rule);
        Expect(ok, what);
    };
    expect(layout.valid, "laid out");
    for (const auto &[id, place] : layout.nodes)
    {
        expect(place.left >= 0 && place.left + tree::Layout::slot <= tree::Layout::width && place.top >= 0 &&
                   place.top + tree::Layout::box <= layout.height,
               "every touch box on the canvas");
        for (const auto &[other, next] : layout.nodes)
            if (id != other)
                expect(place.left + tree::Layout::slot <= next.left || next.left + tree::Layout::slot <= place.left ||
                           place.top + tree::Layout::box <= next.top || next.top + tree::Layout::box <= place.top,
                       "touch boxes never overlap");
    }
    std::map<std::string, std::set<int>> arrivals;
    for (const auto &link : layout.links)
    {
        const auto &edge = layout.edges[link.edge];
        const auto &s = layout.nodes.at(edge.from), &t = layout.nodes.at(edge.to);
        expect(s.top < t.top, "a prerequisite stands above its dependent");
        expect(link.points.size() >= 2 &&
                   link.points.front().y >= s.top + tree::Layout::icon_top + tree::Layout::icon &&
                   link.points.back().y <= t.top + tree::Layout::icon_top,
               "a link leaves under its source icon and arrives above its target icon");
        expect(arrivals[edge.to].insert(link.points.back().x).second, "each link arrives at its own point");
        // A lane (two points straight down) passes no icon of the rows between.
        for (size_t i = 0; i + 1 < link.points.size(); ++i)
        {
            const auto a = link.points[i], b = link.points[i + 1];
            if (a.x != b.x || i == 0 || i + 2 == link.points.size())
                continue;
            for (const auto &[id, v] : layout.nodes)
                if (v.top + tree::Layout::box > a.y && v.top < b.y)
                    expect(a.x <= v.left + (tree::Layout::slot - tree::Layout::icon) / 2 ||
                               a.x >= v.left + (tree::Layout::slot + tree::Layout::icon) / 2,
                           "a lane passes between icons");
            for (const auto &other : layout.links)
                if (layout.edges[other.edge].from != edge.from)
                    for (size_t j = 1; j + 2 < other.points.size(); ++j)
                    {
                        const auto c = other.points[j], d = other.points[j + 1];
                        if (c.x == a.x && d.x == c.x)
                            expect(b.y <= c.y || d.y <= a.y, "lanes of different sources never overlap");
                    }
        }
    }
}

static void Topology()
{
    const std::vector<std::string> ids{"Trustworthy", "IronWill", "AdvancedCommerce", "BasicCommerce", "BasicDefense"};
    const std::vector<tree::Edge> edges{
        {"BasicCommerce", "AdvancedCommerce"}, {"AdvancedCommerce", "Trustworthy"}, {"IronWill", "Trustworthy"}};
    const auto layout = tree::Lay(ids, edges);
    Expect(layout.valid && layout.nodes.size() == 5 && layout.links.size() == 3, "real cross-group closure retained");
    Expect(layout.nodes.at("basiccommerce").top < layout.nodes.at("advancedcommerce").top &&
               layout.nodes.at("advancedcommerce").top < layout.nodes.at("trustworthy").top &&
               layout.nodes.at("ironwill").top == layout.nodes.at("advancedcommerce").top,
           "a row per tier, top to bottom; a perk whose dependents stand further down moves next to them");
    Expect(layout.nodes.at("basicdefense").top == layout.nodes.at("basiccommerce").top,
           "a perk without links takes a free slot in the top row, among the others (no caption)");
    auto shuffled = edges;
    std::reverse(shuffled.begin(), shuffled.end());
    Expect(tree::Lay(ids, shuffled).nodes.at("trustworthy").left == layout.nodes.at("trustworthy").left,
           "the same topology in any edge order is laid out the same");
    Invariants(layout, "closure");

    const auto skipped = tree::Lay({"a", "b", "c"}, {{"a", "b"}, {"b", "c"}, {"a", "c"}});
    size_t lanes = 0;
    for (const auto &link : skipped.links)
        lanes += link.points.size() == 4;
    Expect(skipped.valid && lanes == 1, "a redundant stock link takes a lane past the middle row");
    Invariants(skipped, "skip");

    const auto self = tree::Lay(kSelf, kSelfEdges);
    Invariants(self, "stock personal group");
    const auto ship = tree::Lay(kShip, kShipEdges);
    Invariants(ship, "stock ship group");
    int top_row = 0;
    for (const auto &[id, place] : ship.nodes)
        top_row += place.top == ship.nodes.at("shipspeedup").top || place.top == ship.nodes.at("fastreload").top;
    Expect(ship.nodes.at("flaghol").top < ship.nodes.at("sailingprofessional").top && top_row > 4,
           "ship: the flags and lone perks fill free slots of the rows, no caption and no rows of their own");
    std::map<int, int> per_row;
    for (const auto &[id, place] : ship.nodes)
        ++per_row[place.top];
    int widest = 0;
    for (const auto &[row, count] : per_row)
        widest = std::max(widest, count);
    Expect(widest <= tree::Layout::slots && tree::Layout::width <= 500,
           "every stock tier fits one row of a 780 dp phone");

    auto cycle = edges;
    cycle.push_back({"Trustworthy", "BasicCommerce"});
    Expect(!tree::Lay(ids, cycle).valid, "cyclic data cannot produce an invented partial graph");
    Expect(!tree::Lay({"a"}, {{"missing", "a"}}).valid, "a missing source is not a root");
    Expect(!tree::Lay({"A", "a"}, {}).valid, "case alias duplicate rejected");
}

int main()
{
    Topology();
    if (!failures)
        std::puts("PASS: perk tree layout");
    return failures ? 1 : 0;
}
