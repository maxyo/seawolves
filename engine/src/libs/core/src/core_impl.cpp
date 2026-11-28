#include "app_script_events.hpp"
#include "load_watch.hpp"
#include "loading_transaction.hpp"
#include "scene_load_status.hpp"
#ifdef STORM_TOUCH_UI
#include "platform/android_ui.hpp"
#endif
#include "core_impl.h"
#include "game_profile.hpp"
#include "loading_scene.hpp"
#include "scene_hold.hpp"

#include "compiler.h"
#include "controls.h"
#include "fs.h"
#include "platform/frame_work.hpp"
#include "script_state.hpp"
#include "platform/host_hooks.hpp"
#include "profiler.hpp"
#include "save_autosave.hpp"
#include "save_file.hpp"
#include "save_state.hpp"
#include "save_trash.hpp"

#include <chrono>
#include <fstream>
#ifdef STORM_TRACY
#include <cxxabi.h>
#include <string>
#include <typeindex>
#include <unordered_map>
#endif

#include "string_compare.hpp"
#include <SDL2/SDL.h>

Core &core = core_internal;

uint64_t get_performance_counter()
{
    return SDL_GetPerformanceCounter();
}

#ifdef STORM_TRACY
namespace
{
// The class of an entity for its profiler zone, demangled once per type.
const std::string &EntityClassName(Entity &entity)
{
    static std::unordered_map<std::type_index, std::string> names;
    auto [it, inserted] = names.try_emplace(typeid(entity));
    if (inserted)
    {
        int status = 0;
        char *demangled = abi::__cxa_demangle(typeid(entity).name(), nullptr, nullptr, &status);
        it->second = status == 0 && demangled ? demangled : typeid(entity).name();
        std::free(demangled);
    }
    return it->second;
}
} // namespace
#define STORM_PROFILE_ENTITY(entity)                                                                                   \
    STORM_PROFILE_ZONE("entity");                                                                                      \
    const std::string &entityName = EntityClassName(entity);                                                           \
    STORM_PROFILE_ZONE_TEXT(entityName.data(), entityName.size())
#else
#define STORM_PROFILE_ENTITY(entity) static_cast<void>(0)
#endif

namespace storm
{
namespace
{
ENGINE_VERSION getTargetEngineVersion(const std::string_view &version)
{
    using namespace std::string_view_literals;

    if (iEquals(version, "sd"sv))
    {
        return ENGINE_VERSION::SEA_DOGS;
    }
    else if (iEquals(version, "potc"sv))
    {
        return ENGINE_VERSION::PIRATES_OF_THE_CARIBBEAN;
    }
    else if (iEquals(version, "ct"sv))
    {
        return ENGINE_VERSION::CARIBBEAN_TALES;
    }
    else if (iEquals(version, "coas"sv))
    {
        return ENGINE_VERSION::CITY_OF_ABANDONED_SHIPS;
    }
    else if (iEquals(version, "teho"sv))
    {
        return ENGINE_VERSION::TO_EACH_HIS_OWN;
    }
    else if (iEquals(version, "latest"sv))
    {
        return ENGINE_VERSION::LATEST;
    }

    return ENGINE_VERSION::UNKNOWN;
}
} // namespace
} // namespace storm

typedef struct
{
    uint32_t code;
    void *pointer;
} CODE_AND_POINTER;

void CoreImpl::ResetCore()
{
    Initialized = false;
    bEngineIniProcessed = false;

    ReleaseServices();

    Services_List.Release();

    delete[] State_file_name;
    State_file_name = nullptr;
}

void CoreImpl::CleanUp()
{
    Initialized = false;
    bEngineIniProcessed = false;
    // Entity destructors still need renderer/geometry/animation services and
    // script attributes. Also empty the realization layers before another run.
    EraseEntities();
    ReleaseServices();
    Compiler->Release();
    Services_List.Release();
    Services_List.Release();
    delete[] State_file_name;
}

void CoreImpl::SetWindow(std::shared_ptr<storm::OSWindow> window)
{
    window_ = std::move(window);
}

void CoreImpl::Init()
{
    Initialized = false;
    bEngineIniProcessed = false;
    State_file_name = nullptr;
    Exit_flag = false;
    State_loading = false;
    Memory_Leak_flag = false;
    Controls = nullptr;
    fTimeScale = 1.0f;
    Compiler = new COMPILER;
#ifdef STORM_SCENE_SWEEP
    // tools/vm_verify --state compares the script state of two engines: the game's time must not follow the clock.
    if (const char *states = std::getenv("STORM_VM_STATE_DIR"); states && *states)
        Timer.FixedElapsed = 20.0f;
#endif

    /* TODO: place this outside CoreImpl */
    SetLayerType(EXECUTE, layer_type_t::execute);
    SetLayerType(REALIZE, layer_type_t::realize);
    SetLayerType(SEA_EXECUTE, layer_type_t::execute);
    SetLayerType(SEA_REALIZE, layer_type_t::realize);
    SetLayerType(INTERFACE_EXECUTE, layer_type_t::execute);
    SetLayerType(INTERFACE_REALIZE, layer_type_t::realize);
    SetLayerType(FADER_EXECUTE, layer_type_t::execute);
    SetLayerType(FADER_REALIZE, layer_type_t::realize);
    SetLayerType(LIGHTER_EXECUTE, layer_type_t::execute);
    SetLayerType(LIGHTER_REALIZE, layer_type_t::realize);
    SetLayerType(VIDEO_EXECUTE, layer_type_t::execute);
    SetLayerType(VIDEO_REALIZE, layer_type_t::realize);
    SetLayerType(EDITOR_REALIZE, layer_type_t::realize);
    SetLayerType(INFO_REALIZE, layer_type_t::realize);
    SetLayerType(SOUND_DEBUG_REALIZE, layer_type_t::realize);
}

