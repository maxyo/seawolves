// Host checks for the land HUD, the scene of the touch interface's scripts (interface/touch/hud/land.c) on the
// fixture game's program. First the scene alone (a recording mount): what each synthetic state shows and which
// engine input each event sends. Then the whole runtime with a renderer that draws nothing and the application's
// pack: which touches the UI claims over the game, per pointer, and what real gestures send. No GL context, no game
// data.
#include "hud_scripts.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

static const ScriptPack *app_pack = nullptr;
static std::string app_pack_path;

// A bench whose HUD draws the scripts' land scene with the application's pack.
struct LandBench : Bench
{
    LandBench()
    {
        app_pack->Attach(*this);
    }
};

// Exploring a town on one HUD: the party and place, toasts and the log, the buttons, talking and its hint, event
// lines, a stale combat state and what is absent.
struct ExploreRun
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    Json talk; // the combat state with a talk target, from Hints on

    bool Hint(const char *icon, const char *text, bool leaving)
    {
        return mount.Has("hint") && mount.shown.elements["hint"]["icon"] == icon &&
               mount.shown.elements["hint"]["text"] == text && mount.shown.elements["hint"]["leaving"] == leaving;
    }

    void Party()
    {
        bench.Explore();
        ExpectEqual(mount.shown.layout, "land", "explore uses the land layout");
        const Json hero = mount.shown.elements["party"]["officers"];
        Expect(hero.size() == 1 &&
                   hero[0]["bars"] == Json::parse(R"([{"kind":"health","value":84},{"kind":"energy","value":62}])") &&
                   hero[0]["texture"] == "" && !hero[0].contains("tap") && !mount.Has("effects"),
               "the party: the hero alone, no effects row");
        ExpectEqual(mount.shown.elements["location"]["text"].get<std::string>(), "Сен-Пьер · улицы",
                    "the location line is town · place, without island, date or time");
        bench.Set("worldInfo", Json("Открытое море\n7 окт. 1654\n12:31"));
        ExpectEqual(mount.shown.elements["location"]["text"].get<std::string>(), "Открытое море",
                    "a single name stands alone");
        bench.Set("worldInfo", Json("1 октября 1654 11:15"));
        Expect(!mount.Has("location"), "a date alone is no location");
        bench.Feed("worldInfo", "world-info");
        Expect(mount.Has("hero") && mount.shown.elements["hero"]["disabled"] == false && mount.Has("pause") &&
                   mount.Has("move") && mount.Has("actions") && mount.Has("abilities") && !mount.Has("potion") &&
                   mount.Has("weapon") && !mount.Has("context") && !mount.Has("strike") && !mount.shown.modal,
               "the peace set (the potion behind the abilities button); no context without an action");
        ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.draw",
                    "the weapon button names what it will do");
        mount.commands.clear();
        bench.Press("pause");
        ExpectEqual(mount.Last(), "shortcut:mainmenu", "the menu button opens the game menu");
    }

    void Toasts()
    {
        // How long a toast stays: 2.5 s + 60 ms a character, 3…7 s.
        Expect(hud::ToastSeconds("Готово") == 3.0 && std::abs(hud::ToastSeconds("Недостаточно денег") - 3.58) < 1e-9 &&
                   hud::ToastSeconds(std::string(200, 'x')) == 7.0,
               "toast time by length: the short one 3 s, 18 characters 3.58 s, a long one 7 s");
        // A game message repeated while its toast is up: one toast with a count, its time starts again.
        Expect(bench.hud.Notice("Недостаточно денег", "info") && bench.hud.Notice("Взято: ром", "info"),
               "event lines go to the HUD's log");
        bench.Frame(2.0);
        Expect(bench.hud.Notice("Недостаточно денег", "info"), "the repeat is taken");
        bench.Frame();
        {
            const Json lines = mount.shown.elements["log"]["lines"];
            Expect(lines.size() == 2 && lines[1]["text"] == "Недостаточно денег \u00D72" &&
                       lines[0]["text"] == "Взято: ром",
                   "no second copy: the shown line counts «×2»: " + lines.dump());
        }
        bench.Frame(1.2); // 3.2 s after the first: «Взято: ром» (3.1 s) animates out, the repeated one lives on
        bench.Frame(0.3); // and is gone after its exit
        {
            const Json lines = mount.shown.elements["log"]["lines"];
            Expect(lines.size() == 1 && lines[0]["text"] == "Недостаточно денег \u00D72",
                   "the repeat extends its toast: " + lines.dump());
        }
        bench.hud.Notice("Недостаточно денег", "info");
        bench.Frame();
        ExpectEqual(mount.shown.elements["log"]["lines"][0]["text"].get<std::string>(), "Недостаточно денег \u00D73",
                    "and counts on");
        // a finger on the line stops its time; lifting it gives back what was left
        const std::string held = mount.shown.elements["log"]["lines"][0]["key"];
        bench.Press("log", "hold", held);
        bench.Frame(10.0);
        Expect(mount.Has("log") && mount.shown.elements["log"]["lines"][0]["leaving"] == false,
               "held: it stays as long as the finger does");
        bench.Press("log", "release", held);
        bench.Frame(3.4);
        Expect(mount.shown.elements["log"]["lines"][0]["leaving"] == false,
               "released: the time it had left (3.58 s) runs on");
        bench.Frame(0.3);
        Expect(mount.shown.elements["log"]["lines"][0]["leaving"] == true, "then it animates out");
        bench.Frame(0.3);
        Expect(!mount.Has("log"), "and goes");
        bench.hud.Notice("Недостаточно денег", "info");
        bench.Frame();
        ExpectEqual(mount.shown.elements["log"]["lines"][0]["text"].get<std::string>(), "Недостаточно денег",
                    "after it went, the same message starts afresh");
        bench.Frame(4.0);
    }

    void Buttons()
    {
        bench.Press("hero");
        ExpectEqual(mount.Last(), "shortcut:character", "Персонаж opens the character");
        bench.Press("pause");
        ExpectEqual(mount.Last(), "shortcut:mainmenu", "Меню opens the menu");
        bench.Press("weapon");
        ExpectEqual(mount.Last(), "combat:5\t11\tweapon\ttap",
                    "the weapon tap carries the activity, the action's generation and a sequence");
        bench.Press("abilities");
        Expect(mount.Has("abilities-1") && mount.shown.elements["abilities-1"]["icon"] == "potion" &&
                   !mount.Has("abilities-2"),
               "outside a fight the button lays out the potion alone: " + mount.shown.elements["abilities-1"].dump());
        Expect(mount.shown.elements["abilities-1"]["disabled"] == true &&
                   mount.shown.elements["abilities-1"]["badge"] == "0",
               "no potion carried: greyed with its 0");
        Json carried = State("vitals");
        carried["potions"] = 2;
        bench.Set("vitals", carried);
        Expect(mount.shown.elements["abilities-1"]["disabled"] == false &&
                   mount.shown.elements["abilities-1"]["badge"] == "2",
               "two carried: live, with the count");
        bench.Press("abilities-1");
        ExpectEqual(mount.Last(), "combat:5\t12\tpotion\ttap", "the potion drinks, the sequence grows");
        Expect(!mount.Has("abilities-1"), "… and the row folds");
        Expect(mount.Has("actions") && mount.Has("travel") && mount.shown.elements["travel"]["label"] == "hud.travel",
               "the menu split: actions at the top, transitions in the cluster");
        bench.Press("actions");
        ExpectEqual(mount.Last(), "landcommands:10\topen\t", "Действия asks the engine to open its menu");
        bench.Press("travel");
        ExpectEqual(mount.Last(), "landcommands:10\topen\tBI_FastReload",
                    "the boot opens the menu straight into the transitions");
        {
            Json only = State("land-commands-idle");
            only["offered"] = Json::array({"BI_FastReload"});
            bench.Set("landCommands", only);
            Expect(!mount.Has("actions") && mount.Has("travel"), "transitions alone: no actions button");
            only["offered"] = Json::array({"BI_TalkSelf"});
            bench.Set("landCommands", only);
            Expect(mount.Has("actions") && !mount.Has("travel"),
                   "no transitions offered (an enemy town, an alarm, a cabin): no boot");
            bench.Feed("landCommands", "land-commands-idle");
        }
    }

    void QuickAction()
    {
        // the script's quick action
        bench.Feed("contextAction", "context-door");
        Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "door" &&
                   mount.shown.elements["context"]["label"] == "hud.context.enter",
               "a door in front: the context slot");
        bench.Press("context");
        ExpectEqual(mount.Last(), "context:Reload", "the context tap names the action the engine published");
        bench.Set("contextAction", Json("Talk"));
        Expect(!mount.Has("context"), "the script's Talk never shows: talking belongs to the engine's target");
        bench.Set("contextAction", Json("Nothing"));
        Expect(!mount.Has("context"), "Nothing is no action");
        bench.Set("contextAction", Json("Levitate"));
        Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "hand",
               "an unknown action keeps its button with the generic look (the adapter says so in the script log)");
        bench.Press("context");
        ExpectEqual(mount.Last(), "context:Levitate", "… and still acts");
    }

    void Talking()
    {
        // talking: the engine's own target (U0): appears and disappears with the combat state
        bench.Feed("combat", "combat-explore-talk");
        Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "talk" &&
                   mount.shown.elements["context"]["label"] == "hud.context.talk",
               "somebody to talk to: the talk button takes the slot");
        bench.Press("context");
        ExpectEqual(mount.Last(), "combat:5\t14\tinteract\ttap", "talk is the engine's interact action");
        bench.Press("weapon");
        ExpectEqual(mount.Last(), "combat:5\t11\tweapon\ttap",
                    "a passer-by changes neither the activity nor the weapon's generation");
        const size_t sent = mount.commands.size();
        bench.router.SetState("combat",
                              State("combat-explore")); // the target is lost; the tap arrives before the next build
        bench.hud.OnEvent({"context", "click", ""});
        Expect(mount.commands.size() == sent, "a tap after the target is lost sends nothing");
        bench.hud.Tick();
        bench.Set("contextAction", Json(""));
        Expect(!mount.Has("context"), "the target left: no button");

        // after a won boarding fight the game waits for its action key (LAi_ActivateReload armed): the
        // button stays whatever the script's icon says meanwhile (locator exits reset it to Nothing)
        Json boarding = State("combat-explore-talk");
        boarding["interactKind"] = "boarding";
        bench.Set("combat", boarding);
        Expect(mount.Has("context") && mount.shown.elements["context"]["icon"] == "grapple" &&
                   mount.shown.elements["context"]["label"] == "hud.context.boarding",
               "boarding armed: «Продолжить абордаж» takes the slot without any script icon");
        bench.Set("contextAction", Json("Reload"));
        Expect(mount.shown.elements["context"]["icon"] == "grapple", "… and outranks the script's Reload icon");
        bench.Press("context");
        Expect(mount.Last().rfind("combat:5\t14\t", 0) == 0 && mount.Last().ends_with("\tinteract\ttap"),
               "it is the engine's action key (ChrAction)");
        bench.Set("contextAction", Json(""));
        bench.Set("combat", State("combat-explore"));
        Expect(!mount.Has("context"), "boarding continued: no button");
    }

    void Hints()
    {
        // the bottom hint: whom the talk button talks to, where the door ahead leads; shown at once,
        // held through a short loss (a passer-by at the edge of the cone, a door re-entered), then it
        // fades and goes
        bench.Frame(1.0);
        Expect(!mount.Has("hint"), "nothing to name: no hint");
        talk = State("combat-explore-talk");
        talk["interactName"] = "Хуан Перес";
        bench.Set("combat", talk);
        Expect(Hint("talk", "hud.context.talk: Хуан Перес", false),
               "the talk target named: " + mount.shown.elements["hint"].dump());
        bench.Set("combat", State("combat-explore"));
        Expect(Hint("talk", "hud.context.talk: Хуан Перес", false), "the target lost: the hint holds");
        bench.Frame(0.3);
        bench.Set("combat", talk);
        bench.Frame(0.4);
        Expect(Hint("talk", "hud.context.talk: Хуан Перес", false), "back within the hold: it never blinked");
        bench.Set("combat", State("combat-explore"));
        bench.Frame(0.3);
        Expect(Hint("talk", "hud.context.talk: Хуан Перес", false), "still within the hold");
        bench.Frame(0.25);
        Expect(Hint("talk", "hud.context.talk: Хуан Перес", true), "the hold ran out: it fades");
        bench.Frame(0.25);
        Expect(!mount.Has("hint"), "faded: gone");
        talk.erase("interactName");
        bench.Set("combat", talk);
        Expect(Hint("talk", "hud.context.talk", false), "a target without a name: the bare label");
        // the talk hint by the person's role: the game's dialog file of the target
        for (const auto &[dialog, icon] :
             std::vector<std::pair<const char *, const char *>>{{"common_store", "store"},
                                                                {"usurer_dialog", "usurer"},
                                                                {"enc_officer_dialog", "officer"},
                                                                {"common_tavern", "tavern"},
                                                                {"common_shipyard", "shipyard"},
                                                                {"common_church", "church"},
                                                                {"common_mayor", "residence"},
                                                                {"common_portman", "portoffice"},
                                                                {"smuggler agent_dialog", "sack"},
                                                                {"common_citizen", "talk"},
                                                                {"", "talk"}})
        {
            talk["interactName"] = "Жан";
            talk["interactDialog"] = dialog;
            bench.Set("combat", talk);
            Expect(Hint(icon, "hud.context.talk: Жан", false),
                   std::string("talk to '") + dialog + "': the " + icon + " icon");
        }
        talk.erase("interactDialog");
        talk.erase("interactName");
        bench.Set("combat", talk);
        bench.Set("combat", State("combat-explore"));
        bench.Set("contextAction", Json("Reload"));
        bench.Set("reloadPlace", Json{{"place", "таверна"}, {"locked", false}});
        Expect(Hint("unlock", "Таверна", false),
               "a door in front: where it leads, capitalised, an open lock: " + mount.shown.elements["hint"].dump());
        bench.Set("contextAction", Json(""));
        bench.Set("reloadPlace", Json{{"place", "резиденция"}, {"locked", true}});
        Expect(Hint("lock", "Резиденция", false) && !mount.Has("context"),
               "a locked door: a closed lock and no button: " + mount.shown.elements["hint"].dump());
        bench.Set("contextAction", Json("Reload"));
        bench.Set("reloadPlace", Json(nullptr));
        bench.Set("contextAction", Json(""));
        bench.Frame(0.6);
        bench.Frame(0.3);
        Expect(!mount.Has("hint"), "away from the door: gone");
        bench.Set("contextAction", Json("Reload"));
        Expect(Hint("door", "hud.context.enter", false), "a door the game names no place for: the button's own words");
    }

    void PrisonExit()
    {
        // the prison exit: a guard at the edge of the cone beside the door.
        // The hint says what the button does at every moment; a talk hint never holds over the door button.
        talk["interactName"] = "Эдмон Симон";
        bench.Set("combat", talk);
        Expect(mount.shown.elements["context"]["icon"] == "talk" &&
                   Hint("talk", "hud.context.talk: Эдмон Симон", false),
               "the guard in front: the talk button and its hint");
        bench.Set("combat", State("combat-explore"));
        Expect(mount.shown.elements["context"]["icon"] == "door" && Hint("door", "hud.context.enter", false),
               "the guard out of the cone: the door button and the door's hint at once: " +
                   mount.shown.elements["hint"].dump());
        Json armed = State("combat-explore-talk");
        armed["interactKind"] = "boarding";
        bench.Set("combat", armed);
        Expect(mount.shown.elements["context"]["icon"] == "grapple" && !mount.Has("hint"),
               "a button the hint has no words for: no hint");
        bench.Set("combat", State("combat-explore"));
        talk.erase("interactName");
        bench.Set("contextAction", Json(""));
        bench.Frame(1.0);
        bench.Set("contextAction", Json("Pick"));
        Expect(Hint("hand", "hud.context.take", false), "a thing to take: the action's own words");
        bench.Set("contextAction", Json("OpenBox"));
        Expect(Hint("chest", "hud.context.open", false), "a chest: the action's own words");
        bench.Set("contextAction", Json(""));
        bench.Frame(1.0);
        Expect(!mount.Has("hint"), "nothing at hand: gone");
        // The one named turns on the hero (the owner who saw him in a chest): the fight begins while
        // the talk hint is up. The combat layout has no hint: it goes at once, it does not hold into
        // land-combat (regression: a toast «the layout has no element 'hint'» each frame of its hold).
        bench.Set("combat", talk);
        Expect(mount.Has("hint"), "the talk hint is up");
        bench.Melee();
        Expect(mount.shown.layout == "land-combat" && !mount.Has("hint"),
               "the fight starts: no hint in the combat layout");
        bench.Frame(1.0);
        bench.Explore();
        Expect(!mount.Has("hint"), "nor a stale one after the fight");
    }

    void EventLines()
    {
        // event lines that lead somewhere (tagged by the game function that wrote them): a tap opens
        // that screen on its subject and dismisses the line; the others are only shown
        bench.hud.Notice("Получен новый уровень", "info", "character:12");
        bench.hud.Notice("Запись в судовом журнале", "info", "journal:Sharlie");
        bench.hud.Notice("Получено 500 пиастров", "info");
        bench.Frame();
        {
            const Json lines = mount.shown.elements["log"]["lines"];
            Expect(lines.size() == 3 && lines[0]["open"] == false && lines[1]["open"] == true &&
                       lines[2]["open"] == true,
                   "a line that leads somewhere says so (the ›); money does not: " + lines.dump());
            const auto key = [&](size_t i) { return lines[i]["key"].get<std::string>(); };
            bench.Press("log", "open", key(1));
            ExpectEqual(mount.Last(), "shortcut:journalscreen", "a quest record opens the journal");
            const auto journal = bench.router.Take("journal");
            Expect(NoteField(journal, "quest") == "Sharlie", "… on that quest");
            bench.Frame();
            Expect(mount.shown.elements["log"]["lines"][1]["leaving"] == true, "the tapped line is dismissed");
            bench.Press("log", "open", key(2));
            ExpectEqual(mount.Last(), "shortcut:characterscreen", "a new level opens the character screen");
            const auto character = bench.router.Take("character");
            Expect(NoteField(character, "character") == "12" && NoteField(character, "tab") == "perks",
                   "… of that character, on its abilities");
            const size_t sent = mount.commands.size();
            bench.Press("log", "open", key(0));
            Expect(mount.commands.size() == sent, "money opens nothing");
        }
        bench.hud.Notice("Вы получили предмет", "info", "items");
        bench.Frame();
        bench.Press("log", "open", mount.shown.elements["log"]["lines"][0]["key"].get<std::string>());
        ExpectEqual(mount.Last(), "shortcut:itemsscreen", "a taken item opens the inventory");
        bench.Frame(8.0);
    }

    void StaleCombat()
    {
        // a combat state of another revision is not ready: buttons stay, disabled, and send nothing
        Json stale = State("combat-explore");
        stale["gameplayRevision"] = 9;
        stale["activityGeneration"] = 6;
        bench.Set("combat", stale);
        Expect(mount.Has("weapon") && mount.shown.elements["weapon"]["disabled"] == true,
               "another revision: the weapon button is disabled");
        const size_t before = mount.commands.size();
        bench.Press("weapon");
        Expect(mount.commands.size() == before, "… and its tap sends nothing");
    }

    void Absent()
    {
        // no vitals, no location, no menu
        bench.Set("vitals", Json(nullptr));
        bench.Set("worldInfo", Json(""));
        bench.Set("landCommands", Json(nullptr));
        Expect(!mount.Has("party") && !mount.Has("effects") && !mount.Has("location") && !mount.Has("actions"),
               "absent states take their elements away");

        // without analog movement there is no stick, and that is said
        Json keyboard = State("gameplay-explore");
        keyboard["analogMovement"] = false;
        bench.Set("gameplay", keyboard);
        Expect(!mount.Has("move") && !mount.errors.empty() &&
                   mount.errors.back().find("no analog movement") != std::string::npos,
               "no analog movement: no stick, an error");
    }
};

