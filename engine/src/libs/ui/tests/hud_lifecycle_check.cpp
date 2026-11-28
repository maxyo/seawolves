// Full runtime regression: removing held land controls must cancel them while their scene
// and its bindings still exist. The application's pack and the touch interface's scripts over the fixture game,
// synthetic states, no graphics context or game files.
#include "hud_scripts.h"

#include <filesystem>
#include <unistd.h>

static void ChangeScene(const std::string &pack, bool combat)
{
    auto files = DirectoryFiles(pack);
    NoBackend backend;
    std::vector<std::string> errors, commands;
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&] { return clock; };
    config.on_log = [&](LogLevel level, const std::string &line) {
        if (level == LogLevel::Error)
            errors.push_back(line);
    };
    config.on_action = [&](const Action &action) { commands.push_back(to_string(action)); };
    std::string error;
    auto host = Host::Create(config, &error);
    Expect(host != nullptr, "runtime starts: " + error);
    if (!host)
        return;
    host->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
    const auto feed = [&](const char *channel, const char *file) { Expect(host->SetState(channel, Text(file)), file); };
    const auto frame = [&] {
        clock += 1.0 / 60.0;
        host->Frame();
    };
    feed("presentation", "presentation-game");
    feed("gameplay", combat ? "gameplay-melee" : "gameplay-explore");
    feed("combat", combat ? "combat-melee" : "combat-explore");
    feed("vitals", "vitals");
    feed("worldInfo", "world-info");
    feed("contextAction", "context-door");
    feed("landCommands", "land-commands-idle");
    frame();
    frame();
    errors.clear();
    if (combat)
    {
        host->Touch(TouchPhase::Down, 1, 44, 216);  // block
        host->Touch(TouchPhase::Down, 2, 672, 282); // strike gesture
    }
    else
    {
        host->Touch(TouchPhase::Down, 1, 90, 320);
        host->Touch(TouchPhase::Move, 1, 130, 320); // movement held
    }
    const auto held = commands.size();
    feed("gameplay", "gameplay-orders"); // unknown mode, only game-menu button remains
    frame();
    frame();
    Expect(errors.empty(), std::string(combat ? "combat" : "movement") +
                               " cancelled through its original scene without orphan events" +
                               (errors.empty() ? "" : ": " + errors.back()));
    Expect(!host->WantsTouch(90, 320) && host->WantsTouch(766, 34), "only the menu button remains");
    if (!combat)
        Expect(commands.size() > held && std::any_of(commands.begin() + held, commands.end(),
                                                     [](const std::string &command) {
                                                         return command.rfind("move:", 0) == 0 &&
                                                                command.ends_with("\t0\t0\t0");
                                                     }),
               "leaving the movement scene sends the native stop");
    const auto ended = commands.size();
    host->Touch(TouchPhase::Up, 1, 130, 320);
    if (combat)
        host->Touch(TouchPhase::Up, 2, 672, 282);
    frame();
    Expect(commands.size() == ended && errors.empty(), "late finger releases neither act nor report stale controls");
}

// «Под левую руку»: the HUD mounts the pack's mirrored placement; touches follow the elements, and back.
static void LeftHanded(const std::string &pack)
{
    auto files = DirectoryFiles(pack);
    NoBackend backend;
    std::vector<std::string> errors;
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&] { return clock; };
    config.on_log = [&](LogLevel level, const std::string &line) {
        if (level == LogLevel::Error)
            errors.push_back(line);
    };
    config.on_action = [](const Action &) {};
    std::string error;
    auto host = Host::Create(config, &error);
    Expect(host != nullptr, "runtime starts: " + error);
    if (!host)
        return;
    host->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
    const auto feed = [&](const char *channel, const char *file) { Expect(host->SetState(channel, Text(file)), file); };
    const auto frame = [&] {
        for (int i = 0; i < 4; ++i)
        {
            clock += 1.0 / 60.0;
            host->Frame();
        }
    };
    feed("presentation", "presentation-game");
    feed("gameplay", "gameplay-explore");
    feed("combat", "combat-explore");
    feed("vitals", "vitals");
    feed("worldInfo", "world-info");
    feed("contextAction", "context-door");
    feed("landCommands", "land-commands-idle");
    frame();
    Expect(host->WantsTouch(90, 320) && host->WantsTouch(766, 34),
           "right-handed: the move zone left, the menu button top right");
    host->SetDisplayOptions(1.0f, 1.0f, true);
    frame();
    Expect(host->WantsTouch(800 - 90, 320) && host->WantsTouch(34, 34) && !host->WantsTouch(90, 320),
           "left-handed: the move zone right, the menu button top left");
    host->SetDisplayOptions(1.0f, 1.0f, false);
    frame();
    Expect(host->WantsTouch(90, 320) && host->WantsTouch(766, 34), "back to right-handed");
    Expect(errors.empty(), "no errors" + (errors.empty() ? std::string() : ": " + errors.back()));
}

