#include "platform/resource_index.hpp"

#include <fstream>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using namespace storm::resource_index;

static void touch(const fs::path &path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

static Map reference(const std::string &root)
{
    Map map;
    scanRelative(map);
    scanAbsolute(map, root);
    return map;
}

TEST_CASE("resource_index")
{
    const fs::path game = fs::temp_directory_path() / "Game Dir";
    touch(game / "RESOURCE/Ini/Interfaces/Main.INI");
    touch(game / "RESOURCE/Textures/Ships/Sail.TX");
    touch(game / "Program/Seadogs.c");
    touch(game / "SAVE/Player/slot");
    touch(game / "engine.ini");
    touch(game / "Modules/readme.txt");      // not indexed
    touch(game / "Other/Deep/Options.Ini");  // indexed by extension
    touch(game / "resource-notes/Файл.txt"); // prefix match, non-ASCII bytes preserved
    fs::create_directories(game / "RESOURCE/Empty");
    const std::string root = game.string() + "/";

    // Working directory == game directory (Android): one walk, identical index.
    fs::current_path(game);
    Map single;
    REQUIRE(scan(single, root) == 1);
    const Map expected = reference(root);
    REQUIRE(single == expected);
    REQUIRE(single.at("resource/ini/interfaces/main.ini") == "RESOURCE/Ini/Interfaces/Main.INI");
    REQUIRE(single.at(storm::ResourceKey(root) + "resource/ini/interfaces/main.ini") ==
            root + "RESOURCE/Ini/Interfaces/Main.INI");
    REQUIRE(single.at("resource/empty") == "RESOURCE/Empty");
    REQUIRE(single.at("other/deep/options.ini") == "Other/Deep/Options.Ini");
    REQUIRE((single.count("modules/readme.txt") == 0 && single.count("modules") == 0));
    REQUIRE((single.count("save/player/slot") == 1 && single.count("engine.ini") == 1));
    // A symlinked spelling of the same directory still takes the single walk.
    fs::create_directory_symlink(game, fs::temp_directory_path() / "link");
    Map linked;
    const std::string linkRoot = (fs::temp_directory_path() / "link").string() + "/";
    REQUIRE((scan(linked, linkRoot) == 1 && linked == reference(linkRoot)));

    // Different working directory (desktop launch from elsewhere): two walks, as before.
    const fs::path elsewhere = fs::temp_directory_path() / "elsewhere";
    touch(elsewhere / "resource/local.txt");
    fs::current_path(elsewhere);
    Map twice;
    REQUIRE(scan(twice, root) == 2);
    REQUIRE(twice == reference(root));
    REQUIRE((twice.count("resource/local.txt") == 1 && twice.count("program/seadogs.c") == 0));
    // Root without a trailing separator is not eligible for the shortcut.
    fs::current_path(game);
    Map plain;
    REQUIRE((scan(plain, game.string()) == 2 && plain == reference(game.string())));
}
