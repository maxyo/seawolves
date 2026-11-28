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
#include "screens/amount.h"
#include "screens/script_props.h"
#include "storm_ui/interface_model.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <catch2/catch.hpp>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <type_traits>

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

// The game: tests/ui_scripts/game with the application's mobile-ui layer over it. The working directory is the game's,
// as in the engine.
void MakeGame()
{
    static bool made = false;
    if (made)
        return;
    made = true;
    for (auto *c : __STORM_CLASSES_REGISTRY) // as CoreImpl::Init: libraries are found by the hash of their name
        c->SetHash(MakeHashValue(c->GetName()));
    // The fixture game, read only (saves are made in memory), with the interface's layer over it as on a phone.
    const fs::path root = Environment("STORM_UI_TESTS") + "/game";
    fs::current_path(root);
    storm::storage::select({.game = root, .layers = {.mobileUi = Environment("STORM_UI_LAYER")}});
    fio->ScanResourcePaths();
}

std::string PackList()
{
    return ReadText(Environment("STORM_MOBILE_UI") + "/design/scripts.json");
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
                    },
                    [](const std::string &) {}})
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

// The globals of the game's interface in a recorded state: "game": {name: whole number or text}, or
// {"Global.attribute.…": whole number or text} for an attribute of a global object, made when the fixture lacks it.
void SetGame(const Json &recorded)
{
    if (!recorded.contains("game"))
        return;
    for (const auto &[name, value] : recorded.at("game").items())
    {
        const size_t dot = name.find('.');
        auto *variable = static_cast<VDATA *>(core.GetScriptVariable(name.substr(0, dot).c_str()));
        REQUIRE(variable != nullptr);
        if (dot == std::string::npos)
        {
            if (value.is_string())
                variable->Set(value.get<std::string>());
            else
                variable->Set(static_cast<int32_t>(value.get<int>()));
            continue;
        }
        ATTRIBUTES *root = variable->GetAClass();
        REQUIRE(root != nullptr);
        ATTRIBUTES *at = root->CreateSubAClass(root, name.c_str() + dot + 1);
        REQUIRE(at != nullptr);
        at->SetValue(value.is_string() ? value.get<std::string>() : std::to_string(value.get<int>()));
    }
}

// The saved values of the program's globals by name (the attribute names they use are codes of the save's name codec,
// which only grows, so a code stays the same).
std::map<std::string, std::vector<char>> GameState(Vm &vm)
{
    const std::vector<char> raw = vm.compiler.SerializeState();
    storm::save::ParsedState state;
    std::string error;
    REQUIRE(storm::save::parseState(raw.data(), raw.size(), state, error));
    std::map<std::string, std::vector<char>> values;
    for (size_t n = 0; n < state.variables.size(); n++)
    {
        const size_t end = n + 1 < state.variables.size() ? state.variables[n + 1].values : raw.size();
        values[state.variables[n].name].assign(raw.begin() + state.variables[n].values, raw.begin() + end);
    }
    return values;
}

std::string List(const std::string &segments)
{
    return R"({"segments": [{"file": "interface/touch/core/core.c", "core": true}, )" + segments + "]}";
}

// What a screen's script does through the natives, recorded; its view as the props of the pack screen.
struct Calls final : storm::ui::ScriptCall
{
    const storm::ui::ComponentSpec &spec;
    std::vector<storm::ui::Widget> widgets;
    Json props;
    std::vector<std::string> calls, bound, modals;
    std::optional<Json> predicted;
    bool first = true;
    std::map<std::string, storm::ui::ViewTree> notes; // UiPass / UiTake, by screen
    std::map<std::string, Json> states;               // UiState, by channel
    std::string answer;                               // of the hold the build is for
    std::vector<std::string> pending;                 // the presses waiting for the game (UiPending)