static void CheckExplore()
{
    ExploreRun run;
    run.Party();
    run.Toasts();
    run.Buttons();
    run.QuickAction();
    run.Talking();
    run.Hints();
    run.PrisonExit();
    run.EventLines();
    run.StaleCombat();
    run.Absent();
}

static void CheckMovement()
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    mount.commands.clear();
    bench.Press("move", "change", "0,-100");
    ExpectEqual(mount.Last(), "move:town:42\t3\t1\t0\t1000", "stick up = forward, full travel");
    Expect(mount.slots.back() == "move", "the stick uses its latest-value slot");
    bench.Press("move", "change", "50,50");
    ExpectEqual(mount.Last(), "move:town:42\t3\t1\t500\t-500", "right and back");
    const size_t count = mount.commands.size();
    bench.Frame(0.5);
    bench.Frame(5.0);
    Expect(mount.commands.size() == count && bench.hud.Tick() > 5.0, "a held stick is said once: no heartbeat");

    // explore → melee keeps the stick: same owner and generation, nothing to say
    bench.Melee();
    bench.Frame(0.11);
    Expect(mount.commands.size() == count, "the stick survives the stance change without a word");
    // a new movement session (the engine's generation moved on) while the stick is held: the old
    // session gets its release, the held stick goes on in the new one
    Json renewed = State("gameplay-melee");
    renewed["movementGeneration"] = 4;
    bench.Set("gameplay", renewed);
    bench.Frame();
    Expect(mount.commands.size() == count + 2 && mount.commands[count] == "move:town:42\t3\t0\t0\t0",
           "the old session's stick is released: " + mount.commands[count]);
    ExpectEqual(mount.Last(), "move:town:42\t4\t1\t500\t-500", "the held stick goes on in the new generation");

    bench.Press("move", "change", "0,0");
    ExpectEqual(mount.Last(), "move:town:42\t4\t0\t0\t0", "release stops the hero");
    const size_t stopped = mount.commands.size();
    bench.Frame(0.5);
    Expect(mount.commands.size() == stopped, "no heartbeat after the release");

    // the platform cancels input while the stick is held
    bench.Press("move", "change", "100,0");
    bench.hud.Released();
    Expect(mount.commands.size() >= 2 && mount.commands[mount.commands.size() - 2] == "move:town:42\t4\t0\t0\t0",
           "cancelled input stops the hero");
    ExpectEqual(mount.Last(), "combat:6\t0\tall\tcancel", "… and cancels every combat hold");
    bench.Frame();

    // Regression: sheathing the weapon while running left the hero unable to stop. Every stick that is down ends with
    // exactly one release, whatever the HUD does between the push and the lift.
    const auto releases = [&](size_t from) {
        return std::count_if(mount.commands.begin() + static_cast<std::ptrdiff_t>(from), mount.commands.end(),
                             [](const std::string &c) { return c.rfind("move:", 0) == 0 && c.ends_with("\t0\t0\t0"); });
    };
    const auto presses = [&](size_t from) {
        return std::count_if(
            mount.commands.begin() + static_cast<std::ptrdiff_t>(from), mount.commands.end(),
            [](const std::string &c) { return c.rfind("move:", 0) == 0 && !c.ends_with("\t0\t0\t0"); });
    };
    Json live = State("gameplay-melee");
    live["movementGeneration"] = 4;
    // holster while the stick is held (melee → explore), keep holding, then lift
    {
        bench.Set("gameplay", live);
        bench.Feed("combat", "combat-melee");
        const size_t from = mount.commands.size();
        bench.Press("move", "change", "0,-100");
        bench.Frame(0.2);
        Json explore = State("gameplay-explore");
        explore["movementGeneration"] = 4;
        bench.Set("gameplay", explore);
        bench.Feed("combat", "combat-explore");
        bench.Frame(0.5);
        bench.Frame(1.0);
        Expect(presses(from) == 1 && releases(from) == 0, "holster under a held stick: it stays held, said once");
        bench.Press("move", "change", "0,0");
        Expect(releases(from) == 1 && mount.Last() == "move:town:42\t4\t0\t0\t0",
               "holster, then lift: one release: " + mount.Last());
    }
    // draw while the stick is held (explore → melee), then lift
    {
        const size_t from = mount.commands.size();
        bench.Press("move", "change", "0,-100");
        bench.Set("gameplay", live);
        bench.Feed("combat", "combat-melee");
        bench.Frame(0.5);
        bench.Press("move", "change", "0,0");
        Expect(presses(from) == 1 && releases(from) == 1, "draw under a held stick, then lift: one press, one release");
    }
    // the HUD stops being interactive under a held thumb (a scene fader): the stop is said at
    // once, the thumb lifted meanwhile says nothing more, and nothing resumes by itself after it
    // (the hidden HUD let go of the pointer: a new push moves the hero again)
    {
        const size_t from = mount.commands.size();
        bench.Press("move", "change", "0,-100");
        bench.Feed("presentation", "presentation-loading");
        Expect(releases(from) == 1 &&
                   std::find(mount.commands.begin(), mount.commands.end(), "presence:hidden") != mount.commands.end(),
               "a fader under a held stick: the hero stops at once");
        bench.Press("move", "change", "0,0"); // lifted under the fader
        bench.Feed("presentation", "presentation-game");
        bench.Frame();
        Expect(presses(from) == 1 && releases(from) == 1, "after the fader: nothing more, nothing resumes");
        bench.Press("move", "change", "0,-100");
        bench.Press("move", "change", "0,0");
        Expect(presses(from) == 2 && releases(from) == 2, "a new push after the fader: one press, one release");
    }
    // the movement session ends under a held thumb (a dialogue, a scripted move: the engine
    // reports no owner, the land scene goes) and a new one starts
    {
        const size_t from = mount.commands.size();
        bench.Press("move", "change", "0,-100");
        bench.Set("gameplay", State("gameplay-orders"));
        bench.Frame();
        Expect(presses(from) == 1 && releases(from) == 1, "the session ends under a held stick: its release is said");
        Json back = State("gameplay-explore");
        back["movementGeneration"] = 5;
        bench.Set("gameplay", back);
        bench.Feed("combat", "combat-explore");
        bench.Frame();
        Expect(presses(from) == 1, "the new session starts with the stick up");
        bench.Press("move", "change", "0,-100");
        ExpectEqual(mount.Last(), "move:town:42\t5\t1\t0\t1000", "a push is said in the new session");
        bench.Press("move", "change", "0,0");
        Expect(presses(from) == 2 && releases(from) == 2, "… and its lift too");
    }

    // in the background nothing moves
    bench.Feed("presentation", "presentation-background");
    const size_t quiet = mount.commands.size();
    bench.Press("move", "change", "100,0");
    bench.Frame(0.3);
    Expect(mount.commands.size() == quiet, "not live: the stick sends nothing");

    bench.Feed("presentation", "presentation-game");
    Expect(mount.visible, "a free game shows the HUD");
    bench.Feed("presentation", "presentation-loading"); // the engine's progress view (location load, transition)
    Expect(!mount.visible && mount.Last() == "presence:hidden", "loading: no HUD over the loading screen");
    bench.Feed("presentation", "presentation-game");
    Expect(mount.visible && mount.Last() == "presence:shown", "loaded: the HUD is back");
    bench.Press("move", "change", "sideways");
    Expect(!mount.errors.empty() && mount.errors.back().find("expected 'x,y'") != std::string::npos,
           "a malformed stick value is an error");
}

