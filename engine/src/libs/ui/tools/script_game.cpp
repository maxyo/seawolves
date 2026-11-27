#include "script_game.h"

#include "../src/screens/script_screen.h"
#include "compiler.h"
#include "core_impl.h"
#include "platform/storage.hpp"
#include "script_port.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;

namespace storm::ui
{
namespace
{
std::string ReadText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Json Attributes(const ATTRIBUTES &at)
{
    if (at.GetAttributesNum() == 0)
        return at.HasValue() ? at.GetValue() : std::string();
    Json out = Json::object();
    for (uint32_t i = 0; i < at.GetAttributesNum(); ++i)
        out[at.GetAttributeClass(i)->GetThisName()] = Attributes(*at.GetAttributeClass(i));
    return out;
}

void SetAttributes(ATTRIBUTES &at, const Json &value)
{
    while (at.GetAttributesNum() != 0)
        at.DeleteAttributeClassX(at.GetAttributeClass(0u));
    if (!value.is_object())
    {
        at.SetValue(value.is_string() ? value.get<std::string>() : value.dump());
        return;
    }
    for (const auto &[name, child] : value.items())
        SetAttributes(at.CreateAttribute(name), child);
}

ATTRIBUTES &GlobalObject(const std::string &name)
{
    auto *data = static_cast<VDATA *>(core.GetScriptVariable(name.c_str()));
    ATTRIBUTES *at = data ? data->GetAClass() : nullptr;
    if (!at)
        throw std::runtime_error("the fixture game has no global object " + name);
    return *at;
}
} // namespace

struct ScriptGame::Program
{
    std::vector<ScriptScreen> screens;
    storm::ui_scripts::Attacher attacher{
        {[] { return ReadText(STORM_UI_SCRIPT_LIST); },
         [](const std::string &path) { return fio->_FileOrDirectoryExists(path.c_str()); },
         [this](const std::vector<ScriptScreen> &attached) { screens = attached; }, [this] { screens.clear(); }}};
    storm::ui_scripts::VmPort port{attacher};
    COMPILER compiler;
};

ScriptGame &ScriptGame::Get()
{
    static ScriptGame game;
    return game;
}

// The game's files are read while the program compiles, from the game's folder as the working directory (as in the
// engine); the check's own relative paths keep working after it.
ScriptGame::ScriptGame()
{
    for (auto *c : __STORM_CLASSES_REGISTRY) // as CoreImpl::Init: libraries are found by the hash of their name
        c->SetHash(MakeHashValue(c->GetName()));
    const fs::path cwd = fs::current_path();
    fs::current_path(STORM_UI_SCRIPT_GAME);
    storm::storage::select({.game = STORM_UI_SCRIPT_GAME, .layers = {.mobileUi = STORM_UI_SCRIPT_LAYER}});
    fio->ScanResourcePaths();
    program_ = std::make_unique<Program>();
    core_internal.Compiler = &program_->compiler;
    storm::script::SetAttacher(&program_->attacher);
    program_->compiler.SetProgramDirectory("PROGRAM");
    if (!program_->compiler.CreateProgram("seadogs.c") || !program_->compiler.Run())
        throw std::runtime_error("the fixture game's program does not run");
    fs::current_path(cwd);
}

ScriptGame::~ScriptGame()
{
    program_->compiler.Release();
    storm::script::SetAttacher(nullptr);
    core_internal.Compiler = nullptr;
}

const std::vector<ScriptScreen> &ScriptGame::Screens() const
{
    return program_->screens;
}

ScriptPort &ScriptGame::Port()
{
    return program_->port;
}

void ScriptGame::Play(const Json &game)
{
    for (const auto &[name, state] : main_state_)
        if (!game.contains(name))
            SetAttributes(GlobalObject(name), state);
    for (const auto &[name, state] : game.items())
    {
        ATTRIBUTES &at = GlobalObject(name);
        main_state_.try_emplace(name, Attributes(at));
        SetAttributes(at, state);
    }
}

screens::Router::ScriptScreens ScriptGame::Entries(const fs::path &pack)
{
    const Json components = Json::parse(ReadText(pack / "components.json"));
    screens::Router::ScriptScreens entries;
    for (const ScriptScreen &screen : program_->screens)
        for (const Json &entry : components.at("components"))
            if (entry.at("tag") == screen.component)
            {
                specs_.push_back(ParseComponentSpec(entry));
                entries.emplace_back(screen.interface, [&screen, spec = specs_.back().get(), this] {
                    return screens::CreateScriptScreen(screen, *spec, program_->port);
                });
            }
    if (entries.size() != program_->screens.size())
        throw std::runtime_error("the pack " + pack.string() + " lacks a script screen's component");
    return entries;
}
} // namespace storm::ui
