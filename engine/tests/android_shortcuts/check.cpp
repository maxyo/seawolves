#include "platform/android_shortcuts.hpp"
#include <cstring>
#include <limits>

#include <catch2/catch.hpp>

TEST_CASE("android_shortcuts")
{
    using namespace storm::android;
    GameplayShortcuts queue;
    REQUIRE(!queue.offer("LaunchGameMenu", 1));
    REQUIRE(!queue.offer("pause", 1)); // Dialog pause belongs to SDL lifecycle, not a script event.
    REQUIRE(!queue.offer("mainmenu:injected", 1));
    REQUIRE(queue.offer("mainmenu", 100));
    REQUIRE(!queue.take(5000, false));        // Slow game / focus dialog: no timed key release.
    REQUIRE(queue.offer("mainmenu", 6000));   // Repeated tap coalesces.
    REQUIRE(!queue.offer("character", 6000)); // Conflicting commands cannot cascade menus.
    auto action = queue.take(10000, true);
    REQUIRE((action && std::strcmp(shortcutControl(*action), "MainMenu") == 0));
    REQUIRE(!queue.take(10001, true));
    REQUIRE(queue.offer("character", 10001));
    action = queue.take(10002, true);
    REQUIRE((action && std::strcmp(shortcutControl(*action), "Interface") == 0));
    REQUIRE(!queue.take(10002, true)); // Never retry after script refusal or menu open.
    REQUIRE(queue.offer("characterscreen", 10003));
    action = queue.take(10004, true);
    REQUIRE((action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "character") == 0));
    REQUIRE(queue.offer("shipscreen", 10005));
    action = queue.take(10006, true);
    REQUIRE((action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "ship") == 0));
    REQUIRE(queue.offer("journalscreen", 10007)); // an event line that leads to the journal
    action = queue.take(10008, true);
    REQUIRE((action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "journal") == 0));
    REQUIRE(queue.offer("itemsscreen", 10009)); // … to the inventory
    action = queue.take(10010, true);
    REQUIRE((action && !shortcutControl(*action) && std::strcmp(shortcutScreen(*action), "items") == 0));
    REQUIRE((!shortcutScreen(GameplayShortcut::MainMenu) && !shortcutScreen(GameplayShortcut::Character)));

    queue.offer("character", 0);
    REQUIRE(!queue.take(GameplayShortcuts::lifetimeMs, true)); // No stale UI replay.
    queue.offer("mainmenu", 1);
    queue.clear(); // Activity background / native UI screen change.
    REQUIRE(!queue.take(100, true));
    const auto wrap = std::numeric_limits<uint32_t>::max();
    queue.offer("mainmenu", wrap - 100);
    REQUIRE(queue.take(100, true)); // Tick rollover is not expiry.
    queue.offer("character", wrap - 100);
    REQUIRE(!queue.take(GameplayShortcuts::lifetimeMs, true));
    // The pause menu's quick save / load: waits while the menu is open, pressed once the game runs, dropped (and
    // named) when the menu stays open past its lifetime.
    QuickGame quick;
    const char *expired = nullptr;
    REQUIRE(!quick.offer("other", 0));
    REQUIRE(quick.offer("save", 0));
    REQUIRE((!quick.take(10, false, expired) && !expired)); // the menu is still open
    const char *control = quick.take(20, true, expired);
    REQUIRE((control && std::strcmp(control, "QuickSave") == 0 && !expired));
    REQUIRE(!quick.take(30, true, expired)); // pressed once
    REQUIRE(quick.offer("load", 100));
    REQUIRE(
        (!quick.take(100 + QuickGame::lifetimeMs, true, expired) && expired && std::strcmp(expired, "QuickLoad") == 0));
    REQUIRE(quick.offer("load", 0));
    quick.clear();
    REQUIRE((!quick.take(1, true, expired) && !expired));
}
