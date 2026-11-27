// The touch interface's scripts (app/mobile-ui) on the production compiler and VM: the segments of the pack's list
// attached to a program, isolated when they fail, kept out of its saves; the probe of what the game offers; the
// screens' scripts through the VM side of script screens, with the cost of a build and of a tap.
#include "compiler.h"
#include "core_impl.h"
#include "istring.hpp"
#include "game_profile.hpp"
#include "logging.hpp"
#include "platform/storage.hpp"
#include "save_state.hpp"
#include "script_libriary.h"
#include "script_port.hpp"
#include "screens/script_props.h"
#include "storm_ui/interface_model.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <catch2/catch.hpp>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using storm::ui::Json;

namespace
{
std::string Environment(const char *name)
{
    const char *value = std::getenv(name);
    REQUIRE(value != nullptr);
    return value;
}

std::string ReadText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A game directory: tests/ui_scripts/game as its program, with the application's interface scripts and the generated
// capability header added, as the APK's layer adds them. The working directory is the game's, as in the engine.
void MakeGame()
{
    static bool made = false;
    if (made)
        return;
    made = true;
    for (auto *c : __STORM_CLASSES_REGISTRY) // as CoreImpl::Init: libraries are found by the hash of their name
        c->SetHash(MakeHashValue(c->GetName()));
    const fs::path root = fs::temp_directory_path() / "game";
    fs::remove_all(root);
    const fs::path program = root / "PROGRAM";
    fs::create_directories(program);
    fs::copy(Environment("STORM_UI_APP") + "/PROGRAM", program, fs::copy_options::recursive);
    fs::copy(Environment("STORM_UI_TESTS") + "/game", program,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    fs::copy_file(Environment("STORM_UI_HEADER"), program / "interface/touch/capabilities.h");
    fs::current_path(root);
    storm::storage::select({.game = root}); // read only: saves are made in memory
    fio->ScanResourcePaths();
}

std::string PackList()
{
    return ReadText(Environment("STORM_UI_APP") + "/design/scripts.json");
}

// The lines the engine and the compiler log while it lives.
struct Log
{
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    Log()
    {
        spdlog::default_logger()->sinks().push_back(sink);
        storm::logging::getOrCreateLogger("error")->sinks().push_back(sink);
    }
    ~Log()
    {
        for (const auto &logger : {spdlog::default_logger(), storm::logging::getOrCreateLogger("error")})
            std::erase(logger->sinks(), sink);
    }
    bool Has(std::string_view text) const
    {
        for (const std::string &line : sink->last_formatted())
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    }
};

// A program with the pack's segments attached by the production attacher.
struct Vm
{
    const bool made = (MakeGame(), true); // the game's folders before the compiler exists
    std::vector<storm::ui::ScriptScreen> screens;
    int released = 0;
    storm::ui_scripts::Attacher attacher;
    COMPILER compiler;

    explicit Vm(std::string list = PackList())
        : attacher({[list] { return list; },
                    [](const std::string &path) { return fio->_FileOrDirectoryExists(path.c_str()); },
                    [this](const std::vector<storm::ui::ScriptScreen> &attached) { screens = attached; },
                    [this] {
                        screens.clear();
                        ++released;
                    }})
    {
        core_internal.Compiler = &compiler;
        storm::script::SetAttacher(&attacher);
        compiler.SetProgramDirectory("PROGRAM");
    }
    ~Vm()
    {
        compiler.Release();
        storm::script::SetAttacher(nullptr);
        core_internal.Compiler = nullptr;
    }
    void Start()
    {
        REQUIRE(compiler.CreateProgram("seadogs.c"));
        compiler.Run();
    }
    bool Loaded(const char *segment)
    {
        return compiler.BC_SegmentIsLoaded(segment);
    }
    int Integer(const char *name)
    {
        auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
        REQUIRE(value != nullptr);
        return value->GetInt();
    }
};

std::string List(const std::string &segments)
{
    return R"({"segments": [{"file": "interface/touch/core.c", "core": true}, )" + segments + "]}";
}

// The rest screen's script, not in the pack's list yet.
std::string RestList()
{
    return List(R"({"file": "interface/touch/legacy/rest.c", "requires": ["interface:tavernwait"], "screens": [
        {"capability": "rest", "interface": "tavernwait", "component": "s-rest", "nodes": [["HOURS_SLIDE", "slider",
         true], ["OK_BTN", "button", true], ["CANCEL_BTN", "button"], ["EXIT_BTN", "button"], ["INFO_TEXT", "text"]]}]})");
}