void CoreImpl::InitBase()
{
    LoadClassesTable();
}

void CoreImpl::ReleaseBase()
{
    Compiler->Token.Release();
}

bool CoreImpl::Run()
{
    stopFrameProcessing_ = false;

    const auto bDebugWindow = true;
    if (bDebugWindow && core_internal.Controls && core_internal.Controls->GetDebugAsyncKeyState(VK_F7) < 0)
        DumpEntitiesInfo();

    if (Exit_flag)
        return false; // exit
    EndStoppedSession();
    storm::ScriptStateMayChange(); // the frame's scripts, entities and loading

    Timer.Run(); // calc delta time
    storm::save::trash().frame(); // before the frame's scripts: save deletes no save replaced become final

    auto *pVCTime = static_cast<VDATA *>(core_internal.GetScriptVariable("iRealDeltaTime"));
    if (pVCTime)
        pVCTime->Set(static_cast<int32_t>(GetRDeltaTime()));

    auto tt = std::time(nullptr);
    auto local_tm = *std::localtime(&tt);

    auto *pVYear = static_cast<VDATA *>(core_internal.GetScriptVariable("iRealYear"));
    auto *pVMonth = static_cast<VDATA *>(core_internal.GetScriptVariable("iRealMonth"));
    auto *pVDay = static_cast<VDATA *>(core_internal.GetScriptVariable("iRealDay"));

    if (pVYear)
        pVYear->Set(local_tm.tm_year + 1900);
    if (pVMonth)
        pVMonth->Set(local_tm.tm_mon + 1); // tm_mon belongs [0, 11]
    if (pVDay)
        pVDay->Set(local_tm.tm_mday);

    if (Controls && Controls->GetDebugAsyncKeyState('R') < 0)
        Timer.Delta_Time *= 10;
    if (Controls && Controls->GetDebugAsyncKeyState('Y') < 0)
        Timer.Delta_Time = static_cast<uint32_t>(Timer.Delta_Time * 0.2f);

    Timer.Delta_Time = static_cast<uint32_t>(Timer.Delta_Time * fTimeScale);
    Timer.fDeltaTime *= fTimeScale;

    auto *pVData = static_cast<VDATA *>(GetScriptVariable("fHighPrecisionDeltaTime", nullptr));
    if (pVData)
        pVData->Set(Timer.fDeltaTime * 0.001f);

    if (!Initialized)
    {
        Initialize(); // initialization at start or after reset
    }
    if (!bEngineIniProcessed)
        ProcessEngineIniFile();

    {
        STORM_PROFILE_ZONE("script: frame");
        const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Script);
        Compiler->ProcessFrame(Timer.GetDeltaTime());
        Compiler->ProcessEvent("frame");
    }
    EndStoppedSession();

    {
        STORM_PROFILE_ZONE("save/load state");
        const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Load);
        ProcessSaveLoad();
    }
    // A scene the scripts opened this frame without the loading screen, so without the ReloadProgressEnd that runs
    // the stock loading transaction (a quest's sea login: SeaLogin alone; the sea's Tele reload): prepared here by the
    // same transaction, behind the same screen, before its first frame is executed or shown. It waits for the same
    // resources and quality products and logs the same ready line as every other load (effects/scene_reveal.hpp).
    if (!storm::loading::active && !storm::loading::screen_up && storm::loading::screen &&
        storm::render::GetSceneLoadStatus().active && !DeferFrozenScene(storm::render::GetSceneLoadStatus()))
    {
        STORM_PROFILE_ZONE("load: scene opened without the loading screen");
        Trace("Stock loading transaction for a scene the scripts opened without the loading screen: prepared behind it");
        storm::loading::screen(true);
        const bool prepared = PrepareLoadingScene();
        storm::loading::screen(false);
        if (!prepared)
            return false; // aborted: the engine exits (AbortLoadingTransaction)
    }

    {
        STORM_PROFILE_ZONE("services: run start");
        const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Services);
        ProcessRunStart(SECTION_ALL);
    }
    if (stopFrameProcessing_)
    {
        // service asked to skip current frame processing
        return true;
    }

    // ExitProgram() runs the script's ExitMain, which tears the world down (weather, its event
    // handlers...), and only then requests the exit. The rest of that frame must not run: entities
    // would execute and render against the torn-down world.
    if (Exit_flag)
        return false;
    ProcessExecute(); // transfer control to objects via Execute() function
    if (Exit_flag)
        return false;
    ProcessRealize(); // transfer control to objects via Realize() function

    {
        STORM_PROFILE_ZONE("controls");
        const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Controls);
        if (Controls)
            Controls->Update(Timer.rDelta_Time);

        if (Controls)
            ProcessControls();
    }

    entity_manager_.NewLifecycle();

    {
        STORM_PROFILE_ZONE("services: run end (present)");
        const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Present);
        ProcessRunEnd(SECTION_ALL);
    }

    return true;
}