static void CheckCombat()
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    bench.Melee();
    ExpectEqual(mount.shown.layout, "land-combat", "melee uses the combat layout");
    Expect(mount.Has("strike") && mount.Has("block") && mount.Has("shot") && mount.Has("actions") &&
               mount.Has("abilities") && mount.Has("weapon") && !mount.Has("context") && !mount.Has("moves") &&
               !mount.Has("dash") && !mount.Has("effects"),
           "the combat set; no wheel until the strike is held; nothing without data");
    Expect(mount.shown.elements["hero"]["disabled"] == true, "no character screen in a fight");
    ExpectEqual(mount.shown.elements["weapon"]["label"].get<std::string>(), "hud.weapon.sheathe",
                "the weapon button sheathes now");
    Expect(mount.shown.elements["shot"]["badge"] == "1" && mount.shown.elements["shot"]["disabled"] == false &&
               !mount.shown.elements["shot"].contains("fill"),
           "a loaded pistol: charges as the badge, no fill");

    // In a fight (a boarding, a no-save zone) the menu button is the same one as in peace: the
    // game's own menu, which greys out what it does not allow there.
    mount.commands.clear();
    bench.Press("pause");
    ExpectEqual(mount.Last(), "shortcut:mainmenu", "the combat menu button opens the game menu, as in peace");
    mount.commands.clear();
    bench.Press("shot");
    ExpectEqual(mount.Last(), "combat:6\t21\tpistol\ttap", "the shot");
    bench.Press("block", "down");
    ExpectEqual(mount.Last(), "combat:6\t22\tblock\tdown", "block is held …");
    bench.Press("block", "up");
    ExpectEqual(mount.Last(), "combat:6\t22\tblock\tup", "… and let go");
    const size_t count = mount.commands.size();
    bench.Press("block", "click");
    bench.Press("strike", "click");
    Expect(mount.commands.size() == count && mount.errors.empty(),
           "the press of a held or swiped button needs no action");

    // the wheel
    bench.Press("strike", "open", "fast");
    Expect(mount.Has("moves") && mount.shown.elements["moves"]["selected"] == "centre" &&
               mount.shown.elements["moves"]["caption"] == "hud.move.fast" &&
               mount.shown.elements["moves"]["moves"].size() == 6,
           "holding the strike opens the wheel on the fast attack");
    Expect(mount.shown.dimmed == std::vector<std::string>({"strike", "block", "shot", "abilities", "weapon"}),
           "the combat cluster that is shown is dimmed under the wheel");
    bench.Press("strike", "preview", "break");
    Expect(mount.shown.elements["moves"]["selected"] == "2" &&
               mount.shown.elements["moves"]["caption"] == "hud.move.break",
           "a drag picks a sector and names the move");
    bench.Press("strike", "commit", "break");
    ExpectEqual(mount.Last(), "combat:6\t17\tbreak\ttap", "the release commits the move");
    Expect(!mount.Has("moves") && mount.shown.dimmed.empty(), "the wheel closes");
    bench.Press("strike", "open", "fast");
    bench.Press("strike", "cancel");
    Expect(!mount.Has("moves") && mount.Last() == "combat:6\t17\tbreak\ttap", "a cancelled gesture commits nothing");

    // a move the engine does not offer is drawn dim and cannot be committed
    Json fewer = State("combat-melee");
    fewer["actions"] = Json{{"weapon", 11}, {"fast", 16}, {"force", 15}, {"block", 22}};
    bench.Set("combat", fewer);
    bench.Press("strike", "open", "fast");
    Expect(mount.shown.elements["moves"]["moves"][0]["disabled"] == true &&
               mount.shown.elements["moves"]["moves"][4]["disabled"] == false,
           "unavailable moves are marked");
    const size_t sent = mount.commands.size();
    bench.Press("strike", "commit", "feint");
    Expect(mount.commands.size() == sent, "an unavailable move is not sent and nothing else is sent instead");
    Expect(!mount.Has("shot") && !mount.Has("abilities"), "no pistol and no potion action (nor Rush): no buttons");

    // reloading
    bench.Feed("combat", "combat-melee-reload");
    const Json &shot = mount.shown.elements["shot"];
    Expect(shot["disabled"] == true && shot["reloading"] == true && shot["fill"] == 40 && shot["fillTone"] == "reload",
           "reloading: the fill rises, no shot");
    const size_t reloading = mount.commands.size();
    bench.Press("shot");
    Expect(mount.commands.size() == reloading, "an unloaded pistol does not fire");
    Json holster = State("combat-melee");
    holster["pistol"] = Json{{"state", "no_weapon"}, {"progress", 0}};
    bench.Set("combat", holster);
    Expect(mount.Has("shot") && mount.shown.elements["shot"]["disabled"] == true &&
               mount.shown.elements["shot"]["reason"] == "hud.combat.noFirearm" &&
               !mount.shown.elements["shot"].contains("badge") && !mount.shown.elements["shot"].contains("fill"),
           "no firearm equipped: the shot button stays, inactive, with the reason a tap toasts: " +
               mount.shown.elements["shot"].dump());
    const size_t unarmed = mount.commands.size();
    bench.Press("shot");
    Expect(mount.commands.size() == unarmed, "a tap without a firearm fires nothing");

    // a new activity restarts the sequence
    Json next = State("combat-melee");
    next["activityGeneration"] = 9;
    bench.Set("combat", next);
    bench.Press("weapon");
    ExpectEqual(mount.Last(), "combat:9\t11\tweapon\ttap", "a new activity generation counts from one");
}