    explicit Calls(const storm::ui::ComponentSpec &component) : spec(component)
    {
    }
    void Show(const storm::ui::ViewTree &view) override
    {
        props = storm::ui::screens::ScriptProps(view, spec);
    }
    void Predict(const storm::ui::ViewTree &view) override
    {
        predicted = storm::ui::screens::ScriptProps(view, spec);
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bound.push_back(id + " " + verb);
    }
    void Send(const std::string &node, int row, const std::string &verb, const std::string &value,
              const std::string &guard, const std::string &lead) override
    {
        for (const std::string *sent : {&lead, &value})
            if (sent == &value || !lead.empty())
                calls.push_back("send " + node + (row < 0 ? "" : "#" + std::to_string(row)) + " " + verb + " " + *sent +
                                (guard.empty() ? "" : " @" + guard));
    }
    void Hold() override
    {
        calls.push_back("hold");
    }
    std::string Answer() const override
    {
        return answer;
    }
    void Await() override
    {
        calls.push_back("await");
    }
    void Rebuild() override
    {
        calls.push_back("rebuild");
    }
    bool First() const override
    {
        return first;
    }
    void Command(const storm::ui::ScriptCommand &command) override
    {
        calls.push_back(std::visit(
            [](const auto &asked) {
                using Kind = std::decay_t<decltype(asked)>;
                if constexpr (std::is_same_v<Kind, storm::ui::HostCommand>)
                    return "host " + asked.action;
                else if constexpr (std::is_same_v<Kind, storm::ui::QuickCommand>)
                    return "quick " + asked.which;
                else if constexpr (std::is_same_v<Kind, storm::ui::TitleCommand>)
                    return "title " + asked.command;
                else if constexpr (std::is_same_v<Kind, storm::ui::SettingsCommand>)
                    return "settings " + asked.verb;
                else if constexpr (std::is_same_v<Kind, storm::ui::SettingCommand>)
                    return "setting " + asked.key + "=" + std::to_string(asked.value);
                else if constexpr (std::is_same_v<Kind, storm::ui::HudControlCommand>)
                    return "control " + asked.control;
                else if constexpr (std::is_same_v<Kind, storm::ui::HudMapCommand>)
                    return "map " + asked.id;
                else if constexpr (std::is_same_v<Kind, storm::ui::HudMenuCommand>)
                    return "menu " + asked.menu + " " + asked.verb + " " + asked.id;
                else if constexpr (std::is_same_v<Kind, storm::ui::HudCombatCommand>)
                    return "combat " + asked.action + " " + asked.phase;
                else if constexpr (std::is_same_v<Kind, storm::ui::HudContextCommand>)
                    return "context " + asked.name;
                else
                    return "shortcut " + asked.name;
            },
            command));
    }
    void Pass(const std::string &screen, const storm::ui::ViewTree &note) override
    {
        std::string line = "pass " + screen;
        for (const storm::ui::ViewTree &field : note.children)
            line += " " + field.name + "=" + field.value;
        calls.push_back(line);
        notes[screen] = note;
    }
    std::optional<storm::ui::ViewTree> Take(const std::string &screen) override
    {
        const auto found = notes.find(screen);
        if (found == notes.end())
            return std::nullopt;
        storm::ui::ViewTree note = std::move(found->second);
        notes.erase(found);
        return note;
    }
    std::optional<storm::ui::ViewTree> State(const std::string &channel) const override
    {
        const auto found = states.find(channel);
        if (found == states.end())
            return std::nullopt;
        return storm::ui::screens::StateTree(channel, found->second);
    }
    void Toast(const std::string &text) override
    {
        calls.push_back("toast " + text);
    }
    void Read(const std::string &title, const std::string &text) override
    {
        calls.push_back("read " + title + ": " + text);
    }
    void EditText(const std::string &node, const std::string &title) override
    {
        calls.push_back("edit-text " + node + " " + title);
    }
    void Error(const std::string &text) override
    {
        throw std::runtime_error(text);
    }
    // The production reading (screens/amount.h), so a case sees the amount the engine sees.
    int Amount(const std::string &text, const std::string &what) const override
    {
        if (const std::optional<int> value = storm::ui::screens::ParseAmount(text))
            return *value;
        throw std::runtime_error("no number in " + what + " ('" + text + "')");
    }
    bool HasAmount(const std::string &text) const override
    {
        return storm::ui::screens::ParseAmount(text).has_value();
    }
    double Number(const std::string &text, const std::string &what) const override
    {
        const size_t at = text.find_first_of("0123456789");
        if (at == std::string::npos)
            throw std::runtime_error("no number in " + what);
        return std::stod(text.substr(at));
    }
    std::string Decimal(const std::string &text) const override
    {
        return text;
    }
    // As the engine's (screens/controls.cpp OneLine, which this check cannot link): line breaks and tabs are spaces.
    std::string OneLine(const std::string &text) const override
    {
        std::string line = text;
        for (char &c : line)
            if (c == '\r' || c == '\n' || c == '\t')
                c = ' ';
        return line;
    }
    std::string Fill(const std::string &text, const std::string &argument) const override
    {
        return text + " " + argument;
    }
    // The deal shows what the game's window holds; its moves and its button raise the events it names.
    storm::ui::DealView Deal(const storm::ui::DealSpec &spec) override
    {
        calls.push_back("deal " + spec.key + " " + std::to_string(spec.low) + ".." + std::to_string(spec.high) +
                        " answer " + std::to_string(spec.answer));
        bound.push_back(spec.offer + " ");
        bound.push_back(spec.commit + " ");
        return {.value = spec.answer, .change = spec.answer - spec.origin};
    }
    void Back(const std::string &node) override
    {
        calls.push_back("back " + node);
    }
    void Modal(const std::string &id, const std::string &cancel) override
    {
        modals.push_back(id + " " + cancel);
    }
    void BackTo(const std::string &id) override
    {
        modals.push_back("back " + id);
    }
    bool Pending(const std::string &id) const override
    {
        return std::ranges::find(pending, id) != pending.end();
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
        Json::parse(ReadText(Environment("STORM_MOBILE_UI") + "/design/screens/" + folder + "/spec.json")));
}

