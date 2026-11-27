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

void game(const fs::path &root)
{
    put(root / "engine.ini", "screen_x = 1\n");
    put(root / "PROGRAM/seadogs.c", "void Main() {}\n");
    put(root / "PROGRAM/interface/menu.c", "void Menu() {}\n");
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
    const auto identity = gameIdentity(root / "one");
    REQUIRE(identity.size() == 16);
    REQUIRE(gameIdentity(root / "moved/elsewhere") == identity); // the same game in another place

    game(root / "renamed");
    fs::rename(root / "renamed/PROGRAM/interface", root / "renamed/PROGRAM/INTERFACE");
    REQUIRE(gameIdentity(root / "renamed") == identity); // the game's names are case-insensitive

    game(root / "version");
    put(root / "version/PROGRAM/interface/menu.c", "void Menu() { }\n");
    REQUIRE(gameIdentity(root / "version") != identity); // one script changed

    game(root / "added");
    put(root / "added/PROGRAM/new.c", "");
    REQUIRE(gameIdentity(root / "added") != identity);

    // The same bytes split otherwise between a file's name and its contents: another game.
    put(root / "split-one/PROGRAM/x", "1x");
    put(root / "split-two/PROGRAM/x1", "x");
    REQUIRE(gameIdentity(root / "split-one") != gameIdentity(root / "split-two"));

    put(root / "nothing/engine.ini", "");
    REQUIRE(failure([&] { gameIdentity(root / "nothing"); }).find("no PROGRAM") != std::string::npos);
}

TEST_CASE("desktop_folders.game")
{
    const auto root = fs::temp_directory_path() / "desktop-game";
    fs::remove_all(root);
    const auto folders = base({{"HOME", (root / "home").string()}});
    const auto engine = root / "engine";
    fs::create_directories(engine / "layers/engine-shaders");
    fs::create_directories(engine / "layers/script-patches");
    game(root / "games/one");
    game(root / "games/two");
    put(root / "games/two/PROGRAM/seadogs.c", "void Main() { Two(); }\n");

    const auto one = gameFolders(folders, root / "games/one", engine);
    const auto id = gameIdentity(root / "games/one");
    REQUIRE(one.game == root / "games/one");
    REQUIRE(one.saves == folders.data / "seawolves" / id / "saves");
    REQUIRE(one.settings == folders.config / "seawolves" / id);
    REQUIRE(one.cache == folders.cache / "seawolves" / id);
    REQUIRE(one.settingsFiles == std::vector<std::string>{"options", "engine.ini"});
    REQUIRE(one.layers.mobileUi.empty());
    REQUIRE(one.layers.engineShaders == engine / "layers/engine-shaders");
    REQUIRE(one.layers.scriptPatches == engine / "layers/script-patches");
    const auto two = gameFolders(folders, root / "games/two", engine);
    REQUIRE((two.saves != one.saves && two.settings != one.settings && two.cache != one.cache));

    // The player's folders inside the game folder, or the game inside one of them: refused.
    const auto inside = base({{"HOME", (root / "games/one").string()}});
    REQUIRE(failure([&] { gameFolders(inside, root / "games/one", engine); }).find("overlap") != std::string::npos);
    const auto cached = folders.cache / "seawolves" / id / "game"; // inside its own cache folder
    game(cached);
    REQUIRE(failure([&] { gameFolders(folders, cached, engine); }).find("overlap") != std::string::npos);
    REQUIRE(failure([&] { gameFolders(folders, "games/one", engine); }).find("absolute") != std::string::npos);

    fs::remove_all(engine / "layers/script-patches");
    REQUIRE(failure([&] { gameFolders(folders, root / "games/one", engine); }).find("script-patches") !=
            std::string::npos);
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