// The hero's status from the vitals channel: effects row, potion count, officers, dash.
static void CheckStatus()
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    bench.Feed("vitals", "vitals-status");
    const Json states = mount.shown.elements["effects"]["states"];
    Expect(states.size() == 2 && states[0]["icon"] == "skull" && states[0]["fillTone"] == "danger" &&
               states[1]["icon"] == "potion" && states[1]["label"] == "hud.effect.healing",
           "effects: one small icon each, in a fixed order");
    {
        // The game makes the hero walk at a full stick (the load, a quest: the game does not say which).
        LandBench walking;
        walking.Explore();
        Json walk = State("vitals-status");
        walk["effects"] = Json::array({"walkonly"});
        walking.Set("vitals", walk);
        const Json shown = walking.mount.shown.elements["effects"]["states"];
        Expect(shown.size() == 1 && shown[0]["icon"] == "lock" && shown[0]["label"] == "hud.effect.walkonly",
               "effects: walking only shows the lock");
    }
    bench.Press("abilities");
    Expect(mount.shown.elements["abilities-1"]["badge"] == "3" &&
               mount.shown.elements["abilities-1"]["fillTone"] == "active" &&
               mount.shown.elements["abilities-1"]["disabled"] == false,
           "potion: count and the healing fill on its button: " + mount.shown.elements["abilities-1"].dump());
    Json dry = State("vitals-status");
    dry["potions"] = 0;
    bench.Set("vitals", dry);
    Expect(mount.shown.elements["abilities-1"]["badge"] == "0" &&
               mount.shown.elements["abilities-1"]["disabled"] == true,
           "nothing to drink: greyed with its 0");
    const size_t drank = mount.commands.size();
    bench.Press("abilities-1", "refused");
    Expect(mount.commands.size() == drank, "… and a tap drinks nothing");
    bench.Feed("vitals", "vitals-status");
    bench.Press("abilities");
    Expect(!mount.Has("dash"), "explore: no dash");
    const Json party = mount.shown.elements["party"]["officers"];
    Expect(party.size() == 3 && party[0]["name"] == "Шарль де Мор" &&
               party[0]["texture"] == "INTERFACES/PORTRAITS/128/face_201" && party[0]["uv"] == "0,0,1,1" &&
               party[1]["texture"] == "INTERFACES/PORTRAITS/128/face_205" &&
               party[1]["bars"] == Json::parse(R"([{"kind":"health","value":84},{"kind":"energy","value":70}])") &&
               party[2]["texture"] == "battle_interface/two" && party[2]["uv"] == "0,0,0.5,1" &&
               party[2]["name"] == "Офицер Бета" && party[0]["tap"] == true && party[2]["tap"] == true,
           "explore: the party row; an officer without a face shows the game's figure");
    Expect(
        std::find(mount.logs.begin(), mount.logs.end(),
                  "land HUD: Офицер Бета (character 32) has no FaceId: the game's own figure battle_interface/two") !=
            mount.logs.end(),
        "… and that is said, naming the person");
    Json bare = State("vitals-status");
    bare["officers"][1].erase("picture");
    bench.Set("vitals", bare);
    Expect(mount.shown.elements["party"]["officers"][2]["texture"] == "" &&
               std::find(mount.logs.begin(), mount.logs.end(),
                         "land HUD: Офицер Бета (character 32) has no FaceId: the game's two.tga fallback is missing, "
                         "the placeholder stays") != mount.logs.end(),
           "without the game's figure either: the neutral placeholder, said aloud");
    // A tap on a member opens the character screen on that person.
    mount.commands.clear();
    bench.Press("party", "member", "2");
    ExpectEqual(mount.Last(), "shortcut:characterscreen", "a tap on an officer opens the character screen");
    auto subject = bench.router.Take("character");
    Expect(NoteField(subject, "character") == "32" && !bench.router.Take("character"),
           "… on that officer (character 32), taken once");
    bench.Press("party", "member", "0");
    subject = bench.router.Take("character");
    Expect(NoteField(subject, "character") == "1" && !bench.router.Take("ship"),
           "the hero's portrait: the character screen on the hero");
    bench.Press("party", "member", "0");
    bench.Frame(Hud::kSubjectSeconds + 1.0);
    Expect(!bench.router.Take("character") && !bench.surface.logs.empty() &&
               bench.surface.logs.back().find("lapsed") != std::string::npos,
           "a request no screen takes in time lapses, and that is said");

    Json combat = State("combat-melee");
    combat["actions"]["dash"] = 23;
    bench.Feed("gameplay", "gameplay-melee");
    bench.Set("combat", combat);
    const Json officers = mount.shown.elements["party"]["officers"];
    Expect(officers.size() == 3 && officers[1]["bars"][0]["value"] == 84 && !officers[0].contains("tap") &&
               !officers[1].contains("tap"),
           "combat: the same party row; no taps (the game refuses the character screen in a fight)");
    Expect(mount.shown.elements["dash"]["fill"] == 40 && mount.shown.elements["dash"]["fillTone"] == "cooldown" &&
               mount.shown.elements["dash"]["disabled"] == true,
           "dash cooling down: filled and not pressable");
    bench.Feed("worldInfo", "world-info");
    Expect(!mount.Has("opponent") && mount.Has("location"), "no cabin captain: no captain bar, the location line");
    Json cabin = State("vitals-status");
    cabin["opponent"] = Json{{"name", "Клод Дюран"}, {"health", 0.62}};
    bench.Set("vitals", cabin);
    Expect(mount.shown.elements["opponent"]["title"] == "hud.captain Клод Дюран" &&
               mount.shown.elements["opponent-hp"]["value"] == 62,
           "cabin fight: the enemy captain's name and health on top");
    Expect(!mount.Has("location"), "the cabin captain takes the place of the location line");
    Expect(!mount.Has("boarding"), "no deck fight: no boarding bars");
    Json deck = State("vitals-status");
    deck["boarding"] = Json{{"ours", 63}, {"theirs", 30}, {"oursStart", 84}, {"theirsStart", 120}};
    bench.Set("vitals", deck);
    Expect(mount.Has("boarding") && mount.shown.elements["boarding"]["ours"] == 75 &&
               mount.shown.elements["boarding"]["theirs"] == 25 &&
               mount.shown.elements["boarding"]["oursCount"] == 63 &&
               mount.shown.elements["boarding"]["theirsCount"] == 30,
           "deck fight: both sides' men as shares of the fight's start, with the men left");
    Expect(!mount.Has("location") && mount.shown.elements["boarding"].contains("title"),
           "the deck (the location line) is the bars' title");
    deck["opponent"] = Json{{"name", "Клод Дюран"}, {"health", 0.62}};
    bench.Set("vitals", deck);
    Expect(mount.Has("opponent") && !mount.Has("boarding"), "the cabin captain, not the deck bars");
    deck.erase("opponent");
    deck["boarding"]["oursStart"] = 0;
    bench.Set("vitals", deck);
    Expect(!mount.errors.empty(), "a boarding without a start is rejected aloud");
    mount.errors.clear();
    cabin["opponent"] = Json{{"name", ""}, {"health", 0.5}};
    bench.Set("vitals", cabin);
    Expect(!mount.errors.empty(), "an opponent without a name is rejected aloud");
    mount.errors.clear();
    Json ready = State("vitals-status");
    ready["dash"] = 0;
    bench.Set("vitals", ready);
    Expect(mount.shown.elements["dash"]["disabled"] == false && !mount.shown.elements["dash"].contains("fill"),
           "dash ready");
    mount.commands.clear();
    bench.Press("dash");
    ExpectEqual(mount.Last(), "combat:6\t23\tdash\ttap", "the dash is a combat action");
    ready.erase("dash");
    bench.Set("vitals", ready);
    Expect(!mount.Has("dash"), "no dash outside fight mode");
}