// The capabilities of the screens the pack's list declares, in its order: on the fixture game every one attaches.
std::vector<std::string> PackCapabilities()
{
    std::vector<std::string> capabilities;
    const Json list = Json::parse(PackList());
    for (const Json &segment : list.at("segments"))
        for (const Json &screen : segment.value("screens", Json::array()))
            capabilities.push_back(screen.at("capability"));
    return capabilities;
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
    CHECK(vm.Loaded("interface/touch/core/core.c"));
    CHECK(vm.Loaded("interface/touch/lineages/legacy/rest.c"));
    CHECK(vm.Loaded("interface/touch/screens/rest.c"));
    const auto &rest = Screen(vm, "rest");
    CHECK(rest.interface == "tavernwait"); // from the adapter's binding
    CHECK(rest.component == "s-rest");
    CHECK(rest.nodes.size() == 5);
    // The game's adapters need the game's scripts, which this program does not have: left out, loudly, the rest kept.
    CHECK_FALSE(vm.Loaded("interface/android_ui_bootstrap.c"));
    CHECK(log.Has("UI script interface/android_ui_bootstrap.c is off: does not compile"));
    std::string offered;
    for (const std::string &capability : PackCapabilities())
        offered += (offered.empty() ? "" : ", ") + capability;
    CHECK(log.Has("capabilities offered: " + offered + "; missing: none"));
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
                       "var:Missing", "ev:Missing", "interface:missing", "file:PROGRAM/missing.c",
                       "node:tavernwait#NO_NODE", "node:missing#OK_BTN"]})"));
        vm.Start();
        CHECK_FALSE(vm.Loaded("interface/test/plain.c"));
        CHECK(log.Has("fn:GameFunction/3: GameFunction takes 2 arguments, not 3; fn:Missing/0: Missing is not a "
                      "function of the game; var:Missing: Missing is not a global of the game; ev:Missing: Missing "
                      "has no handler in the game; interface:missing: missing is not an interface of the game; "
                      "file:PROGRAM/missing.c: PROGRAM/missing.c is not in the game; node:tavernwait#NO_NODE: "
                      "RESOURCE\\INI\\INTERFACES\\TavernWait.ini has no node NO_NODE; node:missing#OK_BTN: missing is "
                      "not an interface of the game"));
    }
    SECTION("needs what the game offers")
    {
        Vm vm(List(R"({"file": "interface/test/plain.c", "requires": ["fn:gamefunction/2", "var:GameValue",
                       "ev:GameTick", "interface:TavernWait", "file:PROGRAM/seadogs.c", "node:TavernWait#HOURS_SLIDE"]})"));
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

TEST_CASE("of the providers of a contract the first that attaches is chosen; its users need one")
{
    Log log;
    Vm vm(List(R"({"file": "interface/test/provider_one.c", "requires": ["fn:Missing/0"], "provides": ["adapter:test"]},
                  {"file": "interface/test/provider_two.c", "provides": ["adapter:test"],
                   "binds": [{"capability": "test", "interface": "tavernwait", "nodes": [["OK_BTN", "button", true]]}]},
                  {"file": "interface/test/inside.c", "provides": ["adapter:test"]},
                  {"file": "interface/test/user.c", "uses": ["adapter:test"],
                   "screens": [{"capability": "test", "component": "s-rest"}]},
                  {"file": "interface/test/plain.c", "uses": ["adapter:other"]},
                  {"file": "interface/test/refer.c", "screens": [{"capability": "unbound", "component": "s-rest"}]})"));
    vm.Start();
    CHECK(vm.Loaded("interface/test/provider_two.c"));
    CHECK_FALSE(vm.Loaded("interface/test/provider_one.c"));
    CHECK_FALSE(vm.Loaded("interface/test/inside.c")); // would compile, but the contract is provided
    CHECK(log.Has("UI scripts: adapter:test by interface/test/provider_two.c; interface/test/inside.c not needed"));
    std::string value;
    REQUIRE(vm.compiler.RunScriptFunction("TestUser", value));
    CHECK(value == "2");
    REQUIRE(vm.screens.size() == 1);
    CHECK(vm.screens[0].capability == "test");
    CHECK(vm.screens[0].interface == "tavernwait");
    CHECK(vm.screens[0].nodes.size() == 1);
    CHECK_FALSE(vm.Loaded("interface/test/plain.c"));
    CHECK(log.Has("UI script interface/test/plain.c is off: adapter:other: no attached script provides it"));
    CHECK(log.Has("UI script interface/test/refer.c: screen unbound has no binding from an attached adapter"));
    CHECK(log.Has("capabilities offered: test; missing: unbound"));
}

TEST_CASE("a binding the game cannot give is off alone; bindings share named node sets")
{
    Log log;
    Vm vm(R"({"nodeSets": {"ok": [["OK_BTN", "button", true]]}, "segments": [
                 {"file": "interface/touch/core/core.c", "core": true},
                 {"file": "interface/test/provider_two.c", "provides": ["adapter:test"], "binds": [
                     {"capability": "test", "interface": "tavernwait", "nodes": ["@ok", ["INFO_TEXT", "text"]],
                      "data": {"column": 3}},
                     {"capability": "gone", "interface": "missing", "nodes": ["@ok"]},
                     {"capability": "bare", "interface": "tavernwait", "nodes": [["NO_NODE", "button", true]]}]},
                 {"file": "interface/test/user.c", "uses": ["adapter:test"], "screens": [
                     {"capability": "test", "component": "s-rest"}, {"capability": "gone", "component": "s-rest"},
                     {"capability": "bare", "component": "s-rest"}]}]})");
    vm.Start();
    CHECK(vm.Loaded("interface/test/provider_two.c"));
    REQUIRE(vm.screens.size() == 1);
    CHECK(vm.screens[0].capability == "test");
    CHECK(vm.screens[0].nodes.size() == 2);
    REQUIRE(vm.screens[0].data.children.size() == 1);
    CHECK(vm.screens[0].data.children[0].value == "3");
    CHECK(log.Has("binding gone is off: missing is not an interface of the game"));
    CHECK(log.Has("binding bare is off: RESOURCE\\INI\\INTERFACES\\TavernWait.ini has no node NO_NODE"));
    CHECK(log.Has("capabilities offered: test; missing: gone, bare"));
}