bool CoreImpl::AbortLoadingTransaction(storm::loading::Outcome outcome, uint64_t generation)
{
    return storm::loading::SceneTransaction::abort(*this, outcome, generation);
}

bool CoreImpl::DeferFrozenScene(const storm::render::SceneLoadStatus &status)
{
    return storm::loading::SceneTransaction::defer_frozen(*this, status);
}

bool CoreImpl::LoadingSceneDeferred() const
{
    return storm::loading::SceneTransaction::deferred(*this);
}

bool CoreImpl::PrepareLoadingScene()
{
    return storm::loading::SceneTransaction::prepare(*this);
}

void CoreImpl::ProcessControls()
{
    CONTROL_STATE cs;
    USER_CONTROL uc;

    if (!Controls)
        return;

    for (int32_t n = 0; n < Controls->GetControlsNum(); n++)
    {
        Controls->GetControlState(n, cs);
        if (cs.state == CST_ACTIVATED)
        {
            Controls->GetControlDesc(n, uc);
            Core::Event("Control Activation", "s", uc.name);
        }
        else if (cs.state == CST_INACTIVATED)
        {
            Controls->GetControlDesc(n, uc);
            Core::Event("Control Deactivation", "s", uc.name);
        }
    }
}

// internal functions
bool CoreImpl::Initialize()
{
    ResetCore();

    Initialized = true;

    return true;
}

void CoreImpl::ProcessEngineIniFile()
{
    char String[MAX_PATH];

    bEngineIniProcessed = true;

    auto engine_ini = fio->OpenIniFile(fs::ENGINE_INI_FILE_NAME);
    if (!engine_ini)
        throw std::runtime_error("no 'engine.ini' file");

    auto res = engine_ini->ReadString(nullptr, "program_directory", String, sizeof(String), "");
    if (res)
    {
        Compiler->SetProgramDirectory(String);
    }

    res = engine_ini->ReadString(nullptr, "controls", String, sizeof(String), "");
    if (res)
    {
        core_internal.Controls = static_cast<CONTROLS *>(MakeClass(String));
        if (core_internal.Controls == nullptr)
            core_internal.Controls = static_cast<CONTROLS *>(MakeClass("controls"));
    }
    else
    {
        delete Controls;
        Controls = nullptr;

        core_internal.Controls = new CONTROLS;
    }

    loadCompatibilitySettings(*engine_ini);

    res = engine_ini->ReadString(nullptr, "run", String, sizeof(String), "");
    if (res)
    {
        if (!Compiler->CreateProgram(String))
            throw std::runtime_error("fail to create program");
        if (!Compiler->Run())
            throw std::runtime_error("fail to run program");

        // Script version test
        if (targetVersion_ >= storm::ENGINE_VERSION::LATEST)
        {
            int32_t iScriptVersion = 0xFFFFFFFF;
            auto *pVScriptVersion = static_cast<VDATA *>(core_internal.GetScriptVariable("iScriptVersion"));
            if (pVScriptVersion)
                pVScriptVersion->Get(iScriptVersion);

            const auto expected = storm::gameProfileFacts().script_version;
            if (iScriptVersion != expected)
            {
                spdlog::error("dialog \"Error\": Wrong script version (scripts {}, engine {})", iScriptVersion,
                              expected);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Wrong script version", nullptr);
                Compiler->ExitProgram();
            }
        }
    }
}

bool CoreImpl::LoadClassesTable()
{
    for (auto *c : __STORM_CLASSES_REGISTRY)
    {
        const auto hash = MakeHashValue(c->GetName());
        c->SetHash(hash);
    }

    return true;
}

void CoreImpl::CheckAutoExceptions(uint32_t = 0) const
{
    spdlog::warn("exception thrown");
}

void CoreImpl::Exit()
{
    Exit_flag = true;
}

storm::OSWindow *CoreImpl::GetWindow()
{
    return window_.get();
}

void CoreImpl::SetTimeScale(float _scale)
{
    fTimeScale = _scale;
}

// transfer message arguments and program control to entity, specified by Destination id
uint64_t CoreImpl::Send_Message(entid_t Destination, const char *Format, ...)
{
    MESSAGE message;
    auto *const ptr = GetEntityPointerSafe(Destination); // check for valid destination
    if (!ptr)
        return 0;

    va_list args;
    va_start(args, Format);
    try { message.ResetVA(Format, args); }
    catch (...) { va_end(args); throw; }
    va_end(args);
    const auto classCode = GetClassCode(Destination);
    try { return static_cast<Entity *>(ptr)->ProcessMessage(message); }
    catch (MessageFormatError &error)
    {
        auto *receiver = FindVMA(classCode);
        error.AppendContext(fmt::format("native Send_Message destination={} receiver={} classHash={}",
            Destination, receiver && receiver->GetName() ? receiver->GetName() : "<unknown>", classCode));
        throw;
    }
}

