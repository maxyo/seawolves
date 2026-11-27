// The land HUD's channel state as the engine makes it from what the land interface read: the vitals, the boarding
// men, the walk-only effect and the abilities.
#include <platform/android_gameplay.hpp>
#include <platform/android_hud_channel.hpp>
#include <platform/android_sea_orders.hpp>
#include <platform/android_sea_quick_action.hpp>
#include <platform/android_worldmap.hpp>
#include <platform/android_land_vitals.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

using namespace storm::android;

namespace
{
// The vitals channel as landVitalsHud makes it from what the land interface read.
void vitalsState()
{
    LandVitals v;
    v.health = 0.3f;
    v.capacity = 3;
    v.loaded = 5; // more than the pistol holds: shown full
    v.hero = {0, "Charles de Maure", "", "battle_interface\\two", ""};
    v.officers.push_back({{12, "Tichingitu", "201", "", ""}, .5f, 1.f});
    v.opponent = LandOpponent{"Captain \"Bones\"", .25f};
    v.dash = 1.5f;
    const auto picture = [](const LandPortrait &p) {
        return p.face.empty() ? storm::ui::hud::Picture{p.texture, {0.f, 0.f, 1.f, 1.f}} : storm::ui::hud::Picture();
    };
    const auto shown = landVitalsHud(v, picture);
    REQUIRE((shown.known && shown.health == 0.3 && shown.loaded == 3 && shown.hero == 0 &&
             shown.picture.texture == "battle_interface\\two" && shown.officers.size() == 1 &&
             shown.officers[0].face == "201" && shown.officers[0].picture.texture.empty() &&
             shown.opponent == "Captain \"Bones\"" && shown.opponent_health == 0.25 && shown.dash == 1.0 &&
             !shown.boarding));
    std::printf("  %-14s the land interface's reading as the channel's state\n", "vitals");
}

// The boarding script's bodies and reserve as men (land_status.hpp readLandBoarding).
void boardingMen()
{
    // 12 bodies of 7 men each at the start; 5 in reserve and 4 alive on the deck: 63 of 84 men.
    const auto men = landBoardingMen(5, 4, 7.f, 12, 3, 2, 10.f, 12);
    REQUIRE((men && men->ours == 63 && men->oursStart == 84 && men->theirs == 50 && men->theirsStart == 120));
    REQUIRE(landBoardingMen(0, 0, 7.f, 12, 0, 0, 10.f, 12)->ours == 0);
    REQUIRE(landBoardingMen(1, 1, 2.5f, 3, 1, 1, 1.f, 3)->ours == 5);
    // The script's musketeers overdraw the enemy reserve (-2 seen in a real boarding): an empty reserve.
    REQUIRE(landBoardingMen(0, 1, 10.f, 1, -2, 16, 12.928572f, 14)->theirs == 207);
    REQUIRE((!landBoardingMen(0, -1, 7.f, 12, 0, 0, 10.f, 12) && !landBoardingMen(1, 0, 0.f, 12, 0, 0, 10.f, 12) &&
             !landBoardingMen(1, 0, 7.f, 0, 0, 0, 10.f, 12) &&
             !landBoardingMen(1, 0, 7.f, 12, 0, 0, std::nanf(""), 12)));
    std::printf("  %-14s men from bodies x crew_per_chr, refused without a start or a crew per body\n", "boarding");
}

// The game's walk-only rule as an effect (land_status.hpp readLandStatus): its run is its walk.
void walkOnly()
{
    REQUIRE((!landWalkOnlyEffect("run", "walk", false) && !landWalkOnlyEffect("run", "walk", true)));
    REQUIRE(std::string_view(landWalkOnlyEffect("walk", "walk", false)) == "overload");
    REQUIRE(std::string_view(landWalkOnlyEffect("walk", "walk", true)) == "cantrun");
    // No actions read (a chest, the hero not set up yet): no claim.
    REQUIRE(!landWalkOnlyEffect("", "", false));
    std::printf("  %-14s overload / cantrun only while the run animation is the walk\n", "walk-only");
}

} // namespace