TEST_CASE("a segment's optional contracts are told to its bindings; it attaches without them")
{
    Log log;
    Vm vm(R"({"segments": [
                 {"file": "interface/touch/core/core.c", "core": true},
                 {"file": "interface/test/provider_one.c", "requires": ["fn:Missing/0"], "provides": ["adapter:gone"]},
                 {"file": "interface/test/inside.c", "provides": ["adapter:here"]},
                 {"file": "interface/test/provider_two.c", "provides": ["adapter:test"],
                  "optional": ["adapter:here", "adapter:gone"],
                  "binds": [{"capability": "test", "interface": "tavernwait", "nodes": [["OK_BTN", "button", true]]}]},
                 {"file": "interface/test/user.c", "uses": ["adapter:test"],
                  "screens": [{"capability": "test", "component": "s-rest"}]}]})");
    vm.Start();
    CHECK(vm.Loaded("interface/test/provider_two.c"));
    REQUIRE(vm.screens.size() == 1);
    REQUIRE(vm.screens[0].data.children.size() == 1);
    const storm::ui::ViewTree &optional = vm.screens[0].data.children[0];
    CHECK(optional.name == "optional");
    REQUIRE(optional.children.size() == 2);
    CHECK((optional.children[0].name == "here" && optional.children[0].value == "1"));
    CHECK((optional.children[1].name == "gone" && optional.children[1].value == "0"));
    CHECK(log.Has("UI script interface/test/provider_two.c: adapter:gone is not provided"));
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
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "requires": ["node:a"]}]})"),
                      Catch::Contains("is not node:<interface>#<node>"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "core": true, "requires": ["var:A"]}]})"),
                      Catch::Contains("a core segment cannot have requirements"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "core": true, "provides": ["adapter:a"]}]})"),
                      Catch::Contains("a core segment cannot have requirements or contracts"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "uses": ["rest"]}]})"),
                      Catch::Contains("uses 'rest' is not adapter:<family>"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "screens": [{"capability": "c",
                      "component": "s-c", "interface": "i"}]}]})"),
                      Catch::Contains("interface and nodes come together"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "binds": [{"capability": "c", "interface": "i",
                      "nodes": ["@none"]}]}]})"),
                      Catch::Contains("no node set @none"));
    CHECK_THROWS_WITH(ParseSegments(R"({"segments": [{"file": "a.c", "screens": [{"capability": "c"}]}]})"),
                      Catch::Contains("scripts.json segment a.c"));
    CHECK(ParseSegments(PackList()).size() == Json::parse(PackList()).at("segments").size());
}

TEST_CASE("the probe reads what the program offers")
{
    Vm vm;
    vm.Start();
    const auto started = std::chrono::steady_clock::now();
    const auto surface = storm::ui_scripts::Probe(vm.compiler, [](const std::string &) { return false; });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    CHECK(surface.functions.at("gamefunction") == 2);
    CHECK(surface.functions.contains("uishow")); // a native the interface's core declares
    CHECK(surface.globals.contains("gamevalue"));
    CHECK(surface.globals.contains("uiscreens"));
    CHECK(surface.events.contains("gametick"));
    CHECK(surface.interfaces.at("tavernwait") == "RESOURCE\\INI\\INTERFACES\\TavernWait.ini");
    CHECK(surface.interfaces.contains("store"));
    WARN("probe of the test program: " << ms << " ms");
}