uint32_t CoreImpl::PostEvent(const char *Event_name, uint32_t post_time, const char *Format, ...)
{
    MESSAGE *pMS;
    MESSAGE message;

    entid_t id;

    if (Format != nullptr)
    {
        pMS = new MESSAGE();
        va_list args;
        va_start(args, Format);
        message.ResetVA(Format, args);
        pMS->Reset(Format);

        auto bAction = true;
        while (bAction)
        {
            switch (message.GetCurrentFormatType())
            {
            case 'l':
                int32_t v;
                v = message.Long();
                pMS->Set(v);
                break;
            case 'f':
                float f;
                f = message.Float();
                pMS->Set(f);
                break;
            case 'i':
                id = message.EntityID();
                pMS->SetEntity(id);
                break;
            case 'e':
                VDATA *e;
                e = message.ScriptVariablePointer();
                pMS->Set(e);
                break;
            case 's': {
                const std::string &s = message.StringPointer();
                pMS->Set(s);
                break;
            }
            case 'a':
                ATTRIBUTES *a;
                a = message.AttributePointer();
                pMS->Set(a);
                break;

            default:
                bAction = false;
                break;
            }
        }
        va_end(args);
    }
    else
        pMS = nullptr;

    auto *pEM = new S_EVENTMSG(Event_name, pMS, post_time);
    pEM->bProcess = true;
    Compiler->AddPostEvent(pEM);
    return 0;
}

bool CoreImpl::EventPosted(const char *Event_name)
{
    return Compiler->EventPosted(Event_name);
}

VDATA *CoreImpl::Event(const std::string_view &event_name)
{
    MESSAGE message;
    return Event(event_name, message);
}

VDATA *CoreImpl::Event(const std::string_view &event_name, MESSAGE &message)
{
    // The phase the event is raised from (the HUD adapters, an entity's Execute or Realize, ...) names its origin.
    Compiler->SetEventOrigin(storm::platform::frame_work::state().current);
    const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Script);
    return Compiler->ProcessEvent(event_name.data(), message);
}

void *CoreImpl::MakeClass(const char *class_name)
{
    const int32_t hash = MakeHashValue(class_name);
    for (auto *const c : __STORM_CLASSES_REGISTRY)
        if (c->GetHash() == hash && storm::iEquals(class_name, c->GetName()))
            return c->CreateClass();

    return nullptr;
}

void CoreImpl::ReleaseServices()
{
    for (auto *const c : __STORM_CLASSES_REGISTRY)
        if (c->Service())
            c->Clear();

    Controls = nullptr;
}

VMA *CoreImpl::FindVMA(const char *class_name)
{
    const int32_t hash = MakeHashValue(class_name);
    for (auto *const c : __STORM_CLASSES_REGISTRY)
        if (c->GetHash() == hash && storm::iEquals(class_name, c->GetName()))
            return c;

    return nullptr;
}

VMA *CoreImpl::FindVMA(int32_t hash)
{
    for (auto *const c : __STORM_CLASSES_REGISTRY)
        if (c->GetHash() == hash)
            return c;

    return nullptr;
}

void *CoreImpl::GetService(const char *service_name)
{
    auto *pClass = FindVMA(service_name);
    if (pClass == nullptr)
    {
        CheckAutoExceptions(0);
        return nullptr;
    }

    if (pClass->GetHash() == 0)
    {
        CheckAutoExceptions(0);
        return nullptr;
    }

    if (pClass->GetReference() > 0)
        return pClass->CreateClass();

    const auto class_code = MakeHashValue(service_name);
    pClass->SetHash(class_code);
    auto *service_PTR = InitService(*pClass);
    if (!service_PTR)
    {
        CheckAutoExceptions(0);
        return nullptr;
    }

    Services_List.Add(class_code, class_code, service_PTR);

    return service_PTR;
}

void CoreImpl::Trace(const char *format, ...)
{
    static char buffer_4k[4096];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer_4k, sizeof(buffer_4k) - 4, format, args);
    va_end(args);
    spdlog::info(buffer_4k);
}

// Transfer programm control to objects via Execute() functions
void CoreImpl::ProcessExecute()
{
    uint64_t ticks;
    STORM_PROFILE_ZONE("execute: game logic, physics");
    const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Execute);

    ProcessRunStart(SECTION_EXECUTE);

    const auto deltatime = Timer.GetDeltaTime();
    const auto &entIds = core.GetEntityIds(layer_type_t::execute);
    for (auto id : entIds)
    {
        if (auto *ptr = core.GetEntityPointerSafe(id))
        {
            STORM_PROFILE_ENTITY(*ptr);
            const storm::platform::frame_work::EntityScope entity(typeid(*ptr), false);
            ptr->ProcessStage(Entity::Stage::execute, deltatime);
            EndStoppedSession();
        }
    }

    ProcessRunEnd(SECTION_EXECUTE);
}

void CoreImpl::ProcessRealize()
{
    uint64_t ticks;
    STORM_PROFILE_ZONE("realize: render submit");
    const storm::platform::frame_work::Scope phase(storm::platform::frame_work::Phase::Realize);
    ProcessRunStart(SECTION_REALIZE);

    const auto deltatime = Timer.GetDeltaTime();
    const auto &entIds = core.GetEntityIds(layer_type_t::realize);
    for (auto id : entIds)
    {
        if (auto *ptr = core.GetEntityPointerSafe(id))
        {
            STORM_PROFILE_ENTITY(*ptr);
            const storm::platform::frame_work::EntityScope entity(typeid(*ptr), true);
            ptr->ProcessStage(Entity::Stage::realize, deltatime);
            EndStoppedSession();
        }
    }

    ProcessRunEnd(SECTION_REALIZE);
}