// The land command menu's sheet on one HUD, from opening through its stages to closing.
struct SheetRun
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    Json blocked; // gameplay while the engine's menu is open, from WhileBlocked on

    void Opens()
    {
        bench.Explore();
        mount.commands.clear();

        bench.Feed("landCommands", "land-commands-root");
        Expect(mount.shown.sheet == "s-hud-sheet" && mount.shown.modal && mount.Has("party") && !mount.Has("move") &&
                   !mount.Has("hero"),
               "an open menu: the sheet over the bars, no controls");
        const Json &props = mount.shown.sheet_props;
        Expect(props["title"] == "hud.sheet.actions" && props["back"] == false && props["layout"] == "rows" &&
                   props["rows"].size() == 5,
               "the root stage: ✕ only, five rows");
        Expect(props["rows"][0]["id"] == "c2" && props["rows"][0]["icon"] == "potion" &&
                   props["rows"][0]["chevron"] == true && props["rows"][1]["icon"] == "thought" &&
                   props["rows"][1]["chevron"] == false && props["rows"][2]["disabled"] == true,
               "rows: icon by command, › for stages, disabled as the engine says");
        bench.Set("vitals", Json{{"health", 0.5}, {"energy", 0.5}});
        bench.Frame();
        bench.Frame(0.1);
        bench.Frame(5.0);
        Expect(mount.commands.empty(), "the sheet sends the engine nothing");
        Expect(bench.hud.Tick() > 5.0, "… and the sheet asks for no frames");
    }

    void WhileBlocked()
    {
        // the engine reports no gameplay mode while its menu is open: the sheet stays, nothing is closed
        blocked = State("gameplay-explore");
        blocked["mode"] = "unknown";
        blocked["revision"] = 20;
        const size_t open = mount.commands.size();
        bench.Set("gameplay", blocked);
        Expect(mount.visible && mount.shown.modal && mount.commands.size() == open,
               "gameplay blocked by the open menu: the sheet stays as it is");
        bench.Feed("gameplay", "gameplay-explore");
    }

    void TapActs()
    {
        // a tap acts at once: select, then activate against the revision that reports the selection
        bench.Press("c3");
        ExpectEqual(mount.Last(), "landcommands:11\tselectItem\tc3", "a tap selects the row …");
        Expect(mount.shown.sheet_props["rows"][1]["pressed"] == true, "… which is drawn pressed");
        bench.Press("c1");
        ExpectEqual(mount.Last(), "landcommands:11\tselectItem\tc3", "another tap waits for the first");
        bench.Feed("landCommands", "land-commands-root-selected");
        bench.Frame();
        ExpectEqual(mount.Last(), "landcommands:12\tactivateItem\tc3", "… and the selected revision is activated");
        bench.Press("c4");
        ExpectEqual(mount.Last(), "landcommands:12\tactivateItem\tc3", "a disabled row does nothing");
    }

    void NextStage()
    {
        // the next stage
        bench.Feed("landCommands", "land-commands-travel");
        {
            const Json &tiles = mount.shown.sheet_props["rows"];
            std::string icons;
            for (const Json &tile : tiles)
                icons += tile["icon"].get<std::string>() + " ";
            Expect(mount.shown.sheet_props["layout"] == "tiles" && mount.shown.sheet_props["back"] == false &&
                       mount.shown.sheet_props["title"] == "Переход" &&
                       icons == "store shipyard tavern residence usurer church lantern portoffice prison port " &&
                       tiles[0]["pressed"] == false,
                   "travel: tiles, ✕, place icons by the place's picture, the port last: " + icons);
        }
        {
            // another town lists its places in another order and offers no port: the kinds keep their order
            Json other = State("land-commands-travel");
            Json reversed = Json::array();
            for (auto item = other["items"].rbegin(); item != other["items"].rend(); ++item)
                if ((*item)["source"] != "port")
                    reversed.push_back(*item);
            other["items"] = reversed;
            bench.Set("landCommands", other);
            std::string icons;
            for (const Json &tile : mount.shown.sheet_props["rows"])
                icons += tile["icon"].get<std::string>() + " ";
            ExpectEqual(icons, std::string("store shipyard tavern residence usurer church lantern portoffice prison "),
                        "the travel tiles in the same order in every town");
            // the hero stands in the shipyard: the game leaves it out of the list, the sheet shows its tile in its
            // slot, marked
            Json inside = State("land-commands-travel");
            Json kept = Json::array();
            for (const Json &item : inside["items"])
                if (item["picture"] != "landtarget_slb:1")
                    kept.push_back(item);
            inside["items"] = kept;
            inside["here"] = Json{{"picture", "landtarget_slb:1"}, {"label", "Верфь"}};
            bench.Set("landCommands", inside);
            const Json &marked = mount.shown.sheet_props["rows"];
            icons.clear();
            for (const Json &tile : marked)
                icons += tile["icon"].get<std::string>() + " ";
            ExpectEqual(icons,
                        std::string("store shipyard tavern residence usurer church lantern portoffice prison port "),
                        "the hero's place keeps its slot");
            Expect(marked[1]["id"] == "here" && marked[1]["title"] == "Верфь" && marked[1]["here"] == true &&
                       marked[1]["disabled"] == true && !marked[1].contains("sub") &&
                       marked[0].value("here", false) == false,
                   "the hero's place is marked and not tappable: " + marked[1].dump());
            const size_t before = mount.commands.size();
            const size_t errors = mount.errors.size();
            bench.Press("here", "refused");
            Expect(mount.commands.size() == before && mount.errors.size() == errors,
                   "a tap on the marked tile shakes it and asks the game nothing");
            bench.Feed("landCommands", "land-commands-travel");
        }
        bench.Frame();
        Expect(bench.hud.Back() && mount.Last() == "landcommands:13\tclose\t",
               "the back key closes the transitions: nothing to go back to");
        bench.Press("sheet", "close");
        ExpectEqual(mount.Last(), "landcommands:13\tclose\t", "a tap beside the sheet closes it");

        bench.Feed("landCommands", "land-commands-potions");
        const Json &rows = mount.shown.sheet_props["rows"];
        Expect(mount.shown.sheet_props["layout"] == "dense" && rows.size() == 4 &&
                   rows[0]["heading"] == "hud.sheet.potions" && !rows[1].contains("heading") &&
                   rows[3]["heading"] == "hud.sheet.abilities" && rows[3]["cooldown"] == 60 &&
                   rows[3]["disabled"] == true && rows[0]["cooldown"] == 0,
               "potions and abilities under headings; a recovering ability shows what is left");
        Expect(mount.shown.sheet_props["back"] == true, "a nested stage of the actions: ←");
        bench.Press("sheet-back");
        ExpectEqual(mount.Last(), "landcommands:14\tback\t", "← goes back a stage");
        Expect(bench.hud.Back() && mount.Last() == "landcommands:14\tback\t",
               "the back key does the same in a nested stage");
    }

    void Closing()
    {
        // a tap waits for the engine's answer, however long, and a refusal ends it at once
        bench.Frame();
        bench.Press("p1");
        Expect(mount.shown.sheet_props["rows"][0]["pressed"] == true, "pressed while waiting");
        bench.Frame(2.0);
        Expect(mount.shown.sheet_props["rows"][0]["pressed"] == true, "still pressed while unanswered");
        bench.Answer(false);
        Expect(mount.shown.sheet_props["rows"][0]["pressed"] == false, "the refusal ends the wait");

        // the root stage closes on the back key
        bench.Set("landCommands", Json(nullptr));
        bench.Feed("landCommands", "land-commands-root");
        bench.Frame();
        Expect(bench.hud.Back() && mount.Last() == "landcommands:11\tclose\t", "the back key closes the root stage");
        bench.Press("sheet-close");
        ExpectEqual(mount.Last(), "landcommands:11\tclose\t", "✕ closes");

        // losing the screen: the menu is closed
        mount.commands.clear();
        bench.Feed("presentation", "presentation-background");
        ExpectEqual(mount.Last(), "landcommands:11\tclose\t", "in the background the menu is closed");
        const size_t closed = mount.commands.size();
        bench.Frame(0.3);
        Expect(mount.commands.size() == closed, "… and nothing more is said while not live");
        bench.Feed("presentation", "presentation-game");
        bench.Frame();

        // the menu closed while gameplay is still blocked: the scene has nothing to hold on to
        bench.Set("gameplay", blocked);
        bench.Set("landCommands", Json(nullptr));
        Expect(mount.visible && mount.shown.layout == "menu" && mount.Last() == "presence:menu",
               "no menu and no mode: only the game-menu button, and the platform host hears it");
        bench.Feed("gameplay", "gameplay-explore");
        Expect(mount.visible && mount.Last() == "presence:shown", "explore again: the HUD is back and says so");
    }

    void PanelUnderFinger()
    {
        // a panel that opens under the finger (regression: one of its buttons showed as already held): the menu goes
        // on to the next stage while a tap of the last one waits, and the new stage reuses the row ids
        bench.Set("gameplay", State("gameplay-explore"));
        bench.Set("landCommands", Json(nullptr));
        bench.Feed("landCommands", "land-commands-root");
        bench.Frame();
        bench.Press("c2");
        ExpectEqual(mount.Last(), "landcommands:11\tselectItem\tc2", "a row tapped");
        bench.Press("c3");
        ExpectEqual(mount.Last(), "landcommands:11\tselectItem\tc3",
                    "a second tap before the engine answered is not swallowed: it replaces the first");
        {
            Json next = State("land-commands-travel");
            next["items"][1]["id"] = "c3"; // the same id, another stage
            bench.Set("landCommands", next);
            bool pressed = false;
            for (const Json &row : mount.shown.sheet_props["rows"])
                pressed = pressed || row["pressed"] == true;
            Expect(!pressed, "the new stage opens with nothing pressed: " + mount.shown.sheet_props["rows"].dump());
            bench.Frame();
            bench.Press("c3");
            ExpectEqual(mount.Last(), "landcommands:13\tselectItem\tc3", "the first tap on the new stage acts");
            bench.Press("t3");
            ExpectEqual(mount.Last(), "landcommands:13\tselectItem\tt3",
                        "and so does a change of mind before the engine answers");
        }
    }

    void Unshowable()
    {
        // a menu the sheet cannot show is closed and reported: the engine never draws its own
        bench.Set("landCommands", Json(nullptr));
        mount.commands.clear();
        bench.Feed("landCommands", "land-commands-unknown-kind");
        bench.Frame();
        Expect(mount.shown.sheet.empty() && !mount.shown.modal && !mount.Has("move") && mount.commands.size() == 1 &&
                   mount.Last() == "landcommands:15\tclose\t" && !mount.errors.empty() &&
                   mount.errors.back().find("not drawable here") != std::string::npos && !bench.hud.Back(),
               "an undrawable menu: closed once, and that is reported");
    }
};