TEST_CASE("the globals of attached segments never enter a save")
{
    Log log;
    std::vector<char> raw;
    {
        Vm vm;
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
    CHECK(std::ranges::find(names, "UiScreens") == names.end());
    CHECK(std::ranges::find(names, "TouchRest") == names.end());

    Vm loaded;
    REQUIRE(loaded.compiler.LoadState(raw, state));
    CHECK(loaded.Integer("GameValue") == 7);
    CHECK(loaded.Loaded("interface/touch/screens/rest.c")); // attached again after OnLoad
    CHECK(loaded.screens.size() == PackCapabilities().size());
    CHECK_FALSE(log.Has("'UiScreens' is not declared"));
    CHECK_FALSE(log.Has("'TouchRest' is not declared"));
}

TEST_CASE("a program replaced releases the attached screens and attaches them again")
{
    Vm vm;
    vm.Start();
    const auto raw = vm.compiler.SerializeState();
    storm::save::ParsedState state;
    std::string error;
    REQUIRE(storm::save::parseState(raw.data(), raw.size(), state, error));
    REQUIRE(vm.compiler.LoadState(raw, state));
    CHECK(vm.released >= 1);
    CHECK(vm.screens.size() == PackCapabilities().size());
    CHECK(vm.attacher.Running() == &vm.compiler);
}

TEST_CASE("the rest screen's script gives the pack screen the props and actions of the recorded models")
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    const auto &screen = Screen(vm, "rest");
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/cases/rest.json"));
    const auto before = GameState(vm);
    const int hours = vm.Integer("iHourWait");
    for (const Json &recorded : fixture.at("cases"))
    {
        INFO(recorded.at("name").get<std::string>());
        storm::ui::InterfaceModel model;
        REQUIRE(storm::ui::ParseInterfaceModel(recorded.at("model").dump(), model));
        SetGame(recorded);
        Calls calls(*spec);
        calls.widgets = model.widgets;
        port.Build(screen, calls.Single(), calls);
        CHECK(calls.props == recorded.at("props"));
        CHECK(calls.modals == std::vector<std::string>{"rest rest-close"});
        for (const Json &event : recorded.at("events"))
        {
            REQUIRE(calls.Binds(event[0], event[1]));
            calls.predicted.reset();
            const size_t made = calls.calls.size();
            port.Event(screen, event[0], event[1], event[2], calls);
            if (event[1] != "select")
                continue;
            // The span drawn at the tap is what the screen builds once the game has made its hours of the slider's
            // new position (or of the one it kept).
            REQUIRE(calls.predicted);
            Calls answered(*spec);
            answered.widgets = model.widgets;
            if (calls.calls.size() > made)
            {
                const std::string sent = calls.calls[calls.calls.size() - 2];
                for (auto &widget : answered.widgets)
                    if (widget.node == "HOURS_SLIDE")
                        widget.value = sent.substr(sent.rfind(' ') + 1);
            }
            if (calls.calls.size() > made)
            {
                // the frame the move was applied in: the game makes its hours of it in its next one
                answered.answer = "applied";
                port.Build(screen, answered.Single(), answered);
                CHECK(answered.calls == std::vector<std::string>{"await"});
                answered.calls.clear();
            }
            static_cast<VDATA *>(core.GetScriptVariable("iHourWait"))
                ->Set(static_cast<int32_t>(std::stoi(event[2].get<std::string>())));
            port.Build(screen, answered.Single(), answered);
            CHECK(answered.calls.empty());
            CHECK(answered.props == *calls.predicted);
            SetGame(recorded);
            port.Build(screen, calls.Single(), calls);
        }
        CHECK(calls.calls == recorded.at("calls").get<std::vector<std::string>>());
    }
    static_cast<VDATA *>(core.GetScriptVariable("iHourWait"))->Set(static_cast<int32_t>(hours));
    // The screen's scripts change nothing of the game's program: its globals save as they did.
    CHECK(GameState(vm) == before);
}

// A script screen on its recorded models (cases/<file>.json; a case of a screen of several capabilities names its own,
// else it is `capability`, and its modals are checked where it lists them): the props its view gives, the modals and
// back key it binds and what its events send, or the error its build gives ("fails").
void CheckCases(const char *folder, const std::string &file, const std::string &capability = "")
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/cases/" + file + ".json"));
    for (const Json &recorded : fixture.at("cases"))
    {
        INFO(recorded.at("name").get<std::string>());
        storm::ui::InterfaceModel model;
        // "like": the model of that case of the file, with no row of the node "unmark" current.
        const Json &source =
            recorded.contains("like") ? fixture.at("cases").at(recorded.at("like").get<size_t>()) : recorded;
        REQUIRE(storm::ui::ParseInterfaceModel(source.at("model").dump(), model));
        if (recorded.contains("unmark"))
            for (auto &widget : model.widgets)
                if (widget.node == recorded.at("unmark").get<std::string>())
                    widget.value = "0";
        // A screen of several pack screens names the case's own ("folder").
        const auto spec = PackScreen(recorded.value("folder", std::string(folder)).c_str());
        Calls calls(*spec);
        calls.widgets = model.widgets;
        // The notes waiting for the screen (UiTake): {screen: {field: value}}; the states of channels (UiState).
        const Json notes = recorded.value("notes", Json::object()), states = recorded.value("states", Json::object());
        for (const auto &[screen, fields] : notes.items())
        {
            storm::ui::ViewTree &note = calls.notes[screen];
            for (const auto &[name, value] : fields.items())
                note.children.push_back({name, value.is_string() ? value.get<std::string>() : value.dump(), {}});
        }
        for (const auto &[channel, state] : states.items())
            calls.states[channel] = state;
        // "pending": the presses still waiting for the game's answer (a busy button takes no second tap).
        calls.pending = recorded.value("pending", std::vector<std::string>{});
        SetGame(recorded);
        const auto &screen = Screen(vm, recorded.value("capability", capability));
        // "fails": the build refuses the model with this error (a state the game's window never has).
        if (recorded.contains("fails"))
        {
            CHECK_THROWS_WITH(port.Build(screen, calls.Single(), calls),
                              Catch::Contains(recorded.at("fails").get<std::string>()));
            continue;
        }
        port.Build(screen, calls.Single(), calls);
        CHECK(calls.props == recorded.at("props"));
        if (capability.empty() || recorded.contains("modals"))
            CHECK(calls.modals == recorded.at("modals").get<std::vector<std::string>>());
        for (const Json &event : recorded.at("events"))
        {
            // ["build"]: the screen built again, as the runtime does after a tap that asked for it (an amount set on
            // the screen binds the button that sends it).
            if (event.size() == 1 && event[0] == "build")
            {
                calls.first = false;
                port.Build(screen, calls.Single(), calls);
                continue;
            }
            REQUIRE(calls.Binds(event[0], event[1]));
            port.Event(screen, event[0], event[1], event[2], calls);
        }
        CHECK(calls.calls == recorded.at("calls").get<std::vector<std::string>>());
        // "after": the view a build of the same model gives once the events have run (a window a tap opened).
        if (recorded.contains("after"))
        {
            calls.first = false;
            port.Build(screen, calls.Single(), calls);
            CHECK(calls.props == recorded.at("after"));
        }
    }
}

