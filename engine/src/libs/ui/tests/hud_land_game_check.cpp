// The land HUD's place and talk icons against the game's own scripts (PROGRAM/*.c of an installed game).
// Game data is never in CI: without $STORM_GAME_DIR the check exits 77, which ctest reports as not run.
#include "fakes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>

using namespace storm::ui;

// Every town of the game's fast_reload_table.c: its place's picture (pic = FRP_*, the menu shows cell pic of
// LandTarget_SLB) against what the location id says the place is.
static void CheckPlaceIcons(const std::string &root)
{
    const auto read = [&](const std::string &relative) {
        std::ifstream file(root + "/PROGRAM/battle_interface/" + relative, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const std::string table = read("fast_reload_table.c"), land = read("landinterface.c");
    Expect(!table.empty() && !land.empty(), "the game's fast_reload_table.c and landinterface.c are read");
    // the menu's texture 1 (the places) and the ship's port icon
    Expect(std::regex_search(
               land, std::regex(
                         R"(CommandTextures\.list\.t1\.name\s*=\s*"battle_interface\\[Ll]and[Tt]arget_SLB\.tga\.tx")")),
           "texture 1 of the land menu is LandTarget_SLB");
    std::smatch port;
    Expect(std::regex_search(
               land, port,
               std::regex(
                   R"(UserIcons\.port\.selpic\s*=\s*(\d+);\s*\n\s*objLandInterface\.UserIcons\.port\.tex\s*=\s*1;)")) &&
               hud::LandPlaceIcon("landtarget_slb:" + port[1].str()) &&
               std::string(hud::LandPlaceIcon("landtarget_slb:" + port[1].str())) == "port",
           "the ship's port tile is the port");
    std::map<std::string, int> defines;
    const std::regex define(R"(#define\s+(FR[PT]_\w+)\s+(\d+))");
    for (std::sregex_iterator it(table.begin(), table.end(), define), end; it != end; ++it)
        defines[(*it)[1]] = std::stoi((*it)[2]);
    // what the location id says it is ("SentJons_tavern", "FleuronTavern", "SanAugustineResidence")
    const std::pair<const char *, const char *> kinds[] = {
        {"store", "store"},         {"shipyard", "shipyard"}, {"tavern", "tavern"}, {"townhall", "residence"},
        {"residence", "residence"}, {"bank", "usurer"},       {"church", "church"}, {"portoffice", "portoffice"},
        {"prison", "prison"},       {"brothel", "lantern"}};
    int places = 0, wrong = 0;
    std::string town;
    std::map<std::string, std::string> pic, tex; // by lN within the current town
    const std::regex line(
        R"(makearef\(curTable,objFastReloadTable\.table\.(\w+)\)|curTable\.(l\d+)\.(pic|tex|location)\s*=\s*([^;]+);)");
    for (std::sregex_iterator it(table.begin(), table.end(), line), end; it != end; ++it)
    {
        const std::smatch &m = *it;
        if (m[1].matched)
        {
            town = m[1];
            pic.clear(), tex.clear();
            continue;
        }
        const std::string slot = m[2], field = m[3], value = m[4];
        if (field == "pic")
            pic[slot] = value;
        else if (field == "tex")
            tex[slot] = value;
        else
        {
            std::string location = value.substr(value.find('"') + 1);
            location = location.substr(0, location.find('"'));
            const auto number = [&](const std::string &v) {
                return defines.count(v)                                 ? defines[v]
                       : std::isdigit(static_cast<unsigned char>(v[0])) ? std::stoi(v)
                                                                        : -1;
            };
            const int cell = number(pic[slot]), texture = number(tex[slot]);
            const char *icon =
                cell >= 0 && texture == 0 ? hud::LandPlaceIcon("landtarget_slb:" + std::to_string(cell)) : nullptr;
            std::string lower = location;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const char *expected = nullptr;
            for (const auto &[suffix, kind] : kinds)
                if (lower.find(suffix) != std::string::npos)
                    expected = kind;
            ++places;
            if (!icon || !expected || std::string(icon) != expected)
            {
                ++wrong;
                Expect(false, town + " " + slot + " " + location + ": pic " + pic[slot] + " tex " + tex[slot] +
                                  " shows '" + (icon ? icon : "?") + "', the location is '" +
                                  (expected ? expected : "unknown kind") + "'");
            }
        }
    }
    Expect(places > 100, "the game's towns list their places: " + std::to_string(places));
    std::printf("place icons against the game's towns: %d places, %d wrong\n", places, wrong);
}

// The talk hint's role icons against the game: every role the table names
// is a dialog file some character of the game talks through, and every common role file of the
// game (dialogs "Common_*", the ones the towns' people use) is either mapped or a plain person.
static void CheckTalkRoles(const std::string &root)
{
    std::set<std::string> files;
    const std::regex assign(R"re([Dd]ialog\.[Ff]ile[Nn]ame\s*=\s*"([^"]+)")re");
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root + "/PROGRAM"))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".c")
            continue;
        std::ifstream file(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        for (std::sregex_iterator it(text.begin(), text.end(), assign), end; it != end; ++it)
        {
            std::string stem = (*it)[1];
            if (const auto slash = stem.find_last_of("\\/"); slash != std::string::npos)
                stem.erase(0, slash + 1);
            if (stem.size() > 2 && stem.compare(stem.size() - 2, 2, ".c") == 0)
                stem.erase(stem.size() - 2);
            std::transform(stem.begin(), stem.end(), stem.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            files.insert(stem);
        }
    }
    Expect(files.size() > 50, "the game's characters name their dialog files: " + std::to_string(files.size()));
    for (const char *role : {"common_store", "common_itemtrader", "common_ammo", "common_shipyard", "common_tavern",
                             "waitress_dialog", "usurer_dialog", "common_mayor", "common_governor", "common_church",
                             "common_portman", "common_brothel", "common_prison", "common_lighthouse",
                             "enc_officer_dialog", "smuggler agent_dialog", "common_soldier", "common_fort"})
        Expect(files.count(role) && std::string(hud::LandTalkIcon(role)) != "talk",
               std::string("role '") + role + "' is a dialog file of the game, with its icon");
    const std::set<std::string> plain = {"common_poorman", "common_citizen", "common_cemeteryman"};
    int roles = 0;
    for (const std::string &file : files)
        if (file.rfind("common_", 0) == 0)
        {
            ++roles;
            Expect(std::string(hud::LandTalkIcon(file)) != "talk" || plain.count(file),
                   "the game's role '" + file + "' has no talk icon and is not a plain person");
        }
    std::printf("talk roles against the game's characters: %zu dialog files, %d common roles checked\n", files.size(),
                roles);
}

int main()
{
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::printf("NOT_RUN: set STORM_GAME_DIR to an installed game\n");
        return 77;
    }
    CheckPlaceIcons(root);
    CheckTalkRoles(root);
    if (failures)
        std::fprintf(stderr, "%d land HUD game check(s) failed\n", failures);
    else
        std::printf("land HUD icons agree with the game's scripts\n");
    return failures ? 1 : 0;
}