static void CheckSheet()
{
    SheetRun run;
    run.Opens();
    run.WhileBlocked();
    run.TapActs();
    run.NextStage();
    run.Closing();
    run.PanelUnderFinger();
    run.Unshowable();
}

// The land HUD's touches on one runtime: what is the UI's, the stick, transitions, two pointers, combat, a fader under
// the thumb, a modal sheet, what covers the game and loud errors.
struct LandTouches
{
    Runtime &run;
    Host &host;

    void Layout()
    {
        Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "nothing shown: every touch is the game's");
        run.Explore();

        // The application's layout at 800 × 400 dp: the move zone at 0…336 × 176…400, the travel button at 616…668 ×
        // 348…400, the hint box at 164…588 × 348…388; weapon at 744…796 × 348…400, context at 728…792 × 270…334, hero
        // at 682…730 × 8…56, the party at 12…230 × 10…60 (its portraits open the character screen), the location line
        // at 488…788 from 62 down.
        Expect(host.WantsTouch(90, 320), "the stick is the UI's");
        Expect(host.WantsTouch(750, 360) && host.WantsTouch(740, 290) && host.WantsTouch(700, 30),
               "buttons are the UI's");
        Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(500, 380), "the free scene is the game's");
        Expect(!host.WantsTouch(550, 20) && !host.WantsTouch(500, 370),
               "the location line and the hint show, they do not take touches");
        Expect(host.WantsTouch(240, 320) && host.WantsTouch(20, 390) && host.WantsTouch(330, 180),
               "the whole move zone is the UI's, not just the stick");
        Expect(!host.WantsTouch(340, 320) && !host.WantsTouch(100, 170), "just beside the zone is the game's");
    }

    void FloatingStick()
    {
        // the floating stick: it appears under the thumb, wherever in the zone that lands
        const size_t before = run.commands.size();
        host.Touch(TouchPhase::Down, 1, 250, 300);
        run.Frame();
        Expect(run.commands.size() == before, "a thumb that only landed moves nobody");
        host.Touch(TouchPhase::Move, 1, 250, 260);
        Expect(run.Last() == "move:town:42\t3\t1\t0\t1000",
               "pushed up from where it landed: straight forward, not towards the place of rest: " + run.Last());
        Expect(!host.WantsTouch(400, 200) && !host.WantsTouch(340, 320),
               "a held stick leaves the scene to the camera finger");
        host.Touch(TouchPhase::Down, 2, 400,
                   200); // the game's finger: the UI was not asked to take it, and does not act on it
        host.Touch(TouchPhase::Up, 2, 400, 200);
        Expect(run.Last() == "move:town:42\t3\t1\t0\t1000", "a second finger outside the zone changes nothing");
        host.Touch(TouchPhase::Up, 1, 250, 260);
        Expect(run.Last() == "move:town:42\t3\t0\t0\t0", "letting go stops the hero: " + run.Last());
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 60, 200); // another place, over nothing
        host.Touch(TouchPhase::Move, 1, 100, 200);
        Expect(run.Last() == "move:town:42\t3\t1\t1000\t0", "the next thumb gets its own base: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 100, 200);
        run.Frame();
        host.Touch(TouchPhase::Down, 1, 200, 235); // on the log box, which only shows
        host.Touch(TouchPhase::Move, 1, 200, 275);
        Expect(run.Last() == "move:town:42\t3\t1\t0\t-1000",
               "a box that only shows does not shield the zone: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 200, 275);
        run.Frame();
        const size_t moves = run.commands.size();
        host.Touch(TouchPhase::Down, 1, 642, 374); // the travel button
        host.Touch(TouchPhase::Up, 1, 642, 374);
        Expect(run.commands.size() == moves + 1 && run.Last() == "landcommands:10\topen\tBI_FastReload",
               "the travel button opens the transitions: " + run.Last());
    }

    void Transitions()
    {
        // the transitions open over where the finger was: the first tap on a tile acts, on every tile
        // (the port, t1, goes last: the tiles from the top are t2…)
        run.Feed("landCommands", "land-commands-travel");
        run.Frame();
        run.Frame();
        for (int tile = 0; tile < 4; ++tile)
        {
            // two columns of tiles 164 × 52 from 440, 84, a step of 172 across and 60 down
            const float x = tile % 2 == 0 ? 522.0f : 694.0f, y = 110.0f + 60.0f * static_cast<float>(tile / 2);
            host.Touch(TouchPhase::Down, 1, x, y);
            host.Touch(TouchPhase::Up, 1, x, y);
            run.Frame();
            Expect(run.Last() == "landcommands:13\tselectItem\tt" + std::to_string(tile + 2),
                   "a single tap on tile " + std::to_string(tile + 1) +
                       " of a freshly opened panel acts: " + run.Last());
        }
        Expect(host.SetState("landCommands", "null"), "the transitions closed");
        run.Feed("landCommands", "land-commands-idle");
        run.commands.clear();
        run.Frame();
    }

    void TwoPointers()
    {
        // two pointers at once: the stick held, a button tapped
        host.Touch(TouchPhase::Down, 1, 90, 320);
        host.Touch(TouchPhase::Move, 1, 90, 280);
        run.Frame();
        Expect(run.Last() == "move:town:42\t3\t1\t0\t1000",
               "the stick at its place of rest moves the hero forward: " + run.Last());
        Expect(host.WantsTouch(750, 360) && !host.WantsTouch(400, 200),
               "a held stick changes nothing for the other pointers");
        host.Touch(TouchPhase::Down, 2, 750, 360);
        host.Touch(TouchPhase::Up, 2, 750, 360);
        Expect(run.Sent("combat:5\t11\tweapon\ttap"), "a second finger taps the weapon while the stick is held");
        const size_t held = run.commands.size();
        run.Frame(3.0);
        Expect(run.commands.size() == held && !run.Sent("move:town:42\t3\t0\t0\t0"),
               "the stick stays held through a slow frame, without a heartbeat");

        // the platform takes the gestures away
        host.CancelInput();
        Expect(run.Sent("move:town:42\t3\t0\t0\t0"), "cancelled input releases the stick");
        const size_t count = run.commands.size();
        host.Touch(TouchPhase::Move, 1, 90, 300);
        host.Touch(TouchPhase::Up, 1, 90, 300);
        Expect(run.commands.size() == count, "the cancelled pointer is over");
    }

    void Combat()
    {
        // combat: a held block and a swiped strike, each with its own finger
        run.Feed("gameplay", "gameplay-melee");
        run.Feed("combat", "combat-melee");
        run.Frame();
        run.Frame();
        Expect(host.WantsTouch(672, 282) && host.WantsTouch(44, 216), "strike and block are the UI's");
        // block sits left inside the floating stick's zone: below it the zone still starts the stick
        run.commands.clear();
        host.Touch(TouchPhase::Down, 4, 110, 300);
        host.Touch(TouchPhase::Move, 4, 110, 270);
        Expect(run.Last().rfind("move:", 0) == 0, "the stick zone below the block is the stick's: " + run.Last());
        host.Touch(TouchPhase::Up, 4, 110, 270);
        run.Frame();
        run.commands.clear();
        host.Touch(TouchPhase::Down, 1, 44, 216);
        Expect(run.Last() == "combat:6\t22\tblock\tdown", "block goes down with the finger: " + run.Last());
        host.Touch(TouchPhase::Move, 1, 200, 60); // far beyond a tap's slop: still held
        host.Touch(TouchPhase::Down, 2, 672, 282);
        host.Touch(TouchPhase::Move, 2, 720, 290); // right of the start: the thrust sector
        run.Frame();
        host.Touch(TouchPhase::Up, 2, 720, 290);
        Expect(run.Last() == "combat:6\t17\tbreak\ttap", "a swipe from the strike picks the move: " + run.Last());
        host.Touch(TouchPhase::Up, 1, 200, 60);
        Expect(run.Last() == "combat:6\t22\tblock\tup", "block is let go with its finger: " + run.Last());
        run.Frame();
        host.Touch(TouchPhase::Down, 2, 672, 282);
        host.Touch(TouchPhase::Up, 2, 672, 282);
        Expect(run.Last() == "combat:6\t16\tfast\ttap", "a plain tap on the strike is the fast attack: " + run.Last());

        // a stick held through the stance change keeps working (the element is not remounted)
        host.Touch(TouchPhase::Down, 3, 90, 320);
        host.Touch(TouchPhase::Move, 3, 130, 320);
        run.Feed("gameplay", "gameplay-explore");
        run.Feed("combat", "combat-explore");
        run.Frame(0.12);
        Expect(run.Last().rfind("move:town:42\t3\t", 0) == 0 && run.Last().find("\t1\t1000\t0") != std::string::npos,
               "the stick survives melee → explore: " + run.Last());
        host.Touch(TouchPhase::Up, 3, 130, 320);
        Expect(run.Last().rfind("move:town:42\t3\t", 0) == 0 && run.Last().ends_with("\t0\t0\t0"),
               "the thumb lifted after melee → explore stops the hero: " + run.Last());
    }

    void Fader()
    {
        // a fader under a held thumb: the hidden HUD lets the pointer go and the hero stops
        host.Touch(TouchPhase::Down, 3, 90, 320);
        host.Touch(TouchPhase::Move, 3, 130, 320);
        const size_t pushed = run.commands.size();
        run.Feed("presentation", "presentation-loading");
        run.Frame();
        Expect(run.Sent("presence:hidden") &&
                   std::count_if(
                       run.commands.begin() + static_cast<std::ptrdiff_t>(pushed), run.commands.end(),
                       [](const std::string &c) { return c.rfind("move:", 0) == 0 && c.ends_with("\t0\t0\t0"); }) == 1,
               "a fader under a held stick stops the hero, once");
        const size_t faded = run.commands.size();
        host.Touch(TouchPhase::Up, 3, 130, 320);
        run.Feed("presentation", "presentation-game");
        run.Frame();
        Expect(std::none_of(run.commands.begin() + static_cast<std::ptrdiff_t>(faded), run.commands.end(),
                            [](const std::string &c) { return c.rfind("move:", 0) == 0; }),
               "the thumb lifted under the fader says nothing more");
    }

    void ModalSheet()
    {
        // an open sheet is modal: every touch and the back key are the UI's
        run.Feed("landCommands", "land-commands-root");
        run.Frame();
        run.Frame();
        Expect(host.WantsTouch(400, 200) && host.WantsTouch(5, 5) && host.WantsBack(),
               "an open sheet takes every touch and the back key");
        host.Touch(TouchPhase::Down, 1, 700, 84); // the first row
        host.Touch(TouchPhase::Up, 1, 700, 84);
        Expect(run.Last() == "landcommands:11\tselectItem\tc2", "a tap on a row: " + run.Last());
        host.Touch(TouchPhase::Down, 1, 100, 300); // beside the rows
        host.Touch(TouchPhase::Up, 1, 100, 300);
        Expect(run.Last() == "landcommands:11\tclose\t", "a tap beside the sheet closes it: " + run.Last());
        host.Back();
        Expect(run.Last() == "landcommands:11\tclose\t", "the back key closes the root stage");
        run.Feed("landCommands", "land-commands-idle");
        Expect(host.SetState("landCommands", "null"), "the menu is gone");
        run.Frame();
        Expect(!host.WantsTouch(400, 200) && !host.WantsBack(), "the sheet closed: the scene is the game's again");
    }

    void Covered()
    {
        // what covers the game, or a game in the background, leaves nothing to touch
        run.Feed("presentation", "presentation-menu");
        run.Frame();
        Expect(!host.WantsTouch(90, 320) && !host.WantsTouch(750, 360),
               "under a menu of the platform host the HUD is gone");
        run.Feed("presentation", "presentation-background");
        run.Frame();
        Expect(!host.WantsTouch(90, 320), "not live: drawn, but nothing is claimed");
        run.Feed("presentation", "presentation-game");
        run.Frame();
        Expect(host.WantsTouch(90, 320), "live again");

        // a mode without a scene (every gameplay mode has one now: land, sea, world map)
        run.Feed("gameplay", "gameplay-orders");
        run.Frame();
        Expect(!host.WantsTouch(90, 320), "a mode without a scene takes the land HUD away");

        // safe area: the layer moves in, and the claimed places with it
        run.Feed("gameplay", "gameplay-explore");
        host.SetSafeArea(50, 0, 30, 10);
        run.Frame();
        run.Frame();
        Expect(host.WantsTouch(140, 310) && !host.WantsTouch(40, 380) && !host.WantsTouch(140, 395) &&
                   host.WantsTouch(720, 350) && !host.WantsTouch(775, 385),
               "the safe area shifts the elements and what the UI claims");
    }

    void Errors()
    {
        // errors are loud
        Expect(!host.SetState("vitals", "{broken"), "malformed JSON is refused");
    }
};