// A row is tapped by its identity: a tap that arrives after the game's list changed (a row above it gone, the rest
// moved up) acts on the row the player tapped, not on whatever is now at its old place. Case `at` of `file` is
// built, row `gone` of the game's table `node` dropped (`renumber`: the rows after it move up, as the engine's tables
// do; else they keep their indices) and built again; then `tap` (its value or, for "<prefix>{}", its element
// id: what the first build drew for row `drawn` of the props list at `list` sends) runs: the calls it made.
std::vector<std::string> TapAfterRowGone(const char *file, const char *folder, const char *capability, const char *node,
                                         int gone, bool renumber, const char *list, size_t drawn, std::string id,
                                         const char *verb, size_t at = 0)
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/cases/" + file + ".json"));
    const Json &recorded = fixture.at("cases").at(at);
    storm::ui::InterfaceModel model;
    REQUIRE(storm::ui::ParseInterfaceModel(recorded.at("model").dump(), model));
    const auto spec = PackScreen(folder);
    SetGame(recorded);
    const auto &screen = Screen(vm, capability);
    Calls calls(*spec);
    calls.widgets = model.widgets;
    port.Build(screen, calls.Single(), calls);
    // What the templates send for the row: its id, its place without one.
    const Json &row = calls.props.at(Json::json_pointer(list)).at(drawn);
    const std::string tapped = row.contains("id") ? row.at("id").get<std::string>() : std::to_string(drawn);
    std::erase_if(calls.widgets, [&](const storm::ui::Widget &w) { return w.node == node && w.index == gone; });
    if (renumber)
        for (auto &widget : calls.widgets)
            if (widget.node == node && widget.index > gone)
                --widget.index;
    calls.first = false;
    port.Build(screen, calls.Single(), calls);
    calls.calls.clear();
    std::string value = tapped;
    if (const size_t at = id.find("{}"); at != std::string::npos)
        id.replace(at, 2, tapped), value.clear();
    port.Event(screen, id, verb, value, calls);
    return calls.calls;
}

TEST_CASE("a row tapped before the list changes acts on the row tapped")
{
    const auto store =
        TapAfterRowGone("trade_store", "trade", "trade_store", "TABLE_LIST", 0, true, "/list/rows", 2, "goods", "row");
    REQUIRE(!store.empty());
    CHECK(store.front() == "send TABLE_LIST#1 activate goods:2:ship:0");
    // The other trade windows and the ship exchange: the engine's rows are the good or item, renumbered as rows go.
    for (const auto &[family, id] :
         std::vector<std::pair<const char *, std::string>>{{"trade_cannons", "cannons:item:37"},
                                                           {"trade_items", "itemstrade:item:3"},
                                                           {"trade_loot", "colonycapture:item:5"},
                                                           {"trade_contraband", "contraband:item:17"}})
    {
        INFO(family);
        const auto row =
            TapAfterRowGone(family, "trade", family, "TABLE_LIST", 0, true, "/list/rows", 2, "goods", "row");
        REQUIRE(!row.empty());
        CHECK(row.front() == "send TABLE_LIST#1 activate commerce:" + id + ":owner:1:role:0");
    }
    const auto cargo =
        TapAfterRowGone("transfer", "transfer", "transfer", "TABLE_LIST", 1, true, "/list/rows", 2, "goods", "row");
    CHECK(cargo == std::vector<std::string>{"send TABLE_LIST#1 select transfer:good:2", "hold"});
    const auto items = TapAfterRowGone("items", "items", "items", "TABLE_ITEMS", 0, false, "/rows", 2, "items", "row");
    REQUIRE(!items.empty());
    CHECK(items.front() == "send TABLE_ITEMS#2 select inventory:30:character:1:role:0");
    // The navigator's slot gone from the hero's people: the cannoner tapped is still the one picked.
    const auto person = TapAfterRowGone("character", "hero", "character", "CHARACTERS_SCROLL", 1, false,
                                        "/subject/rows", 2, "header-subject-{}", "click");
    CHECK(person == std::vector<std::string>{"rebuild", "send CHARACTERS_SCROLL#3 select 1:3", "hold"});
    // The guns' window: the 3 lb guns gone from the hold, the 6 lb tapped are still the ones fitted.
    const auto guns = TapAfterRowGone("ship", "ship", "ship", "CANNONS_TABLE", 0, false, "/sheet/calibres/rows", 1,
                                      "cannons-stock", "row", 5);
    CHECK(guns == std::vector<std::string>{"send CANNONS_TABLE#1 select ", "hold"});
    // The shipyard's captains: the first passenger gone, the third tapped is still the one chosen.
    const auto captain = TapAfterRowGone("shipyard", "shipyard", "shipyard", "PASSENGERSLIST", 0, false,
                                         "/sheet/candidates", 2, "{}", "click", 5);
    CHECK(captain == std::vector<std::string>{"send PASSENGERSLIST#2 select ", "hold"});
    // The journal's quests: the first one gone from the page, the one tapped is still the one opened.
    const auto quest =
        TapAfterRowGone("journal", "journal", "journal", "QUEST_TITLE", 0, true, "/list", 1, "entries", "row");
    CHECK(quest == std::vector<std::string>{"send QUEST_TITLE#0 open q_debt", "hold"});
    // The maps carried: the first one gone, the one tapped is still the one shown.
    const auto map = TapAfterRowGone("map", "map", "map_atlas", "TABLE_MAPS", 1, false, "/maps", 2, "maps", "row");
    CHECK(map == std::vector<std::string>{"send TABLE_MAPS#3 select 3"});
}