// Scripts stopped for good (COMPILER::ScriptsStopped) leave a game whose logic ran halfway: the session ends, and
// the frame loop's fatal error report shows the reason.
void CoreImpl::EndStoppedSession()
{
    if (const std::string &reason = Compiler->ScriptsStopped(); !reason.empty())
        throw std::runtime_error(reason);
}

void CoreImpl::FailSave(const std::string &record, std::string reason, bool space)
{
    storm::save::trash().rollback(); // nothing replaced the slot the scripts deleted on the way to this save
    save_failure_ = SaveFailure{record, std::move(reason), space};
}

// The state of a save whose description never came: no record is written, since the game refuses to load one
// without a description.
void CoreImpl::FailUndescribedSave()
{
    if (!pending_save_)
        return;
    const auto save = std::move(pending_save_);
    spdlog::error("Save '{}' is not written: the scripts gave it no description", save->record);
    FailSave(save->record, "the scripts gave the save no description");
}

bool CoreImpl::SaveState(const char *file_name)
{
    if (!file_name)
    {
        throw std::logic_error("Bad file name of save");
    }
    FailUndescribedSave();

    // Resolve the path first: file_name usually points into a script stack value,
    // and serializing runs the script OnSave handler, which reuses that storage.
    const std::string name = file_name;
    const auto path = std::filesystem::u8path(fio->ConvertPathResource(name.c_str()));
    if (!storm::storage::writable(storm::storage::folders(), path))
    {
        spdlog::error("Refusing to save '{}' (resolved '{}'): it is not in the player's saves", name, path.string());
        FailSave(name, "the save is not in the player's saves folder");
        return false;
    }

    auto save = std::unique_ptr<PendingSave>(new PendingSave{name, path, {}, {}});
    if (!Compiler->SaveState(save->image))
    {
        FailSave(name, Compiler->ScriptsStopped().empty() ? "the script state could not be serialized"
                                                          : "a game script was stopped midway: its state is not saved");
        return false;
    }
    pending_save_ = std::move(save);
    return true;
}