// What a screen's script does through the natives, recorded; its view as the props of the pack screen.
struct Calls final : storm::ui::ScriptCall
{
    const storm::ui::ComponentSpec &spec;
    std::vector<storm::ui::Widget> widgets;
    Json props;
    std::vector<std::string> calls, bound, modals;

    explicit Calls(const storm::ui::ComponentSpec &component) : spec(component)
    {
    }
    void Show(const storm::ui::ViewTree &view) override
    {
        props = storm::ui::screens::ScriptProps(view, spec);
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bound.push_back(id + " " + verb);
    }
    void Send(const std::string &node, const std::string &verb, const std::string &value) override
    {
        calls.push_back("send " + node + " " + verb + " " + value);
    }
    void Back(const std::string &node) override
    {
        calls.push_back("back " + node);
    }
    void Modal(const std::string &id, const std::string &cancel) override
    {
        modals.push_back(id + " " + cancel);
    }
    bool Pending(const std::string &) const override
    {
        return false;
    }
    bool Offers(const std::string &node, const std::string &verb) const override
    {
        for (const auto &widget : widgets)
            if (storm::ui::SameNode(widget.node, node))
                return widget.Offers(verb);
        FAIL("Offers of a node the screen does not have: " << node);
        return false;
    }
    std::string Text(const std::string &key) const override
    {
        return "<" + key + ">";
    }
    std::vector<const storm::ui::Widget *> Single() const
    {
        std::vector<const storm::ui::Widget *> out;
        for (const auto &widget : widgets)
            out.push_back(&widget);
        return out;
    }
    bool Binds(const std::string &id, const std::string &verb) const
    {
        return std::ranges::find(bound, id + " " + verb) != bound.end() ||
               std::ranges::find(bound, id + " ") != bound.end();
    }
};

std::unique_ptr<storm::ui::ComponentSpec> PackScreen(const char *folder)
{
    return storm::ui::ParseComponentSpec(
        Json::parse(ReadText(Environment("STORM_UI_APP") + "/design/screens/" + folder + "/spec.json")));
}

const storm::ui::ScriptScreen &Screen(const Vm &vm, const std::string &capability)
{
    for (const auto &screen : vm.screens)
        if (screen.capability == capability)
            return screen;
    FAIL("no script screen " << capability);
    return vm.screens.front();
}

storm::ui_scripts::VmPort *insidePort = nullptr;
const storm::ui::ScriptScreen *insideScreen = nullptr;
storm::ui::ScriptCall *insideCall = nullptr;

// TestBuildInside(): a screen's build asked for from inside a running script; 1 when it is refused.
uint32_t TestBuildInside(VS_STACK *stack)
{
    int refused = 0;
    try
    {
        insidePort->Build(*insideScreen, {}, *insideCall);
    }
    catch (const std::logic_error &)
    {
        refused = 1;
    }
    static_cast<VDATA *>(stack->Push())->Set(static_cast<int32_t>(refused));
    return IFUNCRESULT_OK;
}
} // namespace

class UiScriptsTest : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        IFUNCINFO info;
        info.pFuncName = "TestBuildInside";
        info.pReturnValueName = "int";
        info.nArguments = 0;
        info.pFuncAddress = TestBuildInside;
        core.SetScriptFunction(&info);
        return true;
    }
};
CREATE_SCRIPTLIBRIARY(UiScriptsTest)

