#include "platform/android_writable_storage.hpp"
#include "platform/checked_file_io.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using storm::android::writable::Storage;

static void put(const fs::path &path, const std::string &text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.close();
    REQUIRE(!file.fail());
}

static std::string get(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    return {std::istreambuf_iterator<char>(file), {}};
}

template <typename Operation> static void rejected(Operation operation)
{
    bool failed = false;
    try
    {
        operation();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    REQUIRE(failed);
}

static void storageChecks(const fs::path &root)
{
    const auto game = root / "external/TEHO";
    const auto user = root / "private/files/storm";
    fs::create_directories(game);
    fs::create_directories(user.parent_path());
    Storage storage(game, user);
    const auto saves = user / "SaveData";
    REQUIRE(storage.resolve("SAVE") == saves);
    REQUIRE(storage.resolve(".\\sAvE\\Player One\\slot") == saves / "Player One/slot");
    REQUIRE(storage.resolve("save/p/../q/slot") == saves / "q/slot");
    REQUIRE(storage.resolve((game / "SAVE/p/slot").string()) == saves / "p/slot");
    REQUIRE(storage.resolve("OPTIONS") == user / "options");
    REQUIRE(storage.resolve((user / "options.storm-tmp").string()) == user / "options.storm-tmp");
    REQUIRE(storage.resolve((game / "options").string()) == user / "options");
    REQUIRE(!storage.resolve("savegame/a"));
    REQUIRE(!storage.resolve("RESOURCE/SAVE/a"));
    REQUIRE(!storage.resolve((root / "unrelated/save/a").string()));
    rejected([&] { storage.resolve("SAVE/../options"); });
    rejected([&] { storage.resolve("SAVE/p/../../outside"); });
    rejected([&] { storage.resolve((user / "../outside").string()); });

    put(saves / "player one/slot a", "private A");
    put(saves / "player one/Slot B", "private B");
    REQUIRE(storage.resolve("SaVe/PLAYER ONE/SLOT B") == saves / "player one/Slot B");
    REQUIRE(storage.resolve((saves / "player one/Slot B").string()) == saves / "player one/Slot B");
    REQUIRE(storage.resolve("save/player one/*") == saves / "player one/*");
    // Double resolution used by XI_FindFolders; listing and delete see the same files.
    const auto listing = storage.resolve(storage.resolve("SAVE/player one")->string()).value();
    REQUIRE(std::distance(fs::directory_iterator(listing), fs::directory_iterator{}) == 2);
    REQUIRE(fs::file_size(storage.resolve("save/player one/slot b").value()) == 9);
    fs::remove(storage.resolve("save/player one/slot b").value());
    const auto fresh = storage.resolve("SAVE/New Profile/new slot").value();
    put(fresh, "new save");
    REQUIRE(get(storage.resolve("save/NEW PROFILE/NEW SLOT").value()) == "new save");
    REQUIRE(!fs::exists(game / "SAVE/New Profile"));
    fs::remove_all(storage.resolve("save/new profile").value());
    REQUIRE(!fs::exists(fresh));

    fs::create_directory_symlink(game, saves / "linked");
    rejected([&] { storage.resolve("SAVE/linked/options"); });
    rejected([&] { storage.resolve("SAVE/linked/../player one"); });
    fs::remove(saves / "linked");
    put(saves / "player one/SLOT A", "ambiguous duplicate");
    rejected([&] { storage.resolve("save/player one/slot a"); });
}

static void optionsChecks(const fs::path &root)
{
    const auto game = root / "options/game";
    const auto user = root / "options/private/storm";
    put(game / "OPTIONS", "game options");
    put(game / "SAVE/player/slot", "a save beside the game");
    Storage storage(game, user);
    storage.seedOptions();
    REQUIRE(get(user / "options") == "game options");
    REQUIRE(!fs::exists(user / "SaveData")); // saves are never copied from the game
    REQUIRE(!fs::exists(user / "options.storm-tmp"));
    put(user / "options", "player options");
    storage.seedOptions();
    REQUIRE(get(user / "options") == "player options");

    const auto bare = root / "options/bare";
    fs::create_directories(bare);
    Storage without(bare, root / "options/private-bare/storm");
    without.seedOptions();
    REQUIRE(!fs::exists(root / "options/private-bare/storm/options"));

    const auto linked = root / "options/linked";
    fs::create_directories(linked);
    fs::create_symlink(game / "SAVE/player/slot", linked / "options");
    Storage symlinked(linked, root / "options/private-linked/storm");
    rejected([&] { symlinked.seedOptions(); });
    REQUIRE(!fs::exists(root / "options/private-linked/storm/options"));
}

static void streamChecks(const fs::path &root)
{
    using storm::io::checked;
    const auto path = root / "stream";
    std::fstream file(path, std::ios::binary | std::ios::out);
    REQUIRE(file.is_open());
    const auto mask = std::ios::failbit | std::ios::badbit;
    file.exceptions(mask);
    REQUIRE(checked(file, [&] { file.write("saved", 5); }).ok);
    REQUIRE(file.exceptions() == mask);
    REQUIRE(checked(file, [&] { file.close(); }).ok);
    REQUIRE(get(path) == "saved");
    file.open(path, std::ios::binary | std::ios::in);
    char buffer[32];
    REQUIRE(!checked(file, [&] { file.read(buffer, sizeof(buffer)); }).ok);
    REQUIRE((file.gcount() == 5 && file.fail()));
    REQUIRE(file.exceptions() == mask);
    REQUIRE(!checked(file, [&] { file.read(buffer, 1); }).ok); // already bad with throwing mask
    REQUIRE(!checked(file, [&] { file.close(); }).ok);         // retains earlier error
    REQUIRE(!file.is_open());

    // A buffered small write may succeed and only close reports ENOSPC.
    // This fixture verifies the mechanism, not the Android incident's errno.
    std::fstream full("/dev/full", std::ios::binary | std::ios::out);
    REQUIRE(full.is_open());
    REQUIRE(checked(full, [&] { full.write("x", 1); }).ok);
    const auto close = checked(full, [&] { full.close(); });
    REQUIRE((!close.ok && close.error == ENOSPC && full.fail()));
    REQUIRE(full.exceptions() == std::ios::goodbit);
    REQUIRE(!full.is_open());

    std::fstream large("/dev/full", std::ios::binary | std::ios::out);
    large.exceptions(mask);
    const std::string many(1024 * 1024, 'x');
    REQUIRE(!checked(large, [&] { large.write(many.data(), many.size()); }).ok);
    REQUIRE(large.exceptions() == mask);
    REQUIRE(!checked(large, [&] { large.close(); }).ok);
    std::fstream missing(root / "missing/file", std::ios::out);
    REQUIRE(!missing.is_open());
    REQUIRE(!checked(missing, [&] { missing.close(); }).ok);
}

TEST_CASE("android_writable_storage")
{
    const fs::path root = fs::temp_directory_path();
    storageChecks(root);
    optionsChecks(root);
    streamChecks(root);
    std::cout << "PASS: private writable paths, options seed, symlink rejection, checked I/O\n";
}