// Writes the record SaveState began: its state with this description and thumbnail, swapped in atomically
// (save_file.hpp). Until it returns true the slot holds the previous save, or the trash does (save_trash.hpp).
bool CoreImpl::SetSaveData(const char *file_name, void *data_ptr, int32_t data_size)
{
    const auto path = std::filesystem::u8path(fio->ConvertPathResource(file_name ? file_name : ""));
    if (!pending_save_ || path != pending_save_->path)
    {
        spdlog::error("Save description for '{}' without its state: nothing is written", file_name ? file_name : "");
        return false;
    }
    const auto save = std::move(pending_save_);

    std::string error;
    if (data_ptr == nullptr || data_size <= 0 ||
        !storm::save::appendExtData(save->image, Compiler->SaveFileInfo().c_str(), data_ptr,
                                    static_cast<uint32_t>(data_size), error))
    {
        spdlog::error("Cannot save '{}': its description cannot be stored: {}", save->record,
                      error.empty() ? "it is empty" : error);
        FailSave(save->record, "the description of the save could not be stored");
        return false;
    }

    if (std::error_code ec; !path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    const auto writing = std::chrono::steady_clock::now();
    if (!storm::save::writeAtomic(path, save->image, storm::save::checkImage, error))
    {
        spdlog::error("Cannot save '{}' (resolved '{}'): {}; the previous save, if any, is unchanged", save->record,
                      path.string(), error);
        std::error_code ec;
        const auto space = std::filesystem::space(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path(), ec);
        const bool full = error.find(std::generic_category().message(ENOSPC)) != std::string::npos ||
                          error.find(std::generic_category().message(EDQUOT)) != std::string::npos ||
                          (!ec && space.available < save->image.size());
        FailSave(save->record, error, full);
        return false;
    }

    // The new save is durable: the delete of the save it replaced under this name is final.
    storm::save::trash().commit(path);
    spdlog::info("Saved '{}': {} bytes written in {} ms", save->record, save->image.size(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - writing).count());
    return true;
}

// Reads a save for a load and checks everything the load takes from the file, while the running game is whole:
// the file (save_file.hpp), the structure of its state (save_state.hpp) and the program it names
// (COMPILER::BindStateProgram). False with the `problem`; nothing of the file is checked again afterwards.
bool CoreImpl::ReadStateFile(const char *file_name, storm::save::Bytes &raw, storm::save::ParsedState &state,
                             std::string &problem)
{
    const auto path = std::filesystem::u8path(fio->ConvertPathResource(file_name));
    storm::save::Bytes image;
    std::string error;
    const auto source = storm::save::loadWithRecovery(path, image, raw, error);
    if (source == storm::save::Source::None)
    {
        problem = error.empty() ? std::string("unreadable") : error;
        return false;
    }
    if (source == storm::save::Source::Backup)
        spdlog::warn("Save '{}': {}", file_name, error);
    if (!storm::save::parseState(raw.data(), raw.size(), state, error))
    {
        problem = "the script state is damaged: " + error;
        return false;
    }
    problem = Compiler->BindStateProgram(state);
    return problem.empty();
}

std::string CoreImpl::StateFileProblem(const char *file_name)
{
    if (!file_name)
        return "no file name";
    storm::save::Bytes raw;
    storm::save::ParsedState state;
    std::string problem;
    ReadStateFile(file_name, raw, state, problem);
    return problem;
}

// force core to load state file at the start of next game loop, return false if no state file
bool CoreImpl::InitiateStateLoading(const char *file_name)
{
    if (!file_name)
    {
        return false;
    }

    // Reject missing or damaged saves here, while the caller can still react,
    // instead of discovering it after the running game has been torn down.
    if (const std::string problem = StateFileProblem(file_name); !problem.empty())
    {
        spdlog::error("Cannot load save '{}' (resolved '{}'): {}", file_name, fio->ConvertPathResource(file_name), problem);
        storm::loadWatch().Refused(file_name, "the record cannot be read: " + problem);
        return false;
    }

    delete[] State_file_name;

    const auto len = strlen(file_name) + 1;
    State_file_name = static_cast<char *>(new char[len]);
    strcpy_s(State_file_name, len, file_name);
    return true;
}

void CoreImpl::ProcessSaveLoad()
{
    ProcessStateLoading();
    ReportFailedLoad();
    FailUndescribedSave();
    ReportFailedSave();
}

void CoreImpl::ProcessStateLoading()
{
    if (!State_file_name)
    {
        return;
    }
    const std::string record = State_file_name;
    delete[] State_file_name;
    State_file_name = nullptr;

    // The file is read again: a save that changed since InitiateStateLoading must not cost the player the
    // current session.
    storm::save::Bytes raw;
    storm::save::ParsedState state;
    if (std::string problem; !ReadStateFile(record.c_str(), raw, state, problem))
    {
        spdlog::error("Save '{}' cannot be loaded, load cancelled: {}", record, problem);
        storm::loadWatch().Refused(record, "the record cannot be read: " + problem);
        return;
    }

    State_loading = true;
    EraseEntities();
    const bool applied = Compiler->LoadState(raw, state);
    State_loading = false;
    // The game is erased and its program, which ran until now, did not load again: no script is left to bring a
    // menu back. The frame loop shows the reason and ends the session.
    if (!applied)
        throw std::runtime_error("The save '" + record + "' could not be loaded: the game's scripts did not load again");

    if (storm::loadWatch().Armed())
        spdlog::info("load watch: '{}' applied; its state was taken {:.1f} s (wall clock) after it was asked for", record,
                     storm::loadWatch().Waited());
    storm::loadWatch().Loaded();
}

// The load watchdog (load_watch.hpp): a load the player asked for that was refused or never
// started. Said aloud in the log, then the game's scripts bring the menu back (the event
// AndroidLoadFailed: app script interface/android_load_failure.c) and the UI tells the player.
void CoreImpl::ReportFailedLoad()
{
    // Wall-clock time: the core's frame delta is clamped to 100 ms and would stretch the
    // deadline tenfold at the emulator's frame rates.
    static uint64_t last = SDL_GetTicks64();
    const uint64_t now = SDL_GetTicks64();
    const auto failure = storm::loadWatch().Poll(static_cast<double>(now - last) * 0.001);
    last = now;
    if (!failure)
        return;
    spdlog::error("LOAD FAILED: record '{}': {}; the menu is brought back", failure->record, failure->reason);
    if (!Compiler->HasActiveEventHandler(storm::app::script_event::AndroidLoadFailed, storm::app::script_event::AndroidLoadFailed))
        spdlog::error("LOAD FAILED: no script handles AndroidLoadFailed; nothing brings the menu back");
    Core::Event(storm::app::script_event::AndroidLoadFailed, "ss", failure->record.c_str(), failure->reason.c_str());
#ifdef STORM_TOUCH_UI
    storm::android::ui::state("loadFailure",
                              storm::android::uiJson({{"record", failure->record}, {"reason", failure->reason}}));
#endif
}

// Said in the log, the scripts told (AndroidSaveFailed(record, reason): app script
// interface/android_save_failure.c brings the save screen back after a save of the player) and
// the UI tells the player (channel "saveFailure" {record, reason, space, autosave}, once: null follows).
void CoreImpl::ReportFailedSave()
{
    if (!save_failure_)
        return;
    const SaveFailure failure = std::move(*save_failure_);
    save_failure_.reset();
    spdlog::error("SAVE FAILED: record '{}': {}{}", failure.record, failure.reason, failure.space ? " (the storage is full)" : "");
    if (!Compiler->HasActiveEventHandler(storm::app::script_event::AndroidSaveFailed, storm::app::script_event::AndroidSaveFailed))
        spdlog::error("SAVE FAILED: no script handles AndroidSaveFailed");
    Core::Event(storm::app::script_event::AndroidSaveFailed, "ss", failure.record.c_str(), failure.reason.c_str());
#ifdef STORM_TOUCH_UI
    const size_t slash = failure.record.find_last_of("\\/");
    const bool autosave =
        storm::save::IsAutosaveRecord(slash == std::string::npos ? failure.record : failure.record.substr(slash + 1));
    storm::android::ui::state("saveFailure", storm::android::uiJson({{"record", failure.record},
                                                                     {"reason", failure.reason},
                                                                     {"space", failure.space},
                                                                     {"autosave", autosave}}));
    storm::android::ui::state("saveFailure", "null");
#endif
}

void CoreImpl::ProcessRunStart(uint32_t section_code)
{
    uint32_t class_code;
    SERVICE *service_PTR = Services_List.GetService(class_code);
    while (service_PTR)
    {
        const uint32_t section = service_PTR->RunSection();
        if (section == section_code)
        {
            service_PTR->RunStart();
        }
        service_PTR = Services_List.GetServiceNext(class_code);
    }
}

void CoreImpl::ProcessRunEnd(uint32_t section_code)
{
    uint32_t class_code;
    SERVICE *service_PTR = Services_List.GetService(class_code);
    while (service_PTR)
    {
        const uint32_t section = service_PTR->RunSection();
        if (section == section_code)
        {
            service_PTR->RunEnd();
        }
        service_PTR = Services_List.GetServiceNext(class_code);
    }
}

uint32_t CoreImpl::EngineFps()
{
    return Timer.fps;
}

void CoreImpl::SetDeltaTime(int32_t delta_time)
{
    Timer.SetDelta(delta_time);
}

uint32_t CoreImpl::GetDeltaTime()
{
    return storm::loading::active ? 0 : Timer.GetDeltaTime();
}

uint32_t CoreImpl::GetRDeltaTime()
{
    return storm::loading::active ? 0 : Timer.rDelta_Time;
}

ATTRIBUTES *CoreImpl::Entity_GetAttributeClass(entid_t id_PTR, const char *name)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return nullptr;
    if (pE->AttributesPointer == nullptr)
        return nullptr;
    return pE->AttributesPointer->FindAClass(pE->AttributesPointer, name);
}