TEST_CASE("a deal held at the shipyard answers the game's question about it with the game's ok, another is shown")
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/cases/shipyard.json"));
    const auto spec = PackScreen("shipyard");
    const auto &screen = Screen(vm, "shipyard");
    // Case 1: an offer on the card, its buy button held; case 2: the game's question that follows.
    const auto asked = [&](const std::string &mode) {
        Calls calls(*spec);
        storm::ui::InterfaceModel model;
        REQUIRE(storm::ui::ParseInterfaceModel(fixture.at("cases").at(1).at("model").dump(), model));
        calls.widgets = model.widgets;
        port.Build(screen, calls.Single(), calls);
        port.Event(screen, "primary", "click", "", calls);
        REQUIRE(storm::ui::ParseInterfaceModel(fixture.at("cases").at(2).at("model").dump(), model));
        calls.widgets = model.widgets;
        calls.first = false;
        calls.calls.clear();
        SetGame(Json{{"game", {{"sMessageMode", mode}}}});
        port.Build(screen, calls.Single(), calls);
        return std::pair{calls.calls, calls.props.at("sheet")};
    };
    const auto [answered, none] = asked("ShipBuy");
    CHECK(answered == std::vector<std::string>{"send MSG_OK press ", "await"});
    CHECK(none.is_null());
    const auto [shown, sheet] = asked("RPG_Hint");
    CHECK(shown.empty());
    CHECK(sheet.at("kind") == "confirm");
}

TEST_CASE("the trade screens' script gives the pack screen the props and actions of the recorded models")
{
    for (const std::string capability :
         {"trade_store", "trade_contraband", "trade_cannons", "trade_items", "trade_loot"})
    {
        INFO(capability);
        CheckCases("trade", capability, capability);
    }
}

TEST_CASE("the nations screen's script gives the pack screen the props and actions of the recorded model")
{
    CheckCases("nations", "nations", "nations");
}

TEST_CASE("the crew hire's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("crew", "crew_hire", "crew_hire");
}

TEST_CASE("the sail emblem's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("sail-emblem", "sail_emblem", "sail_emblem");
}

TEST_CASE("the self repair's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("repair", "self_repair", "self_repair");
}

TEST_CASE("the reading screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("reading", "reading");
}

TEST_CASE("the menus' script gives the pack screens the props and actions of the recorded models")
{
    CheckCases("title", "menu");
}

TEST_CASE("the map screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("map", "map");
}

TEST_CASE("the character screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("hero", "character");
}

TEST_CASE("the trade book's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("tradebook", "tradebook");
}

TEST_CASE("the ship exchange's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("transfer", "transfer");
}

TEST_CASE("the journal's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("journal", "journal");
}

TEST_CASE("the new game's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("new-game", "new_game");
}

TEST_CASE("the save list's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("saves", "saves");
}

TEST_CASE("the settings' script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("settings", "settings");
}

TEST_CASE("the items screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("items", "items", "items");
}

TEST_CASE("the shipyard's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("shipyard", "shipyard");
}

TEST_CASE("the table games' script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("table-game", "table");
}

TEST_CASE("the ship screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("ship", "ship");
}

TEST_CASE("the generic layout and the Other sheet give their pack screens the props and actions of the models")
{
    CheckCases("generic", "generic");
}

TEST_CASE("the alchemy screen's script gives the pack screen the props and actions of the recorded models")
{
    CheckCases("alchemy", "alchemy");
}

