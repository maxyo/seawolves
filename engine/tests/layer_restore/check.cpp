#include "core_impl.h"
#include "platform/storage.hpp"

#include <catch2/catch.hpp>

#include <fstream>

namespace fs = std::filesystem;

namespace
{
constexpr char kScript[] = "PROGRAM\\interface\\touch\\core.c";

// A game and a UI layer with one script, the layer put back by `restore` when asked.
struct Folders
{
    fs::path root = fs::temp_directory_path() / "layer_restore";
    fs::path script = root / "layer" / "PROGRAM" / "interface" / "touch" / "core.c";
    int restores = 0;

    explicit Folders(bool restorable)
    {
        fs::remove_all(root);
        fs::create_directories(root / "game" / "PROGRAM");
        Restore();
        storm::storage::Folders folders{.game = root / "game", .saves = root / "saves"};
        folders.layers.mobileUi = root / "layer";
        folders.restoreLayers = [this, restorable] {
            ++restores;
            if (restorable)
                Restore();
            return restorable;
        };
        storm::storage::select(std::move(folders));
        fs::current_path(root / "game"); // the resource index's root
        fio->ScanResourcePaths();
    }

    void Restore() const
    {
        fs::create_directories(script.parent_path());
        std::ofstream(script) << "void TouchCore() {}\n";
    }
};
} // namespace

TEST_CASE("a layer file trimmed while the game runs is restored and read once more")
{
    Folders folders(true);
    fs::remove(folders.script);
    auto file = fio->_CreateFile(kScript, std::ios::binary | std::ios::in);
    CHECK(file.is_open());
    CHECK(folders.restores == 1);
    fio->_CloseFile(file);
    fs::remove(folders.script);
    CHECK(fio->_FileOrDirectoryExists(kScript));
    CHECK(folders.restores == 2);
}

TEST_CASE("a layer file the app cannot restore stays missing")
{
    Folders folders(false);
    fs::remove(folders.script);
    auto file = fio->_CreateFile(kScript, std::ios::binary | std::ios::in);
    CHECK_FALSE(file.is_open());
    CHECK(folders.restores == 1);
}

TEST_CASE("a game file that is missing does not restore the layers")
{
    Folders folders(true);
    auto file = fio->_CreateFile("PROGRAM\\absent.c", std::ios::binary | std::ios::in);
    CHECK_FALSE(file.is_open());
    CHECK_FALSE(fio->_FileOrDirectoryExists("PROGRAM\\absent.c"));
    CHECK(folders.restores == 0);
}
