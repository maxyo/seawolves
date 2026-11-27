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

// The two walks of the keys' definition: relative keys from the working directory, absolute keys below `root`.
static Map reference(const std::string &root)
{
    Map map;
    for (const auto &entry : fs::recursive_directory_iterator("."))
    {
        if (!indexable(entry))
            continue;
        auto path = entry.path().string();
        if (path.rfind("./", 0) == 0)
            path.erase(0, 2);
        add(map, path, "");
    }
    const auto prefix = storm::ResourceKey(root);
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (indexable(entry))
            add(map, entry.path().string(), prefix);
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

    // The working directory is the game directory: one walk, identical index.
    fs::current_path(game);
    Map single;
    scan(single, root);
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
    scan(linked, linkRoot);
    REQUIRE(linked == reference(linkRoot));

    // Any other root is an error, never a partial index.
    const fs::path elsewhere = fs::temp_directory_path() / "elsewhere";
    touch(elsewhere / "resource/local.txt");
    fs::current_path(elsewhere);
    Map wrong;
    REQUIRE_THROWS_AS(scan(wrong, root), std::runtime_error);
    fs::current_path(game);
    REQUIRE_THROWS_AS(scan(wrong, game.string()), std::runtime_error); // no trailing separator
}