// A tap the game takes in the instant the app leaves: its command goes out, then the presentation stops being live
// before any frame shows the answer (the activity paused; the engine closes the answer with the pause). The action
// ends there, said to be cut off; the frames after the return are not its answer.
static void TapAsTheAppLeaves(const std::string &pack)
{
    auto files = DirectoryFiles(pack);
    NoBackend backend;
    std::vector<std::string> lines;
    double clock = 0.0;
    Config config;
    config.files = files.get();
    config.backend = &backend;
    config.width = 800;
    config.height = 400;
    config.clock = [&] { return clock; };
    config.trace_actions = true;
    config.log_taps = true; // the "UI action" lines are logged with it
    config.on_log = [&](LogLevel, const std::string &line) {
        if (line.starts_with("UI action") || line.starts_with("UI flicker"))
            lines.push_back(line);
    };
    config.on_action = [](const Action &) {};
    std::string error;
    auto host = Host::Create(config, &error);
    Expect(host != nullptr, "runtime starts: " + error);
    if (!host)
        return;
    host->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
    const auto feed = [&](const char *channel, const char *file) { Expect(host->SetState(channel, Text(file)), file); };
    const auto frames = [&](int count) {
        for (int i = 0; i < count; ++i)
        {
            clock += 1.0 / 60.0;
            host->PlaceTouches(clock);
            host->Frame();
        }
    };
    const auto tap = [&] {
        clock += 1.0 / 60.0;
        host->Touch(TouchPhase::Down, 1, 766, 34, clock);
        host->Touch(TouchPhase::Up, 1, 766, 34, clock);
        host->PlaceTouches(clock);
        host->Frame(); // the tap's own frame
    };
    // Before the game was ever live, a state that arrives does not end an action: nothing left.
    tap();
    feed("gameplay", "gameplay-explore");
    feed("combat", "combat-explore");
    std::vector<ActionTrace> traces = host->TakeActionTraces(true);
    Expect(traces.size() == 1 && !traces[0].cut_off, "only leaving a live game cuts an action off");
    feed("presentation", "presentation-game");
    feed("vitals", "vitals");
    feed("worldInfo", "world-info");
    feed("contextAction", "context-door");
    feed("landCommands", "land-commands-idle");
    frames(60);
    Expect(host->WantsTouch(766, 34), "the menu button takes the tap");
    host->TakeActionTraces(true);
    lines.clear();

    tap();
    feed("presentation", "presentation-background");
    traces = host->TakeActionTraces();
    Expect(traces.size() == 1 && traces[0].cut_off && lines.size() == 1 &&
               lines[0].find("cut off") != std::string::npos,
           "a tap whose answer is not drawn before the app leaves ends when it leaves, said to be cut off: " +
               (lines.empty() ? std::string("no line") : lines.back()));
    frames(30);
    clock += 8.0; // away
    feed("presentation", "presentation-game");
    frames(60);
    traces = host->TakeActionTraces(true);
    Expect(traces.empty() && lines.size() == 1, "the frames after the return are no action's answer");

    lines.clear();
    tap();
    frames(60);
    feed("presentation", "presentation-background");
    traces = host->TakeActionTraces(true);
    Expect(traces.size() == 1 && !traces[0].cut_off && lines.size() == 1 &&
               lines[0].find("cut off") == std::string::npos,
           "a tap that settled before the app left is traced as usual");
    feed("presentation", "presentation-game");
}

int main(int argc, char **argv)
{
    states_directory = argc > 1 ? argv[1] : "tests/states";
    const std::string pack = argc > 2 ? argv[2] : "build/app-pack";
    LeftHanded(pack);
    TapAsTheAppLeaves(pack);
    ChangeScene(pack, false);
    ChangeScene(pack, true);
    if (!failures)
        std::printf("HUD lifecycle checks passed\n");
    return failures ? 1 : 0;
}