TEST_CASE("a screen's script gets the admitted widgets in its object, a table's rows with their cells")
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    Calls calls(*spec);
    storm::ui::Widget gold{
        .node = "OUR_GOLD", .type = "text", .label = "1 500", .context = {{"currency", "doubloons"}}};
    storm::ui::Widget row{.node = "TABLE_LIST",
                          .type = "choice",
                          .label = "200\t1840",
                          .value = "1",
                          .index = 2,
                          .row_id = "goods:2",
                          .cells = {{.column = 3,
                                     .text = "Ядра",
                                     .color_argb = 0xffc4ffc4,
                                     .images = {{.kind = "atlas", .group = "GOODS", .image = "Balls"}}}},
                          .actions = {"select", "activate"},
                          .column_headers = {"В трюме", "В магазине", "", "Товар"}};
    storm::ui::Widget other = row;
    other.index = 3;
    other.enabled = false;
    other.cells.clear();
    calls.widgets = {gold, row, other};
    // A capability no script draws: the model is filled all the same, beside the other capabilities' objects.
    port.Build({.capability = "model", .interface = "store", .component = "s-rest"}, calls.Single(), calls);
    ATTRIBUTES *screens = static_cast<VDATA *>(core.GetScriptVariable("UiScreens"))->GetAClass();
    ATTRIBUTES *model = screens->FindAClass(screens, "model.model");
    REQUIRE(model != nullptr);
    const auto at = [&](const char *path) {
        const ATTRIBUTES *found = model->FindAClass(model, path);
        return found && found->HasValue() ? std::string(found->GetValue()) : std::string("<none>");
    };
    CHECK(at("OUR_GOLD.label") == "1 500");
    CHECK(at("OUR_GOLD.context.currency") == "doubloons");
    CHECK(at("TABLE_LIST.headers.c3") == "Товар");
    CHECK(at("TABLE_LIST.rows.r2.id") == "goods:2");
    CHECK(at("TABLE_LIST.rows.r2.cells.c0.text") == "200"); // from the label's columns
    CHECK(at("TABLE_LIST.rows.r2.cells.c1.text") == "1840");
    CHECK(at("TABLE_LIST.rows.r2.cells.c3.text") == "Ядра");
    CHECK(at("TABLE_LIST.rows.r2.cells.c3.color") == "c4ffc4");
    CHECK(at("TABLE_LIST.rows.r2.cells.c3.images.i0.image") == "Balls");
    CHECK(at("TABLE_LIST.rows.r2.offers.activate") == "1");
    CHECK(at("TABLE_LIST.rows.r3.enabled") == "0");
    CHECK(at("TABLE_LIST.rows.r3.offers.activate") == "<none>"); // disabled: the engine takes nothing of it
    CHECK(model->GetAttributeClass("TABLE_LIST")->GetAttributeClass("rows")->GetAttributesNum() == 2);
    CHECK(model->GetAttributesNum() == 2); // one entry per node
}

TEST_CASE("script screen events are refused while a script runs; natives outside a screen and script errors fail")
{
    Log log;
    Vm vm;
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
    const storm::ui::ScriptScreen bad{.capability = "bad", .interface = "bad", .component = "s-rest"};
    CHECK_THROWS_WITH(port.Build(bad, {}, calls),
                      Catch::Contains("script screen bad: UiShow: view nope: is not a prop"));
    // So does a runtime error of the script.
    const storm::ui::ScriptScreen fault{.capability = "fault", .interface = "fault", .component = "s-rest"};
    CHECK_THROWS_WITH(port.Build(fault, {}, calls), Catch::Contains("script screen fault: a script error in UiBuild"));

    // The game's whole numbers by name: a global, an attribute of an object and of a reference; absent or of another
    // type, the native fails the build.
    static_cast<VDATA *>(core.GetScriptVariable("iHourWait"))->Set(static_cast<int32_t>(4));
    port.Build({.capability = "gameint", .interface = "gameint", .component = "s-rest"}, {}, calls);
    CHECK(vm.Integer("InsideRead") == 412);
    static_cast<VDATA *>(core.GetScriptVariable("iHourWait"))->Set(static_cast<int32_t>(0));
    // Its texts the same way; an empty value is "", told apart from an absent attribute, which fails.
    port.Build({.capability = "gametexts", .interface = "gametexts", .component = "s-rest"}, {}, calls);
    const char *text = nullptr;
    REQUIRE(static_cast<VDATA *>(core.GetScriptVariable("InsideText"))->Get(text));
    CHECK(std::string(text) == "kept|1||");
    for (const auto &[capability, failure] :
         {std::pair{"textint", "UiGameText: the game's iHourWait is not a string"},
          std::pair{"textpath", "UiGameText: the game has no GameInterface.TABLE_LIST.nothing"},
          std::pair{"nogame", "UiGameInt: the game has no global NoSuchGlobal"},
          std::pair{"gametext", "UiGameInt: the game's GameName is not an int"},
          std::pair{"gamepath", "UiGameInt: the game has no GameInterface.TABLE_LIST.nothing"},
          // A command the engine has no kind for, or with a field its kind does not read.
          std::pair{"command", "UiCommand: no command kind 'nosuch'"},
          std::pair{"commandfield", "UiCommand: no field extra is known"}})
        CHECK_THROWS_WITH(
            port.Build({.capability = capability, .interface = capability, .component = "s-rest"}, {}, calls),
            Catch::Contains(failure));
    CHECK(vm.Integer("InsideRead") == 412);

    vm.compiler.Release();
    CHECK(vm.attacher.Running() == nullptr);
    CHECK_THROWS_WITH(port.Build(*insideScreen, {}, calls), Catch::Contains("no script program runs"));
}

TEST_CASE("cost of a screen build and of a tap in script, in the script work digest")
{
    Vm vm;
    vm.Start();
    storm::ui_scripts::VmPort port(vm.attacher);
    const auto spec = PackScreen("rest");
    const auto &screen = Screen(vm, "rest");
    const Json fixture = Json::parse(ReadText(Environment("STORM_UI_TESTS") + "/cases/rest.json"));
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
    // Every name of the interface's scripts is Touch and more (the pack's build checks it): the game must have none
    // (one game has a global `touch` itself).
    for (const auto &[name, arguments] : surface.functions)
        if (name.starts_with("touch") && name != "touch")
            FAIL_CHECK("the game has a function " << name);
    for (const std::string &name : surface.globals)
        if (name.starts_with("touch") && name != "touch")
            FAIL_CHECK("the game has a global " << name);
    compiler.Release();
    core_internal.Compiler = nullptr;
}

TEST_CASE("the dialogue's script gives the pack screen the props and actions of the engine's dialogue models")
{
    CheckCases("dialogue", "dialogue");
}
