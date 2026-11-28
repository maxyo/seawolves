#include "platform/desktop_folders.hpp"
#include "platform/desktop_settings.hpp"

#include <fstream>
#include <map>
#include <string>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using namespace storm::desktop;

namespace
{
void put(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

BaseFolders base(const std::map<std::string, std::string> &environment)
{
    return baseFolders([&](const char *name) -> const char * {
        const auto found = environment.find(name);
        return found == environment.end() ? nullptr : found->second.c_str();
    });
}

template <typename Operation> std::string failure(Operation operation)
{
    try
    {
        operation();
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
    return {};
}

// A game whose scripts name it `title` the way the series' scripts do.
void game(const fs::path &root, const std::string &title = "Sea Game  ")
{
    put(root / "engine.ini", "screen_x = 1\n");
    put(root / "PROGRAM/seadogs.c", "void Main() {}\n");
    put(root / "PROGRAM/interface/menu.c", "void Menu() {}\n");
    put(root / "PROGRAM/_LSC_on_off.h",
        "// #define VERSION_NUMBER1 \"Commented\"\r\n#define VERSION_NUMBER10 \"Other\"\r\n"
        "#define VERSION_NUMBER1       \"" +
            title + "\" // the game's own note\r\n#define VERSION_NUMBER2 \"ver. 1\"\r\n");
}
} // namespace

TEST_CASE("desktop_folders.base")
{
    const auto all = base({{"HOME", "/home/p"},
                           {"XDG_CONFIG_HOME", "/c"},
                           {"XDG_DATA_HOME", "/d"},
                           {"XDG_CACHE_HOME", "/k"},
                           {"XDG_STATE_HOME", "/s"}});
    REQUIRE((all.config == "/c" && all.data == "/d" && all.cache == "/k" && all.state == "/s"));
    const auto home = base({{"HOME", "/home/p"}, {"XDG_DATA_HOME", ""}});
    REQUIRE(home.config == "/home/p/.config");
    REQUIRE(home.data == "/home/p/.local/share"); // empty is unset
    REQUIRE(home.cache == "/home/p/.cache");
    REQUIRE(home.state == "/home/p/.local/state");
    REQUIRE(logsFolder(home) == "/home/p/.local/state/seawolves/logs");
    REQUIRE(settingsFile(home) == "/home/p/.config/seawolves/settings.json");
    REQUIRE(failure([] { base({{"HOME", "/home/p"}, {"XDG_CACHE_HOME", "relative"}}); }).find("XDG_CACHE_HOME") !=
            std::string::npos);
    REQUIRE(!failure([] { base({}); }).empty());
}

TEST_CASE("desktop_folders.identity")
{
    const auto root = fs::temp_directory_path() / "desktop-identity";
    fs::remove_all(root);
    game(root / "one");
    fs::create_directories(root / "moved");
    fs::copy(root / "one", root / "moved/elsewhere", fs::copy_options::recursive);
    const auto identity = buildIdentity(root / "one");
    REQUIRE(identity.size() == 16);
    REQUIRE(buildIdentity(root / "moved/elsewhere") == identity); // the same game in another place

    game(root / "renamed");
    fs::rename(root / "renamed/PROGRAM/interface", root / "renamed/PROGRAM/INTERFACE");
    REQUIRE(buildIdentity(root / "renamed") == identity); // the game's names are case-insensitive

    game(root / "version");
    put(root / "version/PROGRAM/interface/menu.c", "void Menu() { }\n");
    REQUIRE(buildIdentity(root / "version") != identity); // one script changed

    game(root / "added");
    put(root / "added/PROGRAM/new.c", "");
    REQUIRE(buildIdentity(root / "added") != identity);

    // The same bytes split otherwise between a file's name and its contents: another game.
    put(root / "split-one/PROGRAM/x", "1x");
    put(root / "split-two/PROGRAM/x1", "x");
    REQUIRE(buildIdentity(root / "split-one") != buildIdentity(root / "split-two"));

    put(root / "nothing/engine.ini", "");
    REQUIRE(failure([&] { buildIdentity(root / "nothing"); }).find("no PROGRAM") != std::string::npos);
    REQUIRE(failure([&] { gameKey(root / "nothing"); }).find("no PROGRAM") != std::string::npos);
}

TEST_CASE("desktop_folders.key")
{
    const auto root = fs::temp_directory_path() / "desktop-key";
    fs::remove_all(root);
    game(root / "one");
    const auto one = gameKey(root / "one");
    REQUIRE(one.title == "Sea Game");
    REQUIRE(one.key.size() == 16);
    REQUIRE(one.key != buildIdentity(root / "one"));

    // An update and a script mod: the same game, another build.
    game(root / "update");
    put(root / "update/PROGRAM/interface/menu.c", "void Menu() { New(); }\n");
    put(root / "update/PROGRAM/mod.c", "void Mod() {}\n");
    put(root / "update/PROGRAM/_LSC_on_off.h",
        "#define VERSION_NUMBER1 \" Sea Game\"\n#define VERSION_NUMBER2 \"ver. 2\"\n");
    REQUIRE(gameKey(root / "update").key == one.key);
    REQUIRE(buildIdentity(root / "update") != buildIdentity(root / "one"));

    // Another game of the series, even with the same scripts otherwise.
    game(root / "other", "Other Sea Game");
    REQUIRE(gameKey(root / "other").key != one.key);

    // Scripts that name no title: the key is the build's.
    game(root / "untitled");
    put(root / "untitled/PROGRAM/_LSC_on_off.h", "#define VERSION_NUMBER2 \"ver. 1\"\n");
    const auto untitled = gameKey(root / "untitled");
    REQUIRE((untitled.title.empty() && untitled.key == buildIdentity(root / "untitled")));
    fs::remove(root / "untitled/PROGRAM/_LSC_on_off.h");
    REQUIRE(gameKey(root / "untitled").key == buildIdentity(root / "untitled"));
}

TEST_CASE("desktop_folders.game")
{
    const auto root = fs::temp_directory_path() / "desktop-game";
    fs::remove_all(root);
    const auto folders = base({{"HOME", (root / "home").string()}});
    game(root / "games/one");
    game(root / "games/update");
    put(root / "games/update/PROGRAM/seadogs.c", "void Main() { Updated(); }\n");
    game(root / "games/two", "Another Game");

    const auto one = gameFolders(folders, root / "games/one");
    const auto key = gameKey(root / "games/one").key, id = buildIdentity(root / "games/one");
    REQUIRE(one.game == root / "games/one");
    REQUIRE(one.saves == folders.data / "seawolves" / key / "saves");
    REQUIRE(one.settings == folders.config / "seawolves" / key);
    REQUIRE(one.cache == folders.cache / "seawolves" / id);
    REQUIRE(one.settingsFiles == std::vector<std::string>{"options", "engine.ini"});
    // An update keeps the saves and the settings and gets caches of its own; another game shares nothing.
    const auto update = gameFolders(folders, root / "games/update");
    REQUIRE((update.saves == one.saves && update.settings == one.settings && update.cache != one.cache));
    const auto two = gameFolders(folders, root / "games/two");
    REQUIRE((two.saves != one.saves && two.settings != one.settings && two.cache != one.cache));

    // The player's folders inside the game folder, or the game inside one of them: refused.
    const auto inside = base({{"HOME", (root / "games/one").string()}});
    REQUIRE(failure([&] { gameFolders(inside, root / "games/one"); }).find("overlap") != std::string::npos);
    const auto cached = folders.cache / "seawolves" / id / "game"; // inside its own cache folder
    game(cached);
    REQUIRE(failure([&] { gameFolders(folders, cached); }).find("overlap") != std::string::npos);
    REQUIRE(failure([&] { gameFolders(folders, "games/one"); }).find("absolute") != std::string::npos);
}

TEST_CASE("desktop_folders.settings")
{
    const auto root = fs::temp_directory_path() / "desktop-settings";
    fs::remove_all(root);
    const auto file = root / "config/seawolves/settings.json";
    REQUIRE(rememberedGame(file).empty());
    put(file, R"({"other": 1})");
    rememberGame(file, "/games/Корсары");
    REQUIRE(rememberedGame(file) == storm::storage::utf8Path("/games/Корсары"));
    std::ifstream in(file);
    REQUIRE(nlohmann::json::parse(in)["other"] == 1); // the other settings stay
    put(file, "{\"game\": ");
    REQUIRE(failure([&] { rememberedGame(file); }).find(file.string()) != std::string::npos);
    put(file, R"({"game": 5})");
    REQUIRE(failure([&] { rememberedGame(file); }).find("not a string") != std::string::npos);
    put(file, "[]");
    REQUIRE(failure([&] { rememberedGame(file); }).find("not a JSON object") != std::string::npos);
}

TEST_CASE("desktop_folders.player")
{
    const auto root = fs::temp_directory_path() / "desktop-player";
    fs::remove_all(root);
    const auto file = root / "config/seawolves/settings.json";
    REQUIRE(playerSettings(file).name.empty());
    REQUIRE(playerSettings(file).ownedApps.empty());
    put(file, R"({"game": "/g", "player_name": "Шарль", "owned_apps": [2230982, 3000000]})");
    REQUIRE(playerSettings(file).name == "Шарль");
    REQUIRE(playerSettings(file).ownedApps == std::vector<int32_t>{2230982, 3000000});
    put(file, R"({"player_name": 7})");
    REQUIRE(failure([&] { playerSettings(file); }).find("player_name") != std::string::npos);
    put(file, R"({"owned_apps": 2230982})");
    REQUIRE(failure([&] { playerSettings(file); }).find("owned_apps") != std::string::npos);
    for (const char *bad :
         {R"({"owned_apps": ["2230982"]})", R"({"owned_apps": [1.5]})", R"({"owned_apps": [4294967296]})"})
    {
        put(file, bad);
        REQUIRE(failure([&] { playerSettings(file); }).find("not an app id") != std::string::npos);
    }
}
