#include "platform/android_gameplay.hpp"
#include <cassert>
#include <limits>
int main()
{
    using namespace storm::android;
    GameplayContext context;
    context.group = "PrimaryLand";
    context.land = true;
    assert(gameplayMode(context) == "unknown"); // missing native player fails closed
    context.nativeFight = false;
    assert(gameplayMode(context) == "explore");
    context.nativeFight = true;
    assert(gameplayMode(context) == "melee"); // native stance changed before script group
    context.group = "FightModeControls";
    assert(gameplayMode(context) == "melee");
    context.nativeFight = false;
    assert(gameplayMode(context) == "explore"); // native stance changed before script group
    for (const auto *group : {"PrimaryLand", "FightModeControls"})
        for (bool fight : {false, true})
        {
            context.group = group;
            context.nativeFight = fight;
            assert(gameplayMode(context) == (fight ? "melee" : "explore"));
            context.blocked = true;
            assert(gameplayMode(context) == "unknown");
            context.blocked = false;
        }
    context.group = "BattleInterfaceControls";
    assert(gameplayMode(context) == "unknown");
    context.group = "Sailing3Pers";
    context.sea = true;
    assert(gameplayMode(context) == "sailing");
    context.naval = true;
    assert(gameplayMode(context) == "naval");
    context.blocked = true;
    assert(gameplayMode(context) == "unknown"); // ComState overrides stale sailing group
    context.blocked = false;
    context.group = "PrimaryLand";
    assert(gameplayMode(context) == "unknown"); // entering sea before sailing group is activated
    // Why a scene has no mode (GameplaySnapshot::blocked): the touch UI holds touches only
    // between scenes, so every reason in a scene is named.
    {
        GameplayContext why;
        assert(gameplayBlocked(why) == "no-scene");
        why.location = true;
        assert(gameplayBlocked(why) == "no-land-interface"); // a location: the game may use touches (camera)
        why.land = true;
        why.group = "PrimaryLand";
        assert(gameplayBlocked(why) == "no-player");
        why.nativeFight = false;
        assert(gameplayBlocked(why) == "" && gameplayMode(why) == "explore");
        why.group = "DialogControls";
        assert(gameplayBlocked(why) == "group" && gameplayMode(why) == "unknown");
        why.blocked = true;
        assert(gameplayBlocked(why) == "commands");
        GameplayContext sea;
        sea.sea = true;
        sea.group = "Sailing1Pers"; // the deck camera
        assert(gameplayBlocked(sea) == "" && gameplayMode(sea) == "sailing");
        sea.group = "BattleInterfaceControls";
        assert(gameplayBlocked(sea) == "group");
        GameplayContext map;
        map.worldmap = true;
        map.group = "WorldMapControls";
        assert(gameplayBlocked(map) == "" && gameplayMode(map) == "worldmap");
        map.group = "MainInterface";
        assert(gameplayBlocked(map) == "group");
        GameplaySnapshot loading;
        loading.blocked = "loading";
        GameplayState published;
        published.update(loading);
        assert(published.hud().blocked == "loading");
    }
    GameplayState state;
    state.update({});
    assert(!state.offer({1, "Ship_Fire"}));
    GameplaySnapshot sea{
        "sailing", "russian", "Sailing3Pers", "1", {"Ship_Fire", "Sea_CameraSwitch", "hk_charge1", "Ship_SailUp"}};
    assert(state.update(sea));
    const auto first = state.revision();
    // A tap of the HUD on `control` for the gameplay revision shown now.
    auto request = [&state](std::string control) { return storm::ui::hud::GameplayControl{state.revision(), control}; };
    assert(!state.update(sea)); // identical frames do not invalidate held input
    assert(state.offer(request("Ship_Fire")));
    assert(state.take() == "Ship_Fire");
    assert(!state.take());                        // exactly once
    assert(!state.offer(request("Ship_SailUp"))); // polled controls cannot be faked by script events
    assert(!state.offer(request("hk_charge2")));  // unbound action
    for (int i = 0; i < 8; ++i)
        assert(state.offer(request("hk_charge1")));
    state.update({}); // pause/load/menu invalidates all pending actions
    assert(!state.take());
    state.update(sea);
    assert(state.revision() > first);
    assert(!state.offer({first, "Ship_Fire"})); // a tap on the gameplay before the pause does not act
    assert(state.offer(request("Ship_Fire")));
    sea.scene = "2";
    assert(state.update(sea));
    assert(!state.take()); // same mode, new scene
    assert(state.offer(request("Ship_Fire")));
    sea.capabilities = {"Sea_CameraSwitch"};
    state.update(sea);
    assert(!state.take());
    // the four ship abilities go through this entry point when the gameplay binds them
    sea.capabilities = {"BI_ImmediateReload", "BI_LightRepair", "BI_InstantRepair", "BI_Turn180"};
    state.update(sea);
    for (const char *ability : {"BI_ImmediateReload", "BI_LightRepair", "BI_InstantRepair", "BI_Turn180"})
    {
        assert(state.offer(request(ability)));
        assert(state.take() == ability);
    }
    assert(!state.offer(request("BI_InstantBoarding"))); // the beta-mode cheat is not carried
    // the spyglass toggle: raise and lower are taps of this entry point while the deck binds them
    assert(!state.offer(request("TelescopeIn")));
    sea.capabilities = {"TelescopeIn", "TelescopeOut"};
    state.update(sea);
    for (const char *control : {"TelescopeIn", "TelescopeOut"})
    {
        assert(state.offer(request(control)));
        assert(state.take() == control);
    }
    sea.capabilities = {"Sea_CameraSwitch"};
    state.update(sea);
    sea.inputOwner = "scene:player";
    assert(state.update(sea));
    // The HUD's state: the owner and analog movement only on land, where one player walks.
    assert(state.hud().revision == state.revision() && state.hud().Has("Sea_CameraSwitch") &&
           state.hud().input_owner.empty() && !state.hud().analog_movement);
    GameplayState restarted;
    restarted.update(sea);
    assert(restarted.revision() > state.revision());
    assert(!restarted.offer({state.revision(), "Sea_CameraSwitch"}));
}
