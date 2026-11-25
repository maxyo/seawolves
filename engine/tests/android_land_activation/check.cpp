#include "platform/android_land_commands.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace storm::android;
// The land interface entity: whether it exists and how it answers a dispatched tap.
static bool ownerExists = true, accept = true;
static int calls = 0;
// What the scripts' refresh does to the candidates.
static std::function<void()> refresh;
static bool tap(const HudMenuRequest &request)
{
    return executeLandCommandWith(
               request, ownerExists, [] { refresh(); },
               [] {
                   ++calls;
                   return accept;
               })
        .accepted;
}
static LandCommandsFrame frame()
{
    LandCommandsFrame f;
    f.available = f.active = true;
    f.ownerIdentity = "list";
    f.selectedItemId = "i0";
    f.items = {{"i0", "travel", "Port", "port", "reloadtoport:SantaCatalina_town:reload1", true, 1, ""}};
    return f;
}
static HudMenuRequest request()
{
    return {landCommands.sceneEpoch(), landCommands.revision(), "activateItem", "i0"};
}
static storm::ui::hud::MenuStep payload(const HudMenuRequest &r)
{
    return {storm::ui::hud::MenuStep::Menu::Land, r.revision, r.verb, r.id};
}
// The menu's receipts (the last activation and the history) name `reason`.
static bool receipted(const std::string &reason)
{
    const auto menu = landCommands.hud();
    if (menu.activation.reason == reason)
        return true;
    for (const auto &activation : menu.activations)
        if (activation.reason == reason)
            return true;
    return false;
}

int main()
{
    landCommands.update("tavern", frame());
    auto r = request();
    refresh = [] {
        auto changed = frame();
        changed.items[0].identity = "a-different-target";
        landCommands.update("tavern", changed);
    };
    assert(!tap(r) && calls == 0);
    assert(receipted("candidates-changed-on-refresh"));
    assert(!landCommands.hud().activation.accepted);
    // Same item id is insufficient: changed candidate identity never executes.
    landCommands.update("tavern", frame());
    r = request();
    refresh = [] { landCommands.update("another-native-target-context", frame()); };
    assert(!tap(r) && calls == 0);
    assert(receipted("target-context-changed-on-refresh"));
    landCommands.update("tavern", frame());
    r = request();
    refresh = [] {};
    ownerExists = false;
    assert(!tap(r) && calls == 0);
    assert(receipted("no-land-owner"));
    ownerExists = true;
    assert(tap(r) && calls == 1);
    assert(receipted("native-dispatched"));
    // Accepted native dispatch does not assert any script transition happened.
    assert(landCommands.frame().active);
    const auto accepted = landCommands.hud();
    assert(!landCommands.offer(payload(r))); // duplicate of the accepted, now-stale lease
    assert(landCommands.hud() == accepted);  // may not downgrade native acceptance
    auto unrelated = request();
    unrelated.id = "i7";
    assert(!landCommands.offer(payload(unrelated)));
    assert(receipted("native-dispatched")); // kept in history
    r = request();
    accept = false;
    assert(!tap(r) && calls == 2);
    assert(receipted("native-refused"));
    r = request();
    assert(landCommands.offer(payload(r)));
    const auto pending = landCommands.hud();
    assert(!landCommands.offer(payload(r)));
    assert(landCommands.hud() == pending); // duplicate pending offer is not a refusal of the original
    landCommands.invalidate();
    assert(!landCommands.take());
    assert(receipted("invalidated-before-dispatch"));
    r = request();
    ++r.revision;
    assert(!landCommands.offer(payload(r)));
    assert(receipted("stale-at-enqueue"));
    // Refused before the scripts are asked: another scene, another revision, no menu.
    const int before = calls;
    bool refreshed = false;
    refresh = [&] { refreshed = true; };
    landCommands.update("tavern", frame());
    r = request();
    ++r.scene;
    assert(!tap(r) && receipted("scene-changed"));
    r = request();
    ++r.revision;
    assert(!tap(r) && receipted("revision-changed"));
    auto closed = frame();
    closed.available = false;
    landCommands.update("tavern", closed);
    r = request();
    assert(!tap(r) && receipted("unavailable"));
    assert(!refreshed && calls == before);
    // A tap on an item that is not the selected, enabled one.
    refresh = [] {};
    auto two = frame();
    two.items.push_back({"i1", "travel", "Shore", "shore", "reloadtoshore", false, 1, ""});
    landCommands.update("tavern", two);
    r = request();
    r.id = "i1";
    assert(!tap(r) && receipted("selection-no-longer-enabled")); // disabled
    two.items[1].enabled = true;
    landCommands.update("tavern", two);
    r = request();
    r.id = "i1";
    assert(!tap(r) && receipted("selection-no-longer-enabled")); // enabled but not selected
    two.items[0].enabled = false;
    landCommands.update("tavern", two);
    r = request();
    assert(!tap(r) && receipted("selection-no-longer-enabled")); // selected but disabled
    assert(calls == before);
    std::cout << "PASS production activation bridge: refreshed target refusal receipt, accepted/refused native "
                 "dispatch, invalidation, stale enqueue\n";
}