// An ability's state from the scripts' perk counters (android_gameplay.hpp battleAbilityTiming) and
// its form in the ship and vitals channels.
void battleAbilities()
{
    BattleAbility a{"Rush", "Tincture \"O\""};
    battleAbilityTiming(a, 9.0, 72.0, 12.0, 72.0, false); // just used: the effect runs (the delay runs too)
    REQUIRE((a.state == BattleAbility::Active && a.fill == 0.75 && a.left == 9));
    battleAbilityTiming(a, std::nullopt, 53.5, 12.0, 72.0, false); // the effect over, the delay on
    REQUIRE((a.state == BattleAbility::Recovery && a.fill == 0.26 && a.left == 54));
    battleAbilityTiming(a, std::nullopt, std::nullopt, 12.0, 72.0, true);
    REQUIRE((a.state == BattleAbility::Ready && a.fill == 0 && a.left == 0));
    battleAbilityTiming(a, std::nullopt, std::nullopt, 12.0, 72.0, false);
    REQUIRE(a.state == BattleAbility::Blocked);
    battleAbilityTiming(a, 5.0, std::nullopt, std::nullopt, 72.0,
                        true); // a running effect without its TimeDuration: running, with no fill
    REQUIRE((a.state == BattleAbility::Active && a.fill == 0 && a.left == 5));
    a.state = BattleAbility::Recovery, a.fill = 0.26, a.left = 54;
    const auto shown = hudAbilities({a});
    REQUIRE((shown.size() == 1 && shown[0].id == "Rush" && shown[0].label == "Tincture \"O\"" &&
             shown[0].state == "recovery" && shown[0].fill == 0.26 && shown[0].left == 54));
    LandVitals v;
    const auto none = [](const LandPortrait &) { return storm::ui::hud::Picture(); };
    REQUIRE(landVitalsHud(v, none).abilities.empty());
    v.abilities = {a};
    REQUIRE(landVitalsHud(v, none).abilities == shown);
    std::printf("  %-14s active / recovery / ready / blocked from the perk counters, in ship and vitals\n",
                "abilities");
}

// Script text for the HUD: UTF-8 kept, anything else left out with its defect named; a FaceId is a portrait id.
void scriptTexts()
{
    const char *defect = nullptr;
    REQUIRE((hudScriptText("Шарль де Мор", defect) == "Шарль де Мор" && !defect));
    for (const char *bad : {"\xc0\xf0\xf2\xf3\xf0", "\xe0\x80\x80", "ab\xed\xa0\x80", "\xff"})
    {
        defect = nullptr;
        REQUIRE((hudScriptText(bad, defect).empty() && defect));
    }
    defect = nullptr;
    REQUIRE((hudFaceId("201", defect) == "201" && hudFaceId("Mary_1", defect) == "Mary_1" && !defect));
    for (const char *bad : {"../x", "a b", "12\\13", "\xd0\x90"})
    {
        defect = nullptr;
        REQUIRE((hudFaceId(bad, defect).empty() && defect));
    }
    std::printf("  %-14s UTF-8 only; a FaceId only of letters, digits and '_'\n", "script text");
}

// The UI host's keeper of the HUD states (engine_ui.cpp): an equal state is no change, channels are independent,
// a new host gets the newest of each in the channels' order.
void hudStates()
{
    using namespace storm::ui::hud;
    HudStates states;
    REQUIRE((states.keep(WorldInfo{"Сен-Пьер"}) && !states.keep(WorldInfo{"Сен-Пьер"})));
    REQUIRE((states.keep(Door{"таверна", false}) && states.keep(WorldInfo{"Сен-Пьер\nулицы"})));
    REQUIRE((!states.keep(Door{"таверна", false}) && states.keep(Door{"таверна", true})));
    std::vector<storm::ui::HudState> replayed;
    states.replay([&](const storm::ui::HudState &state) { replayed.push_back(state); });
    REQUIRE((replayed.size() == 2 && std::get<WorldInfo>(replayed[0]).text == "Сен-Пьер\nулицы" &&
             std::get<Door>(replayed[1]).locked));
    std::printf("  %-14s equal states dropped, channels independent, the newest of each replayed in order\n",
                "state keeper");
}