static void CheckTouches(const std::string &pack)
{
    Runtime run(pack);
    if (!run.host)
        return;
    AttachScripts(run);
    LandTouches touches{run, *run.host};
    touches.Layout();
    touches.FloatingStick();
    touches.Transitions();
    touches.TwoPointers();
    touches.Combat();
    touches.Fader();
    touches.ModalSheet();
    touches.Covered();
    touches.Errors();
}

// One host per process (RmlUi state is process-global): a second one is refused aloud, and
// the slot is free again once the first is gone.
static void CheckOneHostPerProcess(const std::string &pack)
{
    {
        Runtime first(pack);
        Config config;
        config.strict_templates = true; // a template path that names nothing is an error
        config.files = first.files.get();
        config.backend = &first.backend;
        config.width = 800;
        config.height = 400;
        std::string error;
        Expect(Host::Create(config, &error) == nullptr && error.find("one host per process") != std::string::npos,
               "a second host in the same process is refused with an error");
    }
    Runtime again(pack);
    Expect(again.host != nullptr, "after the first host is gone a new one starts");
}

// Every place of the travel stage gets the icon of what it is, by the game's picture (the towns of the game itself:
// hud_land_game_check.cpp); a picture outside the table, another texture or a slot name keeps the travel icon.
static void CheckPlaceIcons()
{
    LandBench bench;
    bench.Explore();
    Json travel = State("land-commands-travel");
    Json items = Json::array();
    const char *const pictures[] = {"landtarget_slb:0",  "landtarget_slb:1",
                                    "landtarget_slb:2",  "landtarget_slb:3",
                                    "landtarget_slb:4",  "landtarget_slb:5",
                                    "landtarget_slb:6",  "landtarget_slb:7",
                                    "landtarget_slb:8",  "landtarget_slb:9",
                                    "landtarget_slb:10", "landtarget_slb:11",
                                    "landcommands:0",    "fr_1"};
    for (size_t k = 0; k < std::size(pictures); ++k)
    {
        Json item = travel["items"][0];
        item["id"] = "t" + std::to_string(k);
        item["picture"] = pictures[k];
        items.push_back(item);
    }
    for (const Json &item : travel["items"])
        if (item["kind"] == "cancel")
            items.push_back(item);
    travel["items"] = items;
    travel.erase("here");
    bench.Set("landCommands", travel);
    std::string icons;
    for (const Json &tile : bench.mount.shown.sheet_props["rows"])
        icons += tile["icon"].get<std::string>() + " ";
    for (const std::string &error : bench.mount.errors)
        std::printf("error: %s\n", error.c_str());
    ExpectEqual(icons,
                std::string("store shipyard tavern residence usurer church lantern portoffice prison fort port travel "
                            "travel travel "),
                "the places by their pictures, in the order of their kinds; unknown pictures last, the travel icon");
}

// No frame of the land HUD between choosing a transition and the game's fader (regression: leaving
// the transition dialog flashed the land HUD): the travel panel closes, the HUD
// stays hidden until the engine reports the fader (gameplay blocked "transition"), through it,
// and comes back with the new scene. A door tapped likewise. A change that does not come gives
// the HUD back after Hud::kTransitionWait, with an error line.
static void CheckTransitionFlash()
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    bench.Feed("landCommands", "land-commands-travel");
    bench.Frame();
    Expect(mount.visible && mount.shown.sheet == "s-hud-sheet", "the transitions are open");
    {
        Json selected = State("land-commands-travel");
        selected["selectedItemId"] = "t4";
        bench.Set("landCommands", selected);
    }
    bench.Frame();
    bench.Press("t4");
    Expect(std::find(mount.commands.begin(), mount.commands.end(), "landcommands:13\tactivateItem\tt4") !=
                   mount.commands.end() &&
               !mount.visible && mount.Last() == "presence:hidden",
           "the tavern tile goes at once (it is selected already), and the HUD goes with the panel: " + mount.Last());
    // the engine closes its menu in the same frame as its fader starts; the menu's news may come first
    bench.Feed("landCommands", "land-commands-idle");
    {
        Json idle = State("land-commands-idle");
        idle["revision"] = 20;
        bench.Set("landCommands", idle);
    }
    bench.Frame();
    Expect(!mount.visible, "the panel closed before the fader: no land HUD in between");
    Json fading = State("gameplay-explore");
    fading["mode"] = "unknown";
    fading["blocked"] = "transition";
    fading["revision"] = 30;
    bench.Set("gameplay", fading);
    bench.Frame(0.5);
    bench.Frame(2.0);
    Expect(!mount.visible && mount.errors.empty(), "the fader is up: hidden, also past the wait (the game took over)");
    Json loading = fading;
    loading["blocked"] = "loading";
    loading["revision"] = 31;
    bench.Set("gameplay", loading);
    Expect(!mount.visible, "loading: hidden");
    Json arrived = State("gameplay-explore");
    arrived["revision"] = 32;
    bench.Set("gameplay", arrived);
    bench.Frame();
    Expect(mount.visible && mount.shown.layout == "land" && mount.shown.sheet.empty(),
           "the new scene: the HUD is back");

    // a place chosen whose change never comes: the HUD comes back after the wait, said loudly
    bench.Set("landCommands", Json(nullptr));
    bench.Feed("landCommands", "land-commands-travel");
    {
        Json selected = State("land-commands-travel");
        selected["selectedItemId"] = "t4";
        bench.Set("landCommands", selected);
    }
    bench.Frame();
    bench.Press("t4");
    {
        Json idle = State("land-commands-idle");
        idle["revision"] = 40;
        bench.Set("landCommands", idle);
    }
    bench.Frame();
    Expect(!mount.visible, "waiting for the change");
    const size_t errors = mount.errors.size();
    bench.Frame(hud::Hud::kTransitionWait + 0.1);
    Expect(mount.visible && mount.errors.size() == errors + 1 &&
               mount.errors.back().find("Таверна") != std::string::npos,
           "no transition came: the HUD is back after the wait, with an error line");
    // Native refused the activation after revalidating its candidates. The HUD must
    // release the speculative cover immediately and report this cause, not a reload timeout.
    bench.Set("landCommands", Json(nullptr));
    Json selected = State("land-commands-travel");
    selected["selectedItemId"] = "t4";
    bench.Set("landCommands", selected);
    bench.Frame();
    bench.Press("t4");
    Expect(!mount.visible, "activation awaiting native receipt covers the HUD");
    Json receipt{{"serial", 1},
                 {"scene", 4},
                 {"revision", 999},
                 {"activity", 2},
                 {"id", "t4"},
                 {"accepted", false},
                 {"reason", "candidates-changed-on-refresh"}};
    selected["activation"] = receipt;
    bench.Set("landCommands", selected);
    bench.Frame();
    Expect(!mount.visible, "a late receipt for another request cannot release this transition");
    receipt["revision"] = 13;
    selected["activation"] = receipt;
    bench.Set("landCommands", selected);
    bench.Frame();
    Expect(mount.visible && mount.errors.back().find("candidates-changed-on-refresh") != std::string::npos,
           "exact refusal releases the HUD with the native cause");
    bench.Answer(false); // the same refusal, answering the tapped row's request
    bench.Frame(hud::Hud::kTransitionWait + 0.1);
    const size_t refusedErrors = mount.errors.size();
    receipt["serial"] = 2;
    receipt["accepted"] = true;
    receipt["reason"] = "native-dispatched";
    selected["activation"] = receipt;
    bench.Set("landCommands", selected);
    bench.Frame();
    Expect(mount.visible && mount.errors.size() == refusedErrors,
           "a late accepted receipt cannot revive a refused transition");

    bench.Press("t4");
    Expect(!mount.visible, "a new tap waits for its own later receipt");
    receipt["serial"] = 3;
    selected["activation"] = receipt;
    bench.Set("landCommands", selected);
    bench.Frame();
    Expect(!mount.visible, "native accepted: still wait for the game's fader");
    selected["activations"] = Json::array({receipt});
    receipt["serial"] = 4;
    receipt["accepted"] = false;
    receipt["reason"] = "stale-at-enqueue";
    selected["activations"].push_back(receipt); // same-tuple duplicate must not downgrade acceptance
    receipt["revision"] = 999;
    selected["activation"] = receipt; // unrelated last receipt must not erase the matching answer
    bench.Set("landCommands", selected);
    bench.Frame();
    Expect(!mount.visible, "accepted history beats duplicate and unrelated last refusals without a flash");
    bench.Frame(hud::Hud::kTransitionWait + 0.1);
    Expect(mount.visible && mount.errors.back().find("script started no transition") != std::string::npos,
           "native acceptance alone never proves that the script changed the location");
}