TEST_CASE("the pack's segments are attached after Main, each on its own")
{
    Log log;
    Vm vm;
    vm.Start();
    CHECK(vm.Loaded("interface/touch/core.c"));
    // The game's adapters need the game's scripts, which this program does not have: left out, loudly.
    CHECK_FALSE(vm.Loaded("interface/android_ui_bootstrap.c"));
    CHECK(log.Has("UI script interface/android_ui_bootstrap.c is off: does not compile"));
    CHECK(vm.screens.empty());
}

TEST_CASE("a segment that fails is left out with its screens; the core stops the start")
{
    SECTION("does not compile")
    {
        Log log;
        Vm vm(List(R"({"file": "interface/test/broken.c", "screens": [{"capability": "broken", "interface": "x",
                       "component": "s-x", "nodes": []}]}, {"file": "interface/test/plain.c"})"));
        vm.Start();
        CHECK_FALSE(vm.Loaded("interface/test/broken.c"));
        CHECK(vm.Loaded("interface/test/plain.c"));
        CHECK(vm.screens.empty());
        CHECK(log.Has("UI script interface/test/broken.c is off: does not compile (see the compile log); its screens "
                      "(broken)"));
        CHECK(log.Has("missing: broken"));
    }
    SECTION("needs what the game does not offer: not even compiled")
    {
        Log log;
        Vm vm(List(R"({"file": "interface/test/plain.c", "requires": ["fn:GameFunction/3", "fn:Missing/0",
                       "var:Missing", "ev:Missing", "interface:missing", "file:PROGRAM/missing.c"]})"));
        vm.Start();
        CHECK_FALSE(vm.Loaded("interface/test/plain.c"));
        CHECK(log.Has("fn:GameFunction/3: GameFunction takes 2 arguments, not 3; fn:Missing/0: Missing is not a "
                      "function of the game; var:Missing: Missing is not a global of the game; ev:Missing: Missing "
                      "has no handler in the game; interface:missing: missing is not an interface of the game; "
                      "file:PROGRAM/missing.c: PROGRAM/missing.c is not in the game"));
    }
    SECTION("needs what the game offers")
    {
        Vm vm(List(R"({"file": "interface/test/plain.c", "requires": ["fn:gamefunction/2", "var:GameValue",
                       "ev:GameTick", "interface:TavernWait", "file:PROGRAM/seadogs.c"]})"));
        vm.Start();
        CHECK(vm.Loaded("interface/test/plain.c"));
    }
    SECTION("the core")
    {
        Vm vm(R"({"segments": [{"file": "interface/test/broken.c", "core": true}]})");
        REQUIRE(vm.compiler.CreateProgram("seadogs.c"));
        CHECK_THROWS_WITH(vm.compiler.Run(), Catch::Contains("interface/test/broken.c does not compile"));
    }
}

TEST_CASE("the segment list is checked when it is read")
{
    using storm::ui_scripts::ParseSegments;
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "requires": ["fn:Name"]}]})"),
                      Catch::Contains("is not fn:<name>/<arguments>"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "requires": ["fn:Name/x"]}]})"),
                      Catch::Contains("is not fn:<name>/<arguments>"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "requires": ["global:Name"]}]})"),
                      Catch::Contains("has an unknown kind"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "core": true, "requires": ["var:A"]}]})"),
                      Catch::Contains("a core segment cannot have requirements"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "screens": [{"capability": "c"}]}]})"),
                      Catch::Contains("scripts.json segment a.c"));
    CHECK(ParseSegments(PackList()).size() == 2);
}

TEST_CASE("the probe reads what the program offers")
{
    Vm vm(RestList());
    vm.Start();
    const auto started = std::chrono::steady_clock::now();
    const auto surface = storm::ui_scripts::Probe(vm.compiler, [](const std::string &) { return false; });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    CHECK(surface.functions.at("gamefunction") == 2);
    CHECK(surface.functions.contains("uishow")); // a native the interface's core declares
    CHECK(surface.globals.contains("gamevalue"));
    CHECK(surface.globals.contains("uimodel"));
    CHECK(surface.events.contains("gametick"));
    CHECK(surface.interfaces == std::unordered_set<std::string>{"tavernwait", "store"});
    WARN("probe of the test program: " << ms << " ms");
}