const char *CoreImpl::Entity_GetAttribute(entid_t id_PTR, const char *name)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return nullptr;
    if (pE->AttributesPointer == nullptr)
        return nullptr;
    return pE->AttributesPointer->GetAttribute(name);
}

uint32_t CoreImpl::Entity_GetAttributeAsDword(entid_t id_PTR, const char *name, uint32_t def)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return def;
    if (pE->AttributesPointer == nullptr)
        return def;
    return pE->AttributesPointer->GetAttributeAsDword(name, def);
}

float CoreImpl::Entity_GetAttributeAsFloat(entid_t id_PTR, const char *name, float def)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return def;
    if (pE->AttributesPointer == nullptr)
        return def;
    return pE->AttributesPointer->GetAttributeAsFloat(name, def);
}

bool CoreImpl::Entity_SetAttribute(entid_t id_PTR, const char *name, const char *attribute)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return false;
    if (pE->AttributesPointer == nullptr)
        return false;
    return pE->AttributesPointer->SetAttribute(name, attribute);
}

bool CoreImpl::Entity_SetAttributeUseDword(entid_t id_PTR, const char *name, uint32_t val)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return false;
    if (pE->AttributesPointer == nullptr)
        return false;
    return pE->AttributesPointer->SetAttributeUseDword(name, val);
}

bool CoreImpl::Entity_SetAttributeUseFloat(entid_t id_PTR, const char *name, float val)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return false;
    if (pE->AttributesPointer == nullptr)
        return false;
    return pE->AttributesPointer->SetAttributeUseFloat(name, val);
}

void CoreImpl::Entity_SetAttributePointer(entid_t id_PTR, ATTRIBUTES *pA)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return;
    pE->AttributesPointer = pA;
    if (pA)
        pE->attributesHandle = pA->GetHandle();
    pE->attributesGoneReported = false;
}

uint32_t CoreImpl::Entity_AttributeChanged(entid_t id_PTR, ATTRIBUTES *pA)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return 0;
    return pE->AttributeChanged(pA);
}

ATTRIBUTES *CoreImpl::Entity_GetAttributePointer(entid_t id_PTR)
{
    Entity *pE = GetEntityPointer(id_PTR);
    if (pE == nullptr)
        return nullptr;
    if (pE->AttributesPointer != nullptr && !ATTRIBUTES::IsAlive(pE->attributesHandle))
    {
        if (!pE->attributesGoneReported)
            spdlog::warn("entity {:x}: the script object it was created with is gone: its arefs are empty",
                         static_cast<uint64_t>(id_PTR));
        pE->attributesGoneReported = true;
        return nullptr;
    }
    return pE->AttributesPointer;
}

void CoreImpl::EraseEntities()
{
    entity_manager_.EraseAll();
}

void CoreImpl::ClearEvents()
{
    Compiler->ClearEvents();
}

void CoreImpl::AppState(bool state)
{
    if (Controls)
        Controls->AppState(state);
}

uint32_t CoreImpl::MakeHashValue(const char *string)
{
    uint32_t hval = 0;

    while (*string != 0)
    {
        char v = *string++;
        if ('A' <= v && v <= 'Z')
            v += 'a' - 'A';

        hval = (hval << 4) + static_cast<uint32_t>(v);
        const uint32_t g = hval & (static_cast<uint32_t>(0xf) << (32 - 4));
        if (g != 0)
        {
            hval ^= g >> (32 - 8);
            hval ^= g;
        }
    }
    return hval;
}

void CoreImpl::DumpEntitiesInfo()
{
}

void *CoreImpl::GetSaveData(const char *file_name, int32_t &data_size)
{
    return Compiler->GetSaveData(file_name, data_size);
}