// The vitals' publication (android_ui.hpp publishVitals): a change at most every 100 ms, the hero gone at once.
void vitalsPublication()
{
    VitalsPublication publication;
    LandVitals v;
    v.health = 0.5f;
    REQUIRE((publication.due(v, 1000) && !publication.due(v, 2000))); // the same state never again
    v.health = 0.4f;
    REQUIRE((!publication.due(v, 1050) && publication.due(v, 1100))); // a change waits for its 100 ms
    v.health = 0.3f;
    REQUIRE(publication.due(std::nullopt, 1101)); // the hero's disappearance goes at once
    REQUIRE((!publication.due(std::nullopt, 1102) && publication.due(v, 1300)));
    std::printf("  %-14s 100 ms between changes, the hero's disappearance at once, no repeat\n", "vitals");
}

// A tap made in the loop iteration the window loses focus waits for a game frame that does not run; while the game
// is not in front, the main loop blocks the gameplay (AndroidGame::Block): every channel moves to a new state and
// the tap does not act when the game comes back.
void tapBeforeFocusLoss()
{
    using namespace storm::ui::hud;
    GameplaySnapshot sea{"sailing", "russian", "Sailing3Pers", "1", {"Ship_Fire"}};
    GameplaySnapshot blocked;
    blocked.blocked = "paused";
    GameplayState gameplay;
    gameplay.update(sea);
    REQUIRE(gameplay.offer({gameplay.revision(), "Ship_Fire"}));
    gameplay.update(blocked); // focus lost
    gameplay.update(sea);     // focus back
    REQUIRE(!gameplay.take());

    SeaQuickActionState quick;
    const SeaQuickActionFrame moor{19, true, true, true, "main:17", "Moor", "Moor", "sea:42"};
    quick.update(moor);
    REQUIRE(quick.offer({quick.revision(), "Moor"}));
    quick.update({});
    quick.update(moor);
    REQUIRE(!quick.consume());

    SeaOrdersState orders;
    SeaOrdersFrame menu;
    menu.available = true;
    menu.recipients.push_back({"r11_0", "Flagship", "11", 0, true});
    menu.recipientId = "r11_0";
    orders.update("sea", menu);
    REQUIRE(orders.offer({MenuStep::Menu::Sea, orders.revision(), "open", ""}));
    orders.update({}, {}); // closeSeaOrders
    orders.update("sea", menu);
    REQUIRE(!orders.take());

    WorldMapHud map;
    WorldMapFrame chart;
    chart.available = chart.commandsReady = true;
    chart.owner = 19;
    chart.commands = {{"EnterToSea", "Выйти в море"}};
    map.update(chart);
    REQUIRE(map.offer(storm::ui::hud::MapCommand{map.revision(), "EnterToSea"}));
    map.update({});
    map.update(chart);
    REQUIRE(!map.take());

    ShipTelemetryState ship;
    ShipTelemetryFrame frame;
    frame.valid = frame.canSetSails = true;
    ship.update(gameplay.revision(), frame);
    REQUIRE(ship.offerSails({gameplay.revision(), 2}));
    gameplay.update(blocked);
    ship.update(gameplay.revision(), {});
    gameplay.update(sea);
    ship.update(gameplay.revision(), frame);
    REQUIRE(!ship.takeSails());
    std::printf("  %-14s taps waiting while the game lost the screen do not act when it comes back\n", "focus loss");
}