TEST_CASE("the globals of attached segments never enter a save")
{
    Log log;
    std::vector<char> raw;
    {
        Vm vm(RestList());
        vm.Start();
        REQUIRE(vm.compiler.BC_LoadSegment("interface/test/refer.c")); // a game segment, loaded after the attach
        std::string value;
        REQUIRE(vm.compiler.RunScriptFunction("ReferUi", value));
        raw = vm.compiler.SerializeState();
    }
    // A game global that refers to an attached one is saved empty: the attached one is not in the save.
    CHECK(log.Has("Ghost reference: 'ReferToUi'"));
    storm::save::ParsedState state;
    std::string error;
    REQUIRE(storm::save::parseState(raw.data(), raw.size(), state, error));
    std::vector<std::string> names;
    for (const auto &variable : state.variables)
        names.push_back(variable.name);
    CHECK(std::ranges::find(names, "GameValue") != names.end());
    CHECK(std::ranges::find(names, "UiModel") == names.end());
    CHECK(std::ranges::find(names, "RestView") == names.end());

    Vm loaded(RestList());
    REQUIRE(loaded.compiler.LoadState(raw, state));
    CHECK(loaded.Integer("GameValue") == 7);
    CHECK(loaded.Loaded("interface/touch/legacy/rest.c")); // attached again after OnLoad
    CHECK(loaded.screens.size() == 1);
    CHECK_FALSE(log.Has("'UiModel' is not declared"));
    CHECK_FALSE(log.Has("'RestView' is not declared"));
}

TEST_CASE("a program replaced releases the attached screens and attaches them again")
{
    Vm vm(RestList());
    vm.Start();
    const auto raw = vm.compiler.SerializeState();
    storm::save::ParsedState state;
    std::string error;
    REQUIRE(storm::save::parseState(raw.data(), raw.size(), state, error));
    REQUIRE(vm.compiler.LoadState(raw, state));
    CHECK(vm.released >= 1);
    CHECK(vm.screens.size() == 1);
    CHECK(vm.attacher.Running() == &vm.compiler);
}

TEST_CASE("the rest screen's script gives the pack screen the props and actions of the recorded models")
{
    Vm vm(RestList());
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    const auto &screen = Screen(vm, "rest");
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/rest.json"));
    for (const Json &recorded : fixture.at("cases"))
    {
        INFO(recorded.at("name").get<std::string>());
        storm::ui::InterfaceModel model;
        REQUIRE(storm::ui::ParseInterfaceModel(recorded.at("model").dump(), model));
        Calls calls(*spec);
        calls.widgets = model.widgets;
        port.Build(screen, calls.Single(), calls);
        CHECK(calls.props == recorded.at("props"));
        CHECK(calls.modals == std::vector<std::string>{"rest rest-close"});
        for (const Json &event : recorded.at("events"))
        {
            REQUIRE(calls.Binds(event[0], event[1]));
            port.Event(screen, event[0], event[1], event[2], calls);
        }
        CHECK(calls.calls == recorded.at("calls").get<std::vector<std::string>>());
    }
}

TEST_CASE("script screen events are refused while a script runs; natives outside a screen fail")
{
    Log log;
    Vm vm(RestList());
    vm.Start();
    REQUIRE(vm.compiler.BC_LoadSegment("interface/test/inside.c"));
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    Calls calls(*spec);
    insidePort = &port;
    insideScreen = &Screen(vm, "rest");
    insideCall = &calls;
    std::string value;
    REQUIRE(vm.compiler.RunScriptFunction("BuildInside", value));
    CHECK(value == "1");
    CHECK(calls.calls.empty());

    vm.compiler.RunScriptFunction("ShowOutside", value);
    CHECK(log.Has("UiShow: called outside a UI script screen's build or event"));

    // A native that fails inside a build fails the build.
    const storm::ui::ScriptScreen bad{"bad", "bad", "s-rest", {}};
    CHECK_THROWS_WITH(port.Build(bad, {}, calls),
                      Catch::Contains("script screen bad: UiShow: view nope: is not a prop"));

    vm.compiler.Release();
    CHECK(vm.attacher.Running() == nullptr);
    CHECK_THROWS_WITH(port.Build(*insideScreen, {}, calls), Catch::Contains("no script program runs"));
}