uint32_t CoreImpl::SetScriptFunction(IFUNCINFO *pFuncInfo)
{
    return Compiler->SetScriptFunction(pFuncInfo);
}

const char *CoreImpl::EngineIniFileName()
{
    return fs::ENGINE_INI_FILE_NAME;
}

int CoreImpl::ScriptCallDepth(const char *func_name)
{
    return Compiler ? Compiler->CallDepth(func_name) : -1;
}

VDATA *CoreImpl::ScriptCallArgument(const char *func_name, uint32_t index)
{
    return Compiler ? Compiler->CallArgument(func_name, index) : nullptr;
}

bool CoreImpl::HasActiveEventHandler(const char *event_name, const char *func_name)
{
    return Compiler && Compiler->HasActiveEventHandler(event_name, func_name);
}

std::optional<int32_t> CoreImpl::GetScriptIntegerDefine(const char *name)
{
    return Compiler->FindIntegerDefine(name);
}

void *CoreImpl::GetScriptVariable(const char *pVariableName, uint32_t *pdwVarIndex)
{
    const VarInfo *real_var;

    storm::CountVariableLookup();
    const auto dwVarIndex = Compiler->VarTab.FindVar(pVariableName);
    if (dwVarIndex == INVALID_VAR_CODE)
    {
        return nullptr;
    }

    real_var = Compiler->VarTab.GetVar(dwVarIndex);
    if (real_var == nullptr)
    {
        return nullptr;
    }

    if (pdwVarIndex)
        *pdwVarIndex = dwVarIndex;

    return real_var->value.get();
}

storm::ENGINE_VERSION CoreImpl::GetTargetEngineVersion() const noexcept
{
    return targetVersion_;
}

ScreenSize CoreImpl::GetScreenSize() const noexcept
{
    switch (targetVersion_)
    {
    case storm::ENGINE_VERSION::PIRATES_OF_THE_CARIBBEAN: {
        return {640, 480};
    }
    default: {
        return {800, 600};
    }
    }
}

void CoreImpl::stopFrameProcessing()
{
    stopFrameProcessing_ = true;
}

void CoreImpl::AddToLayer(layer_index_t index, entid_t id, priority_t priority)
{
    entity_manager_.AddToLayer(index, id, priority);
}

void CoreImpl::EraseEntity(entid_t entity)
{
    entity_manager_.EraseEntity(entity);
}

entid_t CoreImpl::CreateEntity(const char *name, ATTRIBUTES *attr)
{
    return entity_manager_.CreateEntity(name, attr);
}

entptr_t CoreImpl::GetEntityPointer(entid_t id) const
{
    return entity_manager_.GetEntityPointer(id);
}

entptr_t CoreImpl::GetEntityPointerSafe(entid_t id) const
{
    return entity_manager_.IsEntityValid(id) ? GetEntityPointer(id) : nullptr;
}

entid_t CoreImpl::GetEntityId(const char *name) const
{
    return entity_manager_.GetEntityId(name);
}

bool CoreImpl::IsEntityValid(entid_t id) const
{
    return entity_manager_.IsEntityValid(id);
}

entity_container_cref CoreImpl::GetEntityIds(layer_type_t type) const
{
    return entity_manager_.GetEntityIds(type);
}

entity_container_cref CoreImpl::GetEntityIds(layer_index_t index) const
{
    return entity_manager_.GetEntityIds(index);
}

entity_container_cref CoreImpl::GetEntityIds(const char *name) const
{
    return entity_manager_.GetEntityIds(name);
}

void CoreImpl::SetLayerType(layer_index_t index, layer_type_t type)
{
    entity_manager_.SetLayerType(index, type);
}

void CoreImpl::HoldSceneRealize(bool held)
{
    entity_manager_.SetLayerHeld(REALIZE, held);
    entity_manager_.SetLayerHeld(SEA_REALIZE, held);
}

void storm::core::HoldSceneRealize(bool held)
{
    core_internal.HoldSceneRealize(held);
}

void CoreImpl::SetLayerFrozen(layer_index_t index, bool freeze)
{
    entity_manager_.SetLayerFrozen(index, freeze);
}

void CoreImpl::RemoveFromLayer(layer_index_t index, entid_t id)
{
    entity_manager_.RemoveFromLayer(index, id);
}

hash_t CoreImpl::GetClassCode(entid_t id) const
{
    return entity_manager_.GetClassCode(id);
}

bool CoreImpl::IsLayerFrozen(layer_index_t index) const
{
    return entity_manager_.IsLayerFrozen(index);
}

void CoreImpl::ForEachEntity(const std::function<void(entptr_t)> &f)
{
    entity_manager_.ForEachEntity(f);
}

void CoreImpl::loadCompatibilitySettings(INIFILE &inifile)
{
    using namespace storm;

    std::array<char, 128> strBuffer{};
    inifile.ReadString("compatibility", "target_version", strBuffer.data(), strBuffer.size(), "latest");
    const std::string_view target_engine_version = strBuffer.data();

    targetVersion_ = getTargetEngineVersion(target_engine_version);
    if (targetVersion_ == ENGINE_VERSION::UNKNOWN)
    {
        spdlog::warn("Unknown target version '{}' in engine compatibility settings", target_engine_version);
        targetVersion_ = ENGINE_VERSION::LATEST;
    }
}