// The potion and Rush behind one button bottom right, where the potion button was (hud.h AbilityControls):
// the potion while the gameplay offers it, Rush only in a fight while the scripts offer it or it runs; a tap
// lays them out as HUD buttons left of it; Rush launches through the land menu's own command.
static void CheckRush()
{
    LandBench bench;
    FakeMount &mount = bench.mount;
    bench.Explore();
    Json vitals = State("vitals");
    vitals["abilities"] = Json::array({Json{{"id", "Rush"},
                                            {"label", "Тинктура Ометочтли"},
                                            {"state", "ready"},
                                            {"fill", 0},
                                            {"left", 0},
                                            {"command", "BI_ActivateRush"}}});
    bench.Set("vitals", vitals);
    const Json &shown = mount.shown.elements;
    Expect(mount.Has("abilities") && shown["abilities"]["cue"] == false,
           "explore: the button for the potion alone, no dot (Rush is a fight's): " + shown["abilities"].dump());
    bench.Press("abilities");
    Expect(mount.Has("abilities-1") && shown["abilities-1"]["icon"] == "potion" && !mount.Has("abilities-2"),
           "explore: the potion alone, no Rush");
    bench.Press("abilities");
    bench.Feed("gameplay", "gameplay-melee");
    bench.Feed("combat", "combat-melee");
    Json commands = State("land-commands-idle");
    commands["offered"].push_back("BI_ActivateRush");
    bench.Set("landCommands", commands);
    Expect(mount.Has("abilities") && shown["abilities"]["icon"] == "rank" && shown["abilities"]["cue"] == true &&
               !shown["abilities"].contains("badge") && !mount.Has("abilities-1") && mount.shown.sheet.empty(),
           "fight: the collapsed button with the ready dot, not a Rush button: " + shown["abilities"].dump());
    Expect(mount.Has("hero") && mount.Has("actions") && !mount.Has("potion"),
           "… bottom right in the potion's place: the top row keeps the hero and actions buttons");
    mount.commands.clear();
    bench.Press("abilities");
    Expect(mount.Has("abilities-1") && mount.Has("abilities-2") && !mount.Has("abilities-3") &&
               shown["abilities-1"]["icon"] == "potion" && shown["abilities-2"]["icon"] == "flame" &&
               shown["abilities-2"]["label"] == "Тинктура Ометочтли" && !shown["abilities-2"].contains("disabled") &&
               shown["abilities"]["variant"] == "active" && mount.shown.sheet.empty() && mount.commands.empty(),
           "a tap lays out the potion, then Rush under the game's name, nothing launched yet: " +
               shown["abilities-2"].dump());
    bench.Press("abilities-2");
    ExpectEqual(mount.Last(), "landcommands:10\topen\tBI_ActivateRush",
                "a tap on Rush opens the land menu straight into its Rush command");
    Expect(!mount.Has("abilities-1"), "… and the row folds");
    bench.Press("abilities");
    Expect(bench.hud.Back(), "Back is the open row's");
    bench.Frame();
    Expect(!mount.Has("abilities-1"), "Back folds the row");
    vitals["abilities"][0]["state"] = "active";
    vitals["abilities"][0]["fill"] = 0.75;
    vitals["abilities"][0]["left"] = 9;
    bench.Set("vitals", vitals);
    Expect(shown["abilities"]["fill"] == 75 && shown["abilities"]["fillTone"] == "reload" &&
               shown["abilities"]["cue"] == false,
           "running: the button's fill, no dot: " + shown["abilities"].dump());
    bench.Press("abilities");
    Expect(shown["abilities-2"]["badge"] == "9" && shown["abilities-2"]["fill"] == 75 &&
               !shown["abilities-2"].contains("disabled"),
           "… its fill and seconds on its button: " + shown["abilities-2"].dump());
    vitals["abilities"][0]["state"] = "recovery";
    vitals["abilities"][0]["fill"] = 0.25;
    vitals["abilities"][0]["left"] = 54;
    bench.Set("vitals", vitals);
    Expect(shown["abilities-2"]["badge"] == "54" && shown["abilities-2"]["fill"] == 25 &&
               shown["abilities-2"]["fillTone"] == "cooldown" && shown["abilities-2"]["disabled"] == true,
           "recovering: the open row follows the scripts' seconds, greyed: " + shown["abilities-2"].dump());
    vitals["abilities"][0]["state"] = "blocked";
    bench.Set("vitals", vitals);
    const size_t sent = mount.commands.size(), errors_before = mount.errors.size();
    bench.Press("abilities-2", "refused");
    Expect(shown["abilities-2"]["disabled"] == true && !shown["abilities-2"].contains("reason") &&
               mount.commands.size() == sent && mount.errors.size() == errors_before,
           "blocked: greyed, no words, a tap launches nothing: " + shown["abilities-2"].dump());
    bench.Set("landCommands", State("land-commands-root"));
    Expect(mount.shown.sheet == "s-hud-sheet" && !mount.Has("abilities-1"), "the land menu takes the row's place");
    commands["revision"] = 12;
    bench.Set("landCommands", commands);
    Expect(mount.shown.sheet.empty() && mount.Has("abilities") && !mount.Has("abilities-1"),
           "… and the row does not come back with its closing");
    vitals.erase("abilities");
    bench.Set("vitals", vitals);
    bench.Press("abilities");
    Expect(mount.Has("abilities") && mount.Has("abilities-1") && !mount.Has("abilities-2"),
           "no Rush offered: the button stays for the potion alone");
    const size_t unlaunched = mount.errors.size();
    vitals["abilities"] = Json::array({Json{{"id", "Rush"}, {"state", "ready"}}});
    bench.Set("vitals", vitals);
    const size_t launched = mount.commands.size();
    bench.Press("abilities-2");
    Expect(mount.commands.size() == launched && mount.errors.size() > unlaunched,
           "an ability without its land menu command launches nothing and says so");
    vitals["abilities"] = Json::array({Json{{"id", ""}, {"state", "ready"}}});
    const size_t errors = mount.errors.size();
    bench.Set("vitals", vitals);
    Expect(mount.errors.size() > errors, "an unnamed ability in the land vitals is rejected aloud");
}

// What a state change costs the scene (the host Release build is the measure): the vitals of a fight change several
// times a second and only move readings; a changed effect asks the script for a build.
static void PrintCost()
{
    LandBench bench;
    bench.Melee();
    const auto cost = [&](auto change) {
        Json state = State("vitals-status");
        constexpr int kRounds = 200;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRounds; ++i)
        {
            change(state, i);
            bench.Set("vitals", state);
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRounds;
    };
    const double readings = cost([](Json &state, int i) { state["health"] = 0.2 + (i % 50) * 0.01; });
    const double builds = cost([](Json &state, int i) {
        state["effects"] = i % 2 ? Json::array({"poison"}) : Json::array({"poison", "healing"});
    });
    std::printf("land scene per state change: %.0f us with readings only, %.0f us with a script build\n", readings,
                builds);
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    app_pack_path = argc > 2 ? argv[2] : "build/app-pack";
    const std::string pack = app_pack_path;
    const ScriptPack scripts(app_pack_path);
    app_pack = &scripts;
    CheckExplore();
    CheckMovement();
    CheckCombat();
    CheckStatus();
    CheckRush();
    CheckSheet();
    CheckPlaceIcons();
    CheckTransitionFlash();
    CheckTouches(pack);
    CheckOneHostPerProcess(pack);
    PrintCost();
    if (failures)
        std::fprintf(stderr, "%d land HUD check(s) failed\n", failures);
    else
        std::printf("land HUD checks passed\n");
    return failures ? 1 : 0;
}