TEST_CASE("cost of a screen build and of a tap in script, in the script work digest")
{
    Vm vm(RestList());
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    const auto &screen = Screen(vm, "rest");
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/rest.json"));
    storm::ui::InterfaceModel model;
    REQUIRE(storm::ui::ParseInterfaceModel(fixture.at("cases")[0].at("model").dump(), model));
    Calls calls(*spec);
    calls.widgets = model.widgets;
    constexpr int kRuns = 2000;
    const auto time = [](auto &&run) {
        const auto started = std::chrono::steady_clock::now();
        for (int n = 0; n < kRuns; n++)
            run();
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() / kRuns;
    };
    const double build = time([&] { port.Build(screen, calls.Single(), calls); });
    const double tap = time([&] { port.Event(screen, "hours", "select", "6", calls); });
    WARN("rest screen in script, per call over " << kRuns << " (ASan -O1 build): build " << build << " us, tap " << tap
                                                 << " us");
    CHECK(calls.props.at("hours").size() == 5);
    // The periodic digest names them with their cost per call, however rare they are; the game's own events stay in
    // its list by their share of the frame.
    Log log;
    core.Event("GameTick");
    vm.compiler.ReportScriptWork();
    CHECK(log.Has("interface scripts (ms per call): "));
    CHECK(log.Has("UiBuild@"));
    CHECK(log.Has("UiTap@"));
    CHECK_FALSE(log.Has("GameTick@other:1x avg"));
}

// A real game's program ($STORM_GAME_DIR, its main segment compiled as script_check does): what the probe costs there.
// Main does not run here, so the interface table is empty (it adds some 60 rows on a running game).
TEST_CASE("cost of the probe on a game", "[game]")
{
    const char *game = std::getenv("STORM_GAME_DIR");
    if (!game)
        std::exit(77); // NOT_RUN: no game
    for (auto *c : __STORM_CLASSES_REGISTRY)
        c->SetHash(MakeHashValue(c->GetName()));
    fs::current_path(game);
    storm::storage::select({.game = fs::current_path()});
    fio->ScanResourcePaths();
    std::string program_name;
    for (const auto &entry : fs::directory_iterator("."))
        if (entry.is_directory() && storm::AsciiLower(entry.path().filename().string()) == "program")
            program_name = entry.path().filename().string();
    REQUIRE(!program_name.empty());
    std::vector<std::string> diagnostics;
    COMPILER compiler;
    core_internal.Compiler = &compiler;
    compiler.ConfigureCompileCheck(&diagnostics, false);
    compiler.SetProgramDirectory(program_name.c_str());
    storm::gameProfile(); // detected from the game before the compile, as script_check does
    REQUIRE(compiler.BC_LoadSegment("seadogs.c"));
    double best = 1e9;
    storm::ui_scripts::GameSurface surface;
    for (int n = 0; n < 20; n++)
    {
        const auto started = std::chrono::steady_clock::now();
        surface = storm::ui_scripts::Probe(compiler, [](const std::string &) { return false; });
        best = std::min(best,
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    WARN("probe of " << game << ": " << surface.functions.size() << " functions, " << surface.globals.size()
                     << " globals, " << surface.events.size() << " events, " << surface.interfaces.size()
                     << " interfaces in " << best << " ms (best of 20, ASan -O1 build)");
    CHECK(surface.functions.size() > 1000);
    compiler.Release();
    core_internal.Compiler = nullptr;
}