// The engine UI host's queue of the UI's commands for the next frame (engine_ui.cpp).
void commandQueue()
{
    CommandQueue<std::string> queue;
    queue.push("hold:9\tShip_TurnLeft\t1", "hold-Ship_TurnLeft-1"); // a press …
    queue.push("hold:9\tShip_TurnLeft\t0", "");                     // … and its release in the same frame
    queue.push("move:a", "move");
    queue.push("gameplay:9\tShip_Fire", "");
    queue.push("move:b", "move"); // the stick's newer place takes the older one's turn
    const auto taken = queue.take();
    REQUIRE((taken == std::vector<std::string>{"hold:9\tShip_TurnLeft\t1", "hold:9\tShip_TurnLeft\t0", "move:b",
                                               "gameplay:9\tShip_Fire"}));
    queue.push("move:c", "move"); // a new frame: the slot is free again
    REQUIRE((queue.take() == std::vector<std::string>{"move:c"} && queue.take().empty()));
    std::printf("  %-14s press and release both, the newest of a slot in its place, order kept\n", "command queue");
}

// The activity's input epoch and the engine UI host's inbox (engine_ui.cpp): a touch sent before the input went off
// never acts after it, however late the game loop runs; touches after the return act in the first iteration of theirs.
void inputEpochOrder()
{
    struct Step
    {
        std::string call; // empty: a pointer sample
        int phase = 0;
        uint64_t epoch = 0;
        bool touch() const
        {
            return call.empty();
        }
    };
    const auto names = [](const UiInbox<Step>::Taken &taken) {
        std::vector<std::string> out;
        for (const Step &step : taken.steps)
            out.push_back(step.touch() ? "touch " + std::to_string(step.phase) : step.call);
        return out;
    };
    InputEpoch input;
    UiInbox<Step> inbox;
    const uint64_t live = input.now();
    inbox.push({"state", 0, live});
    inbox.push({"", 0, live}); // a tap: down …
    inbox.push({"", 2, live}); // … and up; the app goes before an iteration applies them
    input.set(false);
    inbox.push({"cancel", 0, input.now()});
    const uint64_t off = input.now();
    REQUIRE((!InputEpoch::live(off) && off > live));
    input.set(false);
    REQUIRE(input.now() == off); // no change, no new epoch
    auto taken = inbox.take(off);
    REQUIRE(((names(taken) == std::vector<std::string>{"state", "cancel"}) && taken.dropped == 2));
    inbox.push({"", 0, off}); // a sample while the input is off
    REQUIRE(inbox.take(off).dropped == 1);

    // A press applied before the input went off, its release after the return: the host gets the release without its
    // press, after the cancellation (UI checks: no click).
    input.set(true);
    const uint64_t back = input.now();
    REQUIRE((InputEpoch::live(back) && back > off));
    inbox.push({"", 2, back});
    inbox.push({"", 0, back}); // a new tap right after the return
    inbox.push({"", 2, back});
    taken = inbox.take(back);
    REQUIRE(((names(taken) == std::vector<std::string>{"touch 2", "touch 0", "touch 2"}) && !taken.dropped));

    // An iteration that began before the return leaves the return's touches, and what follows them, for the next one.
    input.set(false);
    const uint64_t again = input.now();
    input.set(true);
    inbox.push({"", 0, input.now()});
    inbox.push({"state", 0, input.now()});
    taken = inbox.take(again);
    REQUIRE((taken.steps.empty() && !taken.dropped));
    REQUIRE((names(inbox.take(input.now())) == std::vector<std::string>{"touch 0", "state"}));

    // The loop's gate: input taken in at the top of an iteration acts only while the epoch is that live one.
    REQUIRE(inputCurrent(back, back));
    REQUIRE(!inputCurrent(back, back + 1)); // the input went off during the iteration …
    REQUIRE(!inputCurrent(back, back + 2)); // … and came back before the iteration got there (the game thread paused)
    REQUIRE(!inputCurrent(off, off));       // an iteration with the input off

    std::printf("  %-14s touches sent before the input went off never act after it; the return's act at once\n",
                "input epoch");
}

TEST_CASE("android_hud_channels")
{
    commandQueue();
    inputEpochOrder();
    tapBeforeFocusLoss();
    hudStates();
    vitalsPublication();
    scriptTexts();
    vitalsState();
    boardingMen();
    walkOnly();
    battleAbilities();
    std::puts("android_hud_channels: ok");
}
