// The portable UI's script bootstrap (android_ui_bootstrap.hpp): loaded once per script program, tried again only after
// the program is replaced, and a missing or uncompiled bootstrap stops the start once instead of being retried quietly.
#include "android_ui_bootstrap.hpp"

#include <catch2/catch.hpp>

#include <string>

using namespace storm::android;

namespace
{
// The interpreter and the file system as the bootstrap sees them.
struct Interpreter
{
    bool exists = true, compiles = true, loaded = false;
    int probes = 0, compileCalls = 0;
    UiBootstrap bootstrap;

    void load()
    {
        bootstrap.load(
            "PROGRAM\\",
            [&](const char *segment) {
                CHECK(std::string(segment) == "interface/android_ui_bootstrap.c");
                return loaded;
            },
            [&](const char *path) {
                CHECK(std::string(path) == "PROGRAM\\interface/android_ui_bootstrap.c");
                ++probes;
                return exists;
            },
            [&](const char *) {
                ++compileCalls;
                loaded = compiles;
                return compiles;
            });
    }
    // Release: the segments go with the program.
    void release()
    {
        bootstrap.reset();
        loaded = false;
    }
};

LandCommandsFrame menu()
{
    LandCommandsFrame f;
    f.available = f.active = true;
    f.ownerIdentity = "native-list";
    f.selectedItemId = "i0";
    f.items = {{"i0", "command", "Talk", "BI_DialogStart", "target", true, 1, ""}};
    return f;
}
} // namespace

TEST_CASE("loaded once per program, again after each replacement")
{
    Interpreter vm;
    vm.load(); // Main returned
    CHECK(vm.loaded);
    CHECK(vm.compileCalls == 1);
    for (int i = 0; i < 100; ++i)
        vm.load(); // a new game within the same program
    CHECK(vm.compileCalls == 1);
    for (int save = 0; save < 3; ++save)
    {
        vm.release(); // LoadState
        vm.load();    // OnLoad returned
        CHECK(vm.loaded);
    }
    CHECK(vm.compileCalls == 4);
}

TEST_CASE("a program that already has the bootstrap is not asked again")
{
    Interpreter vm;
    vm.loaded = true;
    vm.load();
    CHECK(vm.probes == 0);
    CHECK(vm.compileCalls == 0);
}

TEST_CASE("a missing or uncompiled bootstrap stops the start once")
{
    Interpreter vm;
    SECTION("missing")
    {
        vm.exists = false;
        CHECK_THROWS_WITH(vm.load(), Catch::Contains("android_ui_bootstrap.c is missing"));
        CHECK(vm.compileCalls == 0);
    }
    SECTION("does not compile")
    {
        vm.compiles = false;
        CHECK_THROWS_WITH(vm.load(), Catch::Contains("do not compile"));
        CHECK(vm.compileCalls == 1);
    }
    const int probes = vm.probes, compiles = vm.compileCalls;
    vm.exists = vm.compiles = true;
    for (int i = 0; i < 100; ++i)
        vm.load();
    CHECK(vm.probes == probes);
    CHECK(vm.compileCalls == compiles);
    CHECK_FALSE(vm.loaded);
}

TEST_CASE("replacing the program revokes the touch menu's taps, not the stock menu")
{
    Interpreter vm;
    vm.load();
    landCommands.update("same-land-save", menu());
    const storm::ui::hud::MenuStep tap{storm::ui::hud::MenuStep::Menu::Land, landCommands.revision(), "activateItem",
                                       "i0"};
    REQUIRE(landCommands.offer(tap));
    vm.release();
    CHECK_FALSE(landCommands.frame().available);
    CHECK_FALSE(landCommands.take());
    CHECK_FALSE(landCommands.offer(tap));
    vm.load();
    landCommands.update("same-land-save", menu());
    CHECK_FALSE(landCommands.offer(tap)); // a tap made for the old tables
}
