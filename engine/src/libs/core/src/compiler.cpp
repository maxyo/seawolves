#include "script_state.hpp"
#include "compiler.h"
#include "platform/android_script_errors.hpp"
#ifdef STORM_TOUCH_UI
#include "platform/android_land_commands.hpp"
#include "platform/android_land_diagnostics.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

#include "core_impl.h"
#include "debug-trap.h"
#include "game_profile.hpp"
#include "fs.h"
#include "logging.hpp"
#include "number_text.hpp"
#include "platform/path_fingerprint.hpp"
#include "save_file.hpp"
#include "save_references.hpp"
#include "save_state.hpp"
#include "script_cache.h"
#include "script_cache_files.hpp"
#include "storm_assert.h"
#include "platform/module_build_id.hpp"

#include <SDL_timer.h>
#include <unordered_map>
#include <fmt/ranges.h>

#define SKIP_COMMENT_TRACING
#define TRACE_OFF
#define INVALID_SEGMENT_INDEX 0xffffffff
#ifndef INVALID_ARRAY_INDEX
#define INVALID_ARRAY_INDEX 0xffffffff
#endif
#define INVALID_OFFSET 0xffffffff
#define DSL_INI_VALUE 0
#define SBUPDATE 4
#define DEF_COMPILE_EXPRESSIONS

namespace
{
storm::script::Attacher *attacher = nullptr; // storm::script::SetAttacher

// An operand of the bytecode: tokens are packed, so it sits at any byte offset and is read by copy
template <typename T> T CodeOperand(const char *at)
{
    T value;
    std::memcpy(&value, at, sizeof(T));
    return value;
}

enum CacheMode : int
{
    kCacheDisabled = 0,             // Disable script caching
    kCacheEnabled = 1,              // Enable script caching and integrity checks
    kCacheEnabledNoRuntimeCheck = 2 // Enable script caching; only check integrity once - should be used if no
                                    // changes(e.g. in the interface scripts) are expected during the session
};

auto GetCacheFolder()
{
    static std::filesystem::path cache_folder = fs::GetCachePath() / "Cache";
    return cache_folder;
}

// Everything that can make cached bytecode stale besides the scripts
// themselves: the engine build (token numbering, internal function table and
// bytecode layout are not stable across builds), the cache layout, and which
// game directory the scripts came from (the cache folder is shared).
constexpr uint64_t kCacheFormatVersion = 3;

uint64_t CalculateCacheKey(const char *program_directory, const std::string &language);

// The engine build: the linker's build ID of the binary this compiler is linked into (engine CMakeLists.txt links
// it with --build-id), a hash of the linked file. It changes with every change of the engine binary, whether its
// objects came from ccache or not, and with nothing else. Throws when the binary has none.
const std::string &EngineBuildId()
{
    static const std::string id = storm::platform::moduleBuildId(reinterpret_cast<const void *>(&CalculateCacheKey));
    return id;
}

uint64_t CalculateCacheKey(const char *program_directory, const std::string &language)
{
    const std::string resolved = fio->ConvertPathResource(program_directory ? program_directory : "");
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(resolved), ec);
    uint64_t key = storm::hash::fnv1a(EngineBuildId());
    key = storm::fingerprint::fnv1a(kCacheFormatVersion, key);
    key = storm::hash::fnv1a(language, key); // %language% includes
    key = storm::fingerprint::fnv1a(static_cast<uint64_t>(storm::gameProfile()), key); // built-in signatures
    key = storm::fingerprint::fnv1a(static_cast<uint64_t>(sizeof(size_t)), key);
    key = storm::hash::fnv1a(ec ? resolved : absolute.lexically_normal().generic_string(), key);
    return storm::fingerprint::mix(key) + fio->GetPathFingerprint(std::filesystem::u8path(resolved));
}

} // namespace

extern INTFUNCDESC IntFuncTable[];

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;

COMPILER::COMPILER()
    : bBreakOnError(false), pRunCodeBase(nullptr), CompilerStage(CS_SYSTEM), pEventMessage(nullptr), SegmentsNum(0),
      InstructionPointer(0), ProgramDirectory(nullptr), bCompleted(false), bEntityUpdate(true),
      pDebExpBuffer(nullptr), nDebExpBufferSize(0), pRun_fi(nullptr),
      bFirstRun(true), bWriteCodeFile(false),
      bDebugInfo(false), DebugSourceLine(0), bDebugExpressionRun(false),
      bTraceMode(true), nDebugTraceLineCode(0), nIOBufferSize(0), pIOBuffer(nullptr), rAP(nullptr),
      script_cache_mode_(kCacheDisabled)

{
    LabelTable.SetStringDataSize(sizeof(uint32_t));
    LabelUpdateTable.SetStringDataSize(sizeof(DOUBLE_DWORD));

    //    FuncTab.KeepNameMode(true);    // true for displaying internal functions names
    InitInternalFunctions();

    strcpy_s(DebugSourceFileName, "<no debug information>");

    SStack.SetVCompiler(this);
    VarTab.SetVCompiler(this);
    srand(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());

    DebugTraceFileName[0] = 0;

    rAX.SetVCompiler(this);
    rBX.SetVCompiler(this);
    ExpressionResult.SetVCompiler(this);


    using storm::logging::getOrCreateLogger;
    logTrace_ = getOrCreateLogger("compile");
    logError_ = getOrCreateLogger("error");
    logStack_ = getOrCreateLogger("script_stack");
    logStack_->set_pattern("%v");
}

COMPILER::~COMPILER()
{
    Release();
}

void COMPILER::Release()
{
    if (attacher)
        attacher->Released();
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (SegmentTable[n].Files_list)
        {
            SegmentTable[n].Files_list->Release();
            delete SegmentTable[n].Files_list;
        }
        SegmentTable[n].Files_list = nullptr;
    }
    SegmentTable.clear();
    SegmentsNum = 0;
    LabelTable.Release();
    LabelUpdateTable.Release();
    Token.Reset();
    delete[] ProgramDirectory;
    ProgramDirectory = nullptr;
    RunningSegmentID = INVALID_SEGMENT_INDEX;

    FuncTab.Release();
    VarTab.Release();
    DefTab.Release();
    SStack.Release();
    EventTab.Release();
    EventMsg.Release();
    SCodec.Release();
    LibriaryFuncs.clear();
    pendingNatives_.clear();
    newArrays_.clear();

    delete[] pDebExpBuffer;
    pDebExpBuffer = nullptr;
    nDebExpBufferSize = 0;


    rAX.Release();
    rBX.Release();
    ExpressionResult.Release();
}

void COMPILER::SetProgramDirectory(const char *dir_name)
{
    delete[] ProgramDirectory;
    ProgramDirectory = nullptr;
    if (dir_name)
    {
        const auto len = strlen(dir_name) + strlen("\\") + 1;
        ProgramDirectory = new char[len];
        strcpy_s(ProgramDirectory, len, dir_name);
        strcat_s(ProgramDirectory, len, "\\");
    }
}

bool COMPILER::AppendFile(std::string &program, const char *file_name)
{
    char buffer[MAX_PATH];
    std::string EngineDir = "storm-engine\\";
    if (strncmp(file_name, EngineDir.c_str(), EngineDir.length()) == 0)
    {
        std::string ExePath = fio->_GetExecutableDirectory() + "resource\\shared\\";
        strcpy_s(buffer, ExePath.c_str());
        strcat_s(buffer, file_name + EngineDir.length());
    }
    else if (ProgramDirectory)
    {
        strcpy_s(buffer, ProgramDirectory);
        strcat_s(buffer, file_name);
    }
    else
    {
        strcpy_s(buffer, file_name);
    }

    const size_t offset = program.size();
    if (const auto *memory = fio->MemoryFile(buffer))
    {
        program += *memory;
    }
    else
    {
        auto fileS = fio->_CreateFile(buffer, std::ios::binary | std::ios::in);
        if (!fileS.is_open())
        {
            return false;
        }
        const auto fsize = fio->_GetFileSize(buffer);
        program.resize(offset + fsize);
        const bool read = fio->_ReadFile(fileS, program.data() + offset, fsize);
        fio->_CloseFile(fileS);
        if (!read)
        {
            program.resize(offset);
            return false;
        }
    }
    if (program.compare(offset, 3, "\xEF\xBB\xBF") == 0)
        program.replace(offset, 3, 3, ' '); // a UTF-8 byte order mark
    program += "\r\n";
    return true;
}

void COMPILER::DTrace(const char *data_PTR, ...)
{
#ifdef DTRACEOFF
    return;
#endif
    if (data_PTR == nullptr)
        return;
    char LogBuffer[4096];
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    va_end(args);
    logTrace_->trace(LogBuffer);
}

void COMPILER::SetError(const char *data_PTR, ...)
{
    if (bDebugExpressionRun)
        return;
    if (data_PTR == nullptr)
        return;
    ++storm::android::scriptErrorEpoch;
    char LogBuffer[BUFSIZ];
    char ErrorBuffer[MAX_PATH + BUFSIZ];
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    uint32_t bytes;
    FindErrorSource();

    switch (CompilerStage)
    {
    case CS_SYSTEM:
        sprintf_s(ErrorBuffer, "ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    case CS_COMPILATION:
        compileErrors_++;
        sprintf_s(ErrorBuffer, "COMPILE ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    case CS_RUNTIME:
        sprintf_s(ErrorBuffer, "RUNTIME ERROR in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
        break;
    }
    va_end(args);

    logError_->error(ErrorBuffer);
    if (diagnosticsSink_)
        diagnosticsSink_->emplace_back(ErrorBuffer);

}

void COMPILER::SetWarning(const char *data_PTR, ...)
{
#ifdef WARNINGS_OFF
    return;
#endif
    if (bDebugExpressionRun)
        return;
    char LogBuffer[BUFSIZ];
    char ErrorBuffer[MAX_PATH + BUFSIZ];
    if (data_PTR == nullptr)
        return;
    va_list args;
    va_start(args, data_PTR);
    vsnprintf(LogBuffer, sizeof(LogBuffer) - 4, data_PTR, args);
    uint32_t bytes;
    FindErrorSource();

    sprintf_s(ErrorBuffer, "WARNING in %s(%d): %s", DebugSourceFileName, DebugSourceLine + 1, LogBuffer);
    va_end(args);

    logTrace_->warn(ErrorBuffer);
    if (diagnosticsSink_)
        diagnosticsSink_->emplace_back(ErrorBuffer);
}

std::string COMPILER::ScriptLanguageDirectory(bool report)
{
    // %language% in include paths: the directory of the game language, as the string service
    // takes it from RESOURCE\INI\TEXTS\language.ini ([COMMON] defaultLanguage, [DIRECTORY])
    // unless SetScriptLanguage chose another language
    constexpr const char *language_ini = "resource\\ini\\texts\\language.ini";
    auto ini = fio->OpenIniFile(language_ini);
    if (!ini)
    {
        if (report)
            SetError("%%language%% in an include: %s not found", language_ini);
        return {};
    }
    char language[256]{};
    if (!scriptLanguage_.empty())
        strcpy_s(language, scriptLanguage_.c_str());
    else if (!ini->ReadString("COMMON", "defaultLanguage", language, sizeof(language) - 1, ""))
    {
        if (report)
            SetError("%%language%% in an include: %s has no [COMMON] defaultLanguage", language_ini);
        return {};
    }
    char directory[256]{};
    if (!ini->ReadString("DIRECTORY", language, directory, sizeof(directory) - 1, "") || directory[0] == 0)
    {
        if (report)
            SetError("%%language%% in an include: %s has no [DIRECTORY] entry for '%s'", language_ini, language);
        return {};
    }
    return directory;
}

std::filesystem::path COMPILER::CacheFolder() const
{
    return cacheFolder_.empty() ? GetCacheFolder() : cacheFolder_;
}

void COMPILER::UseScriptCache(std::filesystem::path folder)
{
    cacheFolder_ = std::move(folder);
    script_cache_mode_ = kCacheEnabled;
}

void COMPILER::ConfigureCompileCheck(std::vector<std::string> *sink, bool missing_natives_are_errors)
{
    diagnosticsSink_ = sink;
    missingNativesAreErrors_ = missing_natives_are_errors;
    compileCheck_ = true;
    bDebugInfo = true;
    script_cache_mode_ = kCacheDisabled;
}

const char *COMPILER::GetSegmentCode(const char *segment_name, uint32_t &size) const
{
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (SegmentTable[n].name == segment_name)
        {
            size = SegmentTable[n].CodeSize();
            return SegmentTable[n].code.data();
        }
    }
    size = 0;
    return nullptr;
}

std::vector<std::string> COMPILER::GetSegmentSymbols(const char *segment_name)
{
    std::vector<std::string> names;
    uint32_t id = INVALID_SEGMENT_ID;
    for (uint32_t n = 0; n < SegmentsNum; n++)
        if (SegmentTable[n].name == segment_name)
            id = SegmentTable[n].id;
    if (id == INVALID_SEGMENT_ID)
        return names;
    for (size_t n = 0; n < FuncTab.FuncsNum(); n++)
        if (const auto *fi = FuncTab.GetFuncInfo(n); fi && fi->segment_id == id && fi->offset != INVALID_FUNC_OFFSET)
            names.push_back(fi->name);
    for (size_t n = 0; n < VarTab.GetVarNum(); n++)
        if (const auto *vi = VarTab.GetVarX(n); vi && vi->segment_id == id)
            names.push_back(vi->name);
    for (uint32_t n = 0; n < DefTab.GetDefNum(); n++)
    {
        DEFINFO di;
        if (DefTab.GetDef(di, n) && di.segment_id == id && di.name)
            names.emplace_back(di.name);
    }
    return names;
}

bool COMPILER::RunScriptFunction(const char *func_name, std::string &value)
{
    value.clear();
    const auto func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("function '%s' not found", func_name);
        return false;
    }
    DATA *result = nullptr;
    if (!BC_Execute(func_code, result))
        return false;
    if (result)
    {
        DATA copy;
        copy.SetVCompiler(this);
        copy.Copy(result);
        copy.Convert(VAR_STRING);
        const char *text = nullptr;
        copy.Get(text);
        value = text ? text : "";
        SStack.Pop();
    }
    return true;
}

void COMPILER::LoadPreprocess()
{
    auto engine_ini = fio->OpenIniFile(core_internal.EngineIniFileName());
    if (engine_ini)
    {
        if (engine_ini->GetInt("script", "debuginfo", 0) == 0)
        {
            bDebugInfo = false;
        }
        else
        {
            bDebugInfo = true;
        }
        if (engine_ini->GetInt("script", "codefiles", 0) == 0)
            bWriteCodeFile = false;
        else
            bWriteCodeFile = true;

        // seconds one call from the engine into the scripts may run before it is stopped (0: no limit)
        scriptCallTimeLimitMs_ = static_cast<uint32_t>(engine_ini->GetInt("script", "call_time_limit", 60)) * 1000;
        // On by default: game scripts do not change while the game runs (see
        // tools/android/README.md, "Compiled script cache").
        script_cache_mode_ = engine_ini->GetInt("script", "cache_mode", kCacheEnabledNoRuntimeCheck);
        if (script_cache_mode_ < kCacheDisabled || script_cache_mode_ > kCacheEnabledNoRuntimeCheck)
        {
            spdlog::warn("engine.ini [script] cache_mode = {} is not 0, 1 or 2; the script cache is off",
                         script_cache_mode_);
            script_cache_mode_ = kCacheDisabled;
        }

    }

}

bool COMPILER::CreateProgram(const char *file_name)
{
    LoadPreprocess();
    const bool bRes = BC_LoadSegment(file_name);

    return bRes;
}

void storm::script::SetAttacher(Attacher *engine)
{
    attacher = engine;
}

void COMPILER::AttachEngineSegments()
{
    if (attacher)
        attacher->Attach(*this);
}

bool COMPILER::Attach(const char *segment)
{
    if (BC_SegmentIsLoaded(segment))
    {
        SetError("Attach: segment %s is loaded already", segment);
        return false;
    }
    if (!BC_LoadSegment(segment))
        return false;
    SegmentTable.back().attached = true;
    return true;
}

std::string COMPILER::RunningSegment() const
{
    if (!execute_depth_)
        return {};
    for (const SEGMENT_DESC &segment : SegmentTable)
        if (segment.id == RunningSegmentID)
            return segment.name;
    return {};
}

void COMPILER::ForEachFunction(const std::function<void(const storm::script::Function &)> &visit) const
{
    for (size_t n = 0; n < FuncTab.FuncsNum(); n++)
        if (const FuncInfo *function = FuncTab.GetFuncPtr(n))
            visit({function->name, function->arguments});
}

void COMPILER::ForEachGlobal(const std::function<void(std::string_view name)> &visit) const
{
    for (size_t n = 0; n < VarTab.GetVarNum(); n++)
        if (const VarInfo *variable = VarTab.GetVar(n))
            visit(variable->name);
}

void COMPILER::ForEachEvent(const std::function<void(std::string_view name)> &visit) const
{
    EventTab.ForEachHandled([&](const EVENTINFO &event) { visit(event.name); });
}

bool COMPILER::Run()
{
    pRun_fi = nullptr;
    const uint32_t function_code = FuncTab.FindFunc("Main");
    DATA *pResult;
    bFirstRun = false;
    const bool executed = BC_Execute(function_code, pResult);
    pRun_fi = nullptr;
    if (executed)
        AttachEngineSegments();
#ifdef STORM_TOUCH_UI
    else
        storm::android::traceLandAdapter(0, "main-execution-failed");
#endif
#ifdef STORM_SCENE_SWEEP
    LoadSceneSweep();
#endif



    return true;
}

void COMPILER::FindErrorSource()
{
    const uint32_t segment_index = GetSegmentIndex(RunningSegmentID);
    if (segment_index == INVALID_SEGMENT_INDEX)
        return;

    uint32_t index;
    uint32_t token_data_size;
    uint32_t file_line_offset;
    uint32_t file_name_size;
    S_TOKEN_TYPE Token_type;

    uint32_t ip = 0;
    DebugSourceLine = 0;
    file_line_offset = 0;
    const char *pCodeBase = SegmentTable[segment_index].code.data();
    do
    {
        Token_type = static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pCodeBase[ip]));
        ip++;
        if (static_cast<uint8_t>(pCodeBase[ip]) < 0xff)
        {
            token_data_size = static_cast<uint8_t>(pCodeBase[ip]);
            ip++;
        }
        else
        {
            ip++;
            memcpy(&token_data_size, &pCodeBase[ip], sizeof(uint32_t));
            ip += sizeof(uint32_t);
        }

        if (ip >= InstructionPointer)
        {
            return;
        }
        switch (Token_type)
        {
        case DEBUG_LINE_CODE:
            memcpy(&DebugSourceLine, &pCodeBase[ip], sizeof(uint32_t));
            break;
        case DEBUG_FILE_NAME:
            index = ip;
            memcpy(&file_line_offset, &pCodeBase[index], sizeof(uint32_t));
            index += sizeof(uint32_t);
            memcpy(&file_name_size, &pCodeBase[index], sizeof(uint32_t));
            index += sizeof(uint32_t);
            memcpy(DebugSourceFileName, &pCodeBase[index], file_name_size);
            index += file_name_size;
            DebugSourceFileName[file_name_size] = 0;
            DebugSourceLine = DSL_INI_VALUE;
            break;
        }
        ip += token_data_size; // step to next instruction
    } while (Token_type != END_OF_PROGRAMM);
}

void COMPILER::SetEventHandler(const char *event_name, const char *func_name, int32_t flag, bool bStatic)
{
    FuncInfo fi;

    if (event_name == nullptr)
    {
        SetError("Invalid event name for SetEventHandler");
        return;
    }
    if (func_name == nullptr)
    {
        SetError("Invalid function name for SetEventHandler");
        return;
    }

    const uint32_t func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("Invalid function code in SetEventHandler");
        return;
    }

    if (!FuncTab.GetFunc(fi, func_code))
    {
        SetError("func not found eror");
        return;
    }

    EventTab.AddEventHandler(event_name, func_code, fi.segment_id, flag, bStatic);
}

void COMPILER::DelEventHandler(const char *event_name, const char *func_name)
{
    if (event_name == nullptr)
    {
        SetError("Bad event name");
        return;
    }

    if (func_name == nullptr)
    {
        SetError("Bad func name");
        return;
    }

    const uint32_t func_code = FuncTab.FindFunc(func_name);
    if (func_code == INVALID_FUNC_CODE)
    {
        SetError("Invalid function code in DelEventHandler");
        return;
    }

    EventTab.SetStatus(event_name, func_code, FSTATUS_DELETED);

    for (int32_t n = 0; n < static_cast<int32_t>(EventMsg.GetClassesNum()); n++)
    {
        S_EVENTMSG *pM = EventMsg.Read(n);
        if (!pM->pEventName)
            continue;
        if (pM->ProcessTime(0))
            continue; // skip events, possible executed on this frame
        if (storm::iEquals(pM->pEventName, event_name))
        {
            EventMsg.Del(n);
            n--;
        }
    }
}

// A handler ProcessEvent would run: FSTATUS_NORMAL (a NEW one becomes normal at the next frame,
// a DELETED one is skipped until ProcessFrame removes it).
bool COMPILER::HasActiveEventHandler(const char *event_name, const char *func_name)
{
    if (!event_name || !func_name)
        return false;
    const uint32_t func_code = FuncTab.FindFunc(func_name);
    return func_code != INVALID_FUNC_CODE && EventTab.HasActiveHandler(event_name, func_code);
}

VDATA *COMPILER::ProcessEvent(const char *event_name)
{
    // TODO: only do if stack debug if enabled (should be runtime configurable)
    // push event name to call stack
    storm::ringbuffer_stack_push_guard push_guard(callStack_);
    push_guard.push(std::make_tuple("", 0U, event_name, kNoArguments, 0U));

    bEventsBreak = false;

    VDATA *pVD = nullptr;
    if (event_name == nullptr)
    {
        SetError("Invalid event name in ProcessEvent");
        return nullptr;
    }

    const uint32_t event_code = EventTab.FindEvent(event_name);
    if (event_code == INVALID_EVENT_CODE)
        return nullptr; // no handlers

    // An event raised by the engine (no script is running): its whole time goes to its name and origin.
    struct Timed
    {
        COMPILER &compiler;
        const uint64_t key;
        const bool outermost;
        const std::chrono::steady_clock::time_point start;
        ~Timed()
        {
            if (!outermost)
                return;
            storm::platform::stall_watch::PublishEvent(nullptr);
            const auto end = std::chrono::steady_clock::now();
            storm::platform::frame_work::EndEvent(end);
            EventWork &work = compiler.event_work_[key];
            work.calls++;
            const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            work.ns += ns;
            work.max_ns = std::max(work.max_ns, ns);
            compiler.event_origin_ = storm::platform::frame_work::Phase::Script;
        }
    } timed{*this, event_code | (static_cast<uint64_t>(event_origin_) << 32), execute_depth_ == 0,
            execute_depth_ == 0 ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};
    // The stall watchdog names the event the game thread is in (platform/stall_watch.hpp), the frame work line the
    // slowest frame's (platform/frame_work.hpp).
    if (timed.outermost)
    {
        storm::platform::stall_watch::PublishEvent(event_name);
        storm::platform::frame_work::BeginEvent(event_name, event_origin_, timed.start);
    }
    // The handlers are read from the table before each call: a handler may add or drop handlers of this event.
    for (uint32_t n = 0;; n++)
    {
        const EVENTINFO *event = EventTab.GetEvent(event_code);
        if (!event || n >= event->elements)
            break;
        if (event->pFuncInfo[n].status != FSTATUS_NORMAL)
            continue;
        const uint32_t func_code = event->pFuncInfo[n].func_code;
        MESSAGE *pMem = pEventMessage;
        if (pMem)
        {
            pMem->Move2Start();
        }

        const uint32_t nStackVars = SStack.GetDataNum(); // remember stack elements num
        // a handler that declares parameters receives the event's values in them
        uint32_t passed = kArgsAsDeclared;
        if (const FuncInfo *handler = FuncTab.GetFuncPtr(func_code); handler && handler->arguments > 0)
        {
            if (!pMem)
            {
                SetError("handler %s of %s takes parameters: the event has no values", handler->name.c_str(),
                         event_name);
                continue;
            }
            for (passed = 0; passed < handler->arguments && pMem->GetCurrentFormatType() != 0; passed++)
                if (!PushEventValue(*pMem))
                    break;
        }
        DATA *pResult;
        BC_Execute(func_code, pResult, nullptr, passed);

        pEventMessage = pMem;
        if (pResult)
            pVD = pResult->GetVarPointer();
        if (pResult)
            SStack.Pop();

        if (nStackVars != SStack.GetDataNum())
        {
            SStack.InvalidateFrom(nStackVars); // restore stack state - crash situation
            pVD = nullptr;
            SetError("process event stack error");
        }

        if (bEventsBreak)
            break;
    }

    pRun_fi = nullptr;

    return pVD;
}

void COMPILER::ProcessEvent(const char *event_name, MESSAGE *pMs)
{
    pEventMessage = pMs;
    ProcessEvent(event_name);
    pEventMessage = nullptr;
}

// The message's next value on the stack, as GetEventData and a handler's parameters receive it.
DATA *COMPILER::PushEventValue(MESSAGE &message)
{
    const char format = message.GetCurrentFormatType();
    DATA *value;
    switch (format)
    {
    case 0:
        SetError("No (more) data on this event");
        return nullptr;
    case 'a':
        value = SStack.Push();
        value->SetType(VAR_AREFERENCE);
        value->SetAReference(message.AttributePointer());
        return value;
    case 'l':
        value = SStack.Push();
        value->Set(message.Long());
        return value;
    case 'f':
        value = SStack.Push();
        value->Set(message.Float());
        return value;
    case 's':
        value = SStack.Push();
        value->Set(message.String().c_str());
        return value;
    case 'i': {
        value = SStack.Push();
        value->SetType(VAR_AREFERENCE);
        const entid_t entity = message.EntityID();
        value->Set(entity);
        value->SetAReference(core_internal.Entity_GetAttributePointer(entity));
        return value;
    }
    case 'e':
        value = SStack.Push();
        value->SetReference(static_cast<DATA *>(message.ScriptVariablePointer()));
        return value;
    default:
        SetError("Invalid data type in event message: '%c'", format);
        return nullptr;
    }
}

VDATA *COMPILER::ProcessEvent(const char *event_name, MESSAGE message)
{
    pEventMessage = &message;
    VDATA *pVD = ProcessEvent(event_name);
    pEventMessage = nullptr;
    return pVD;
}

uint32_t COMPILER::GetSegmentIndex(uint32_t segment_id)
{
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (SegmentTable[n].id == segment_id)
            return n;
    }
    return INVALID_SEGMENT_INDEX;
}

// function release global variables, functions and labels reference for segment
// A segment UnloadSegment marked is gone for the scripts at once (its functions, variables
// and events are invalidated) and freed at the next ProcessFrame; until then it stays in the
// table under its name. It is not loaded: a LoadSegment of the same name in that frame
// (interface.c LoadGameOptions twice in one frame: load, call, unload, load again) loads it
// afresh instead of answering "already loaded" with its functions gone ("Invalid function
// call"), and an unload finds the live copy, not the stale one.
static bool LiveSegment(const SEGMENT_DESC &segment, const char *file_name)
{
    return !segment.bUnload && strcmp(segment.name.c_str(), file_name) == 0;
}

// mark segment for subsequent unload (on ProcessFrame)
void COMPILER::UnloadSegment(const char *segment_name)
{

    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (LiveSegment(SegmentTable[n], segment_name))
        {
            const uint32_t segment_id = SegmentTable[n].id;
            SegmentTable[n].bUnload = true;
            storm::ScriptStateChanged();
            FuncTab.InvalidateBySegmentID(segment_id);
            VarTab.InvalidateBySegmentID(segment_id);
            EventTab.InvalidateBySegmentID(segment_id);
            DefTab.InvalidateBySegmentID(segment_id);
            if (SegmentTable[n].Files_list)
            {
                SegmentTable[n].Files_list->Release();
            }
            if (attacher)
                attacher->Unloaded(SegmentTable[n].name);
            break;
        }
    }
}

bool COMPILER::BC_SegmentIsLoaded(const char *file_name)
{
    if (file_name == nullptr)
    {
        SetError("Invalid segment name");
        return false;
    }
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (LiveSegment(SegmentTable[n], file_name))
            return true;
    }
    return false;
}

bool COMPILER::BC_LoadSegment(const char *file_name)
{
    uint32_t n;

    LabelUpdateTable.Release();

    if (file_name == nullptr)
    {
        SetError("Invalid segment name");
        return false;
    }


    for (n = 0; n < SegmentsNum; n++)
    {
        if (LiveSegment(SegmentTable[n], file_name))
        {
            SetWarning("Segment already loaded: %s", file_name);
            return true;
        }
    }
    storm::ScriptStateChanged(); // a segment comes (or, failing, goes): SegmentIsLoaded changes

    uint32_t id = 0;
    bool found = false;
    while (!found)
    {
        found = true;
        for (n = 0; n < SegmentsNum; n++)
        {
            if (SegmentTable[n].id == id)
                found = false;
        }
        if (!found)
            id++;
    }

    const uint32_t index = SegmentsNum;
    SegmentsNum++;
    SegmentTable.resize(SegmentsNum);

    SegmentTable[index].name = file_name;
    SegmentTable[index].id = id;
    SegmentTable[index].bUnload = false;

    SegmentTable[index].Files_list = new STRINGS_LIST;
    SegmentTable[index].Files_list->SetStringDataSize(sizeof(OFFSET_INFO));
    const auto load_started = std::chrono::steady_clock::now();
    bool result = false;
    bool from_cache = false;
    if (script_cache_mode_ != kCacheDisabled && (!cacheKeyCalculated_ || script_cache_mode_ == kCacheEnabled))
    {
        try
        {
            cacheKey_ = CalculateCacheKey(ProgramDirectory, ScriptLanguageDirectory(false));
            cacheKeyCalculated_ = true;
        }
        catch (const std::exception &error)
        {
            spdlog::error("Script cache: no key ({}); disabled for this session, scripts are compiled from source",
                          error.what());
            script_cache_mode_ = kCacheDisabled;
        }
    }
    if (script_cache_mode_ != kCacheDisabled)
    {
        const uint64_t calculated_fingerprint = cacheKey_;
        using storm::script_cache::StateFile;
        uint64_t stored_fingerprint = 0;
        const auto state = storm::script_cache::readState(CacheFolder(), stored_fingerprint);
        if (state == StateFile::Read && stored_fingerprint == calculated_fingerprint)
        {
            // attempt to load from cache first
            result = from_cache = LoadSegmentFromCache(SegmentTable[index]);
        }
        else
        {
            // Every outcome is logged: a rebuild costs a full compile at startup.
            if (state == StateFile::Missing)
                spdlog::info("Script cache: none in '{}'; compiling and storing scripts", CacheFolder().string());
            else if (state == StateFile::Damaged)
                spdlog::warn("Script cache: state file in '{}' is damaged; rebuilding", CacheFolder().string());
            else
                spdlog::info("Script cache: scripts or engine build changed (key {:016x} -> {:016x}); rebuilding",
                             stored_fingerprint, calculated_fingerprint);
            if (const auto error = storm::script_cache::resetFolder(CacheFolder(), calculated_fingerprint);
                !error.empty())
            {
                spdlog::error("Script cache: {}; disabled for this session, scripts are compiled from source", error);
                script_cache_mode_ = kCacheDisabled;
            }
        }
    }

    if (!result)
    {
        result = Compile(SegmentTable[index]);
    }
    lastSegmentFromCache_ = result && from_cache;
    if (result)
    {
        const int64_t ms = duration_cast<milliseconds>(std::chrono::steady_clock::now() - load_started).count();
        if (!from_cache)
            spdlog::info("Script segment '{}' compiled in {} ms", file_name, ms);
        else
        {
            // A place's load reads its segments again: "script work:" sums them.
            cache_loads_.count++;
            cache_loads_.ms += ms;
            if (ms >= cache_loads_.slowest_ms)
            {
                cache_loads_.slowest_ms = ms;
                cache_loads_.slowest = file_name;
            }
        }
    }

    if (!result && !compileCheck_)
    {
        delete SegmentTable[index].Files_list;
        SegmentsNum--;
        SegmentTable.resize(SegmentsNum);
        FuncTab.InvalidateBySegmentID(id);
        VarTab.InvalidateBySegmentID(id);
        EventTab.InvalidateBySegmentID(id);
        DefTab.InvalidateBySegmentID(id);
    }
    return result;
}

void COMPILER::ProcessFrame(uint32_t DeltaTime)
{
    for (uint32_t n = 0; n < SegmentsNum; n++)
    {
        if (!SegmentTable[n].bUnload)
            continue;
        // unload segment of program
        delete SegmentTable[n].Files_list;
        SegmentTable.erase(SegmentTable.begin() + n);
        SegmentsNum--;
        n = 0;
    }

    EventTab.ProcessFrame();

    for (int32_t ln = 0; ln < static_cast<int32_t>(EventMsg.GetClassesNum()); ln++)
    {
        S_EVENTMSG *pMsg = EventMsg.Read(ln);
        if (pMsg->bInvalide)
            continue;
        if (pMsg->bProcess == false)
        {
            pMsg->ProcessTime(DeltaTime);
            continue;
        }
        if (pMsg->ProcessTime(DeltaTime))
        {
            pMsg->Invalidate();
            ForgetDeletedAttributes(pMsg);
            ProcessEvent(pMsg->pEventName, pMsg->pMessageClass);
        }
    }
    EventMsg.RemoveInvalidated();

    work_frames_++;
    if (std::chrono::steady_clock::now() - work_window_ >= std::chrono::seconds(30))
        ReportScriptWork();
}

// One line: the events by time per frame (ms, calls per frame, the phase they were raised from) and what engine
// code looked up by name outside scripts.
void COMPILER::ReportScriptWork()
{
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - work_window_).count();
    const double frames = work_frames_;
    std::vector<std::pair<uint64_t, EventWork>> events(event_work_.begin(), event_work_.end());
    std::sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.second.ns > b.second.ns; });
    double total = 0.0;
    std::array<double, storm::platform::frame_work::kPhases> by_origin{};
    for (const auto &[key, work] : events)
    {
        total += work.ns;
        by_origin[key >> 32] += work.ns;
    }
    std::string line = fmt::format("script work: {:.1f} s, {} frames; events_ms={:.3f}", seconds, work_frames_,
                                   total / 1e6 / frames);
    for (size_t phase = 0; phase < by_origin.size(); phase++)
        if (by_origin[phase] > 0.0)
            line += fmt::format(" from_{}={:.3f}", storm::platform::frame_work::kPhaseNames[phase], by_origin[phase] / 1e6 / frames);
    line += " top=";
    for (size_t n = 0; n < events.size() && n < 12; n++)
    {
        const EVENTINFO *event = EventTab.GetEvent(static_cast<uint32_t>(events[n].first));
        line += fmt::format("{}{}@{}:{:.3f}x{:.2f}", n ? "," : "", event && event->name ? event->name : "?",
                            storm::platform::frame_work::kPhaseNames[events[n].first >> 32],
                            events[n].second.ns / 1e6 / frames, events[n].second.calls / frames);
    }
    // The touch interface's events always, however rare: a build or a tap answered in script, per call.
    std::string attached;
    for (const auto &[key, work] : events)
        if (const EVENTINFO *event = EventTab.GetEvent(static_cast<uint32_t>(key)); event && AttachedEvent(*event))
            attached += fmt::format("{}{}@{}:{}x avg={:.3f} max={:.3f}", attached.empty() ? "" : ",", event->name,
                                    storm::platform::frame_work::kPhaseNames[key >> 32], work.calls,
                                    work.ns / 1e6 / work.calls, work.max_ns / 1e6);
    if (!attached.empty())
        line += "; interface scripts (ms per call): " + attached;
    storm::ScriptNameLookups &lookups = storm::NameLookups();
    line += fmt::format("; engine lookups by name per frame: attributes={:.1f} (interface adapters {:.1f}, hud {:.1f}) "
                        "variables={:.1f} (hud {:.1f})",
                        lookups.attributes / frames, lookups.adapterAttributes / frames, lookups.hudAttributes / frames,
                        lookups.variables / frames, lookups.hudVariables / frames);
    if (cache_loads_.count)
        line += fmt::format("; segments loaded from cache: {} in {} ms (slowest '{}' {} ms)", cache_loads_.count,
                            cache_loads_.ms, cache_loads_.slowest, cache_loads_.slowest_ms);
    spdlog::info("{}", line);
    cache_loads_ = {};
    lookups = {};
    event_work_.clear();
    work_frames_ = 0;
    work_window_ = now;
}

bool COMPILER::AttachedEvent(const EVENTINFO &event) const
{
    bool any = false;
    for (uint32_t n = 0; n < event.elements; n++)
    {
        if (event.pFuncInfo[n].status != FSTATUS_NORMAL)
            continue;
        const auto segment = std::ranges::find(SegmentTable, event.pFuncInfo[n].segment_id, &SEGMENT_DESC::id);
        if (segment == SegmentTable.end() || !segment->attached)
            return false;
        any = true;
    }
    return any;
}

void COMPILER::CompileToken(SEGMENT_DESC &Segment, S_TOKEN_TYPE Token_type, uint32_t data_blocks_num, ...)
{
    auto &code = Segment.code;
    code.push_back(static_cast<char>(Token_type));

    // the length of the token's data: one byte, or 0xff and four bytes
    va_list args;
    va_start(args, data_blocks_num);
    va_list blocks;
    va_copy(blocks, args);
    uint32_t write_size = 0;
    for (uint32_t n = 0; n < data_blocks_num; n++)
    {
        va_arg(args, const char *);
        write_size += va_arg(args, uint32_t);
    }
    va_end(args);
    if (write_size < 0xff)
    {
        code.push_back(static_cast<char>(write_size));
    }
    else
    {
        code.push_back(static_cast<char>(0xff));
        const auto *size_bytes = reinterpret_cast<const char *>(&write_size);
        code.insert(code.end(), size_bytes, size_bytes + sizeof(write_size));
    }

    for (uint32_t n = 0; n < data_blocks_num; n++)
    {
        const auto *data = va_arg(blocks, const char *);
        const auto data_size = va_arg(blocks, uint32_t);
        code.insert(code.end(), data, data + data_size);
    }
    va_end(blocks);
}

bool COMPILER::InitInternalFunctions()
{
    FuncInfo fi;

    // constants the engine defines for scripts (CL uses them)
    for (const auto &[name, value] : {std::pair<const char *, int32_t>{"INT_MAX", INT32_MAX}, {"INT_MIN", INT32_MIN}})
    {
        DEFINFO di;
        di.name = const_cast<char *>(name);
        di.segment_id = IMPORTED_SEGMENT_ID; // INTERNAL_SEGMENT_ID equals INVALID_SEGMENT_ID (unused entry)
        di.deftype = NUMBER;
        di.data4b = static_cast<uint32_t>(value);
        DefTab.AddDef(di);
    }

    // register internal functions ------------

    const uint32_t internal_functions_num = GetIntFunctionsNum();
    for (uint32_t n = 0; n < internal_functions_num; n++)
    {
        fi.segment_id = INTERNAL_SEGMENT_ID;
        fi.name = IntFuncTable[n].pName;
        fi.offset = n;
        fi.arguments = GetInternalFunctionArgumentsNum(n);
        fi.decl_file_name = "engine";
        fi.decl_line = 0;
        fi.return_type = GetIntFunctionReturnType(n);
        uint32_t func_code = FuncTab.AddFunc(fi);

        if (func_code == INVALID_FUNC_CODE)
        {
            func_code = FuncTab.FindFunc(fi.name);
            FuncTab.GetFunc(fi, func_code);
            SetError("Function [%s] already declared in: %s line %d", fi.name.c_str(), fi.decl_file_name.c_str(),
                     fi.decl_line);
            return false;
        }
    }
    return true;
}

bool COMPILER::Compile(SEGMENT_DESC &Segment)
{
    char file_name[MAX_PATH];
    char func_name[MAX_PATH];
    char var_name[MAX_PATH];
    std::string program; // the segment's file with the files it includes appended
    FuncInfo fi;
    VarInfo vi = {};
    const VarInfo *real_var = nullptr;
    LocalVarInfo lvi;
    uint32_t func_code;
    uint32_t var_code;
    uint32_t def_code;
    uint32_t inout;
    uint32_t bracket_inout;
    uint32_t n;
    uint32_t file_code;
    ptrdiff_t Control_offset;
    uint32_t Current_line;
    STRINGS_LIST BlockTable;
    S_TOKEN_TYPE Token_type;
    DEFINFO di;

    CompilerStage = CS_COMPILATION;

    RunningSegmentID = INVALID_SEGMENT_INDEX;

    script_cache_ = storm::ScriptCache();

    bool bFunctionBlock;
    bool bPass1Ok = true; // false after a recoverable declaration error: keep scanning, then fail
    int32_t lvalue;
    float fvalue;

    Control_offset = 0;
    strcpy_s(file_name, Segment.name.c_str());

    auto is_new = Segment.Files_list->AddUnicalString(file_name);
    file_code = Segment.Files_list->GetStringCode(file_name);
    if (is_new && script_cache_mode_ != kCacheDisabled)
    {
        script_cache_.files.emplace_back(file_name);
    }
    if (!AppendFile(program, file_name))
    {
        SetError("file not found: %s", file_name);
        return false;
    }
    CurrentFuncCode = INVALID_FUNC_CODE;

    // register functions and variables ---------
    strcpy_s(func_name, "null");
    inout = 0;
    bracket_inout = 0;
    bFunctionBlock = false;
    Token.SetProgram(program.data(), program.data());
    if (bDebugInfo)
    {
        strcpy_s(DebugSourceFileName, Segment.name.c_str());
    }
    DebugSourceLine = DSL_INI_VALUE;
    bool bExtern;
    bool bImport;
    bExtern = false;
    bImport = false;
    bool bDotFlag;
    bDotFlag = false;
    // label detection: the two significant tokens before ':' and '?' still waiting for their ':'
    S_TOKEN_TYPE prevToken = SEPARATOR, prevToken2 = SEPARATOR;
    std::string prevText;
    uint32_t pendingQuestions = 0;
    std::unordered_map<std::string, uint32_t> declaredLocals; // pass 1: declarations per name
    bool resumed = false;                                    // see SkipInitializer
    LabelTable.Release(); // labels belong to a function of this segment
    compileErrors_ = 0;   // some compile paths report an error and carry on
    missingNatives_.clear();
    do
    {
        Token_type = Token.Get();
        bDotFlag = false;
        if (Token_type == DOT)
        {
            Token_type = Token.Get();
            bDotFlag = true;
        }
        switch (Token_type)
        {
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case FOR_BLOCK:
            if (inout > 0)
            {
                // for (type x : container): two hidden locals, the container and the position
                char *const at = Token.GetProgramControl();
                const uint32_t line = DebugSourceLine;
                S_TOKEN_TYPE loop_type = UNKNOWN;
                const bool range = Token.Get() == OPEN_BRACKET && PeekRangeFor(loop_type);
                Token.SetProgramControl(at);
                DebugSourceLine = line;
                if (range && func_code != INVALID_FUNC_CODE)
                {
                    LocalVarInfo hidden;
                    hidden.elements = 1;
                    hidden.type = loop_type == VAR_AREFERENCE ? VAR_AREFERENCE : VAR_REFERENCE;
                    hidden.name = LocalSlotName(kRangeContainer, declaredLocals[kRangeContainer]++);
                    FuncTab.AddFuncVar(func_code, hidden);
                    if (script_cache_mode_ != kCacheDisabled)
                        script_cache_.functions.back().local_variables.emplace_back(hidden);
                    hidden.type = VAR_INTEGER;
                    hidden.name = LocalSlotName(kRangePosition, declaredLocals[kRangePosition]++);
                    FuncTab.AddFuncVar(func_code, hidden);
                    if (script_cache_mode_ != kCacheDisabled)
                        script_cache_.functions.back().local_variables.emplace_back(hidden);
                }
            }
            break;
        case OP_QUESTION:
            pendingQuestions++;
            break;
        case LABEL:
            if (pendingQuestions > 0)
            {
                pendingQuestions--; // ':' of a conditional expression
                break;
            }
            // "name:" starting a statement; "default:" belongs to a switch
            if (inout > 0 && prevToken == UNKNOWN && !storm::iEquals(prevText, "default") &&
                (prevToken2 == SEPARATOR || prevToken2 == BLOCK_IN || prevToken2 == BLOCK_OUT ||
                 prevToken2 == LABEL))
            {
                if (!LabelTable.AddUnicalString(LabelKey(func_name, prevText.c_str()).c_str()))
                {
                    SetError("duplicate label: %s", prevText.c_str());
                    bPass1Ok = false;
                }
            }
            break;
        case CASE_COMMAND:
            // the value is an expression up to its ':'
            for (uint32_t depth = 0;;)
            {
                const S_TOKEN_TYPE t = Token.Get();
                if (t == END_OF_PROGRAMM)
                    break;
                if (t == DEBUG_LINEFEED)
                    DebugSourceLine++;
                else if (t == OPEN_BRACKET)
                    depth++;
                else if (t == CLOSE_BRACKET && depth > 0)
                    depth--;
                else if (t == OP_QUESTION)
                    pendingQuestions++;
                else if (t == LABEL && depth == 0)
                {
                    if (pendingQuestions == 0)
                        break;
                    pendingQuestions--;
                }
            }
            Token_type = LABEL;
            break;
        case SEPARATOR:
            if (bFunctionBlock && bExtern)
            {
                bFunctionBlock = false;
                bExtern = false;
            }
            if (bFunctionBlock && bImport)
            {
                bFunctionBlock = false;
                bImport = false;
            }
            break;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            break;
        case DEBUG_FILE_NAME:
            DebugSourceLine = DSL_INI_VALUE;
            strcpy_s(DebugSourceFileName, Token.GetData());
            break;
        case INCLUDE_LIBRIARY: {
            SCRIPT_LIBRIARY *pLib;
            VMA *pClass;

            auto name = std::string_view(Token.GetData());
            auto comparator = [&name](const auto &library) { return storm::iEquals(library.name, name); };

            if (std::ranges::find_if(LibriaryFuncs, comparator) != LibriaryFuncs.end())
            {
                break;
            }

            pClass = core_internal.FindVMA(Token.GetData());
            if (!pClass)
            {
                SetWarning("cant load libriary '%s'", Token.GetData());
                break;
            }

            pLib = static_cast<SCRIPT_LIBRIARY *>(pClass->CreateClass());
            if (pLib)
            {
                std::vector<std::string> undeclared;
                undeclaredNatives_ = &undeclared;
                pLib->Init();
                undeclaredNatives_ = nullptr;
                if (!undeclared.empty())
                    spdlog::info("Script library '{}': {} natives not declared before it is loaded (bound if "
                                 "declared later): {}",
                                 Token.GetData(), undeclared.size(), fmt::join(undeclared, ", "));
            }

            LibriaryFuncs.emplace_back(pLib, Token.GetData());
            if (script_cache_mode_ != kCacheDisabled)
            {
                script_cache_.script_libs.emplace_back(Token.GetData());
            }

            break;
        }
        case INCLIDE_FILE: {
            // CL: "scripts\%language%\file.h" is the file of the current game language
            std::string include_name = Token.GetData();
            if (const auto at = include_name.find("%language%"); at != std::string::npos)
            {
                const std::string language = ScriptLanguageDirectory(true);
                if (language.empty())
                {
                    bPass1Ok = false;
                    break;
                }
                include_name.replace(at, strlen("%language%"), language);
            }
            if (Segment.Files_list->AddUnicalString(include_name.c_str()))
            {
                file_code = Segment.Files_list->GetStringCode(include_name.c_str());
                Control_offset = Token.GetProgramControl() - Token.GetProgramBase(); // store program scan point
                if (bDebugInfo)
                {
                    // the second ';' keeps the line's tokens apart from the file's
                    program.append("#file \"").append(include_name).append("\";;");
                }
                if (!AppendFile(program, include_name.c_str()))
                {
                    SetError("can't load file: %s", include_name.c_str());
                    return false;
                }
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.files.emplace_back(include_name.c_str());
                }
                Token.SetProgram(program.data(), program.data() + Control_offset);
            }
            break;
        }
        case DEFINE_COMMAND: // create define
            // DefineTable
            if (inout == 0)
            {
                strcpy_s(var_name, Token.GetData());
                di.name = var_name;
                di.segment_id = Segment.id;
                Token.Get();
                di.deftype = Token.GetType();
                bool bMinus = false;
                if (Token.GetType() == OP_MINUS)
                {
                    bMinus = true;
                    Token.Get();
                    di.deftype = Token.GetType();
                }
                di.data4b = {};
                if (Token.GetType() == TRUE_CONST || Token.GetType() == FALSE_CONST)
                {
                    // CL: #define X true
                    lvalue = Token.GetType() == TRUE_CONST;
                    di.deftype = NUMBER;
                    memcpy(&di.data4b, &lvalue, sizeof(int32_t));
                }
                switch (Token.GetType())
                {
                case NUMBER:
                    lvalue = ParseIntLiteral(Token.GetData());
                    if (bMinus)
                        lvalue = -lvalue;
                    memcpy(&di.data4b, &lvalue, sizeof(int32_t));
                    break;
                case FLOAT_NUMBER:
                    fvalue = static_cast<float>(atof(Token.GetData()));
                    if (bMinus)
                        fvalue = -fvalue;
                    memcpy(&di.data4b, &fvalue, sizeof(float));
                    break;
                case STRING: {
                    const auto len = strlen(Token.GetData()) + 1;
                    di.data4b = (uintptr_t) new char[len];
                    memcpy((void *)di.data4b, Token.GetData(), len);
                    break;
                }
                case OP_MINUS:

                    break;
                }
                def_code = DefTab.AddDef(di);
                if (def_code == INVALID_DEF_CODE)
                {
                    SetError("define redefinition: %s", di.name);
                    bPass1Ok = false;
                    break;
                }
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.defines.emplace_back(storm::script_cache::Define{di.name, di.deftype, di.data4b});
                }
            }
            break;

        case EXTERN:
            Token_type = Token.Get();
            bExtern = true;
        case IMPORT:
            if (!bExtern)
            {
                Token_type = Token.Get();
                bImport = true;
            }
        case VAR_AREFERENCE:
        case VAR_REFERENCE:
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_FREF:
        case TVOID:
            if (inout == 0)
            {
                if (!bFunctionBlock)
                {
                    if (Token.GetData() && strcmp(Token.GetData(), "[") == 0)
                    {
                        // "type []Name(": a function returning an array
                        if (Token.Get() != SQUARE_CLOSE_BRACKET || Token.Get() != UNKNOWN)
                        {
                            SetError("function name expected after 'type []'");
                            bPass1Ok = false;
                            break;
                        }
                    }
                    Token.Get();
                    if (Token.GetType() == OPEN_BRACKET)
                    {
                        // this is function header
                        Token.StepBack();
                        Token.StepBack();
                        Token.Get();           // function name
                        bFunctionBlock = true; // we are stepped into func area

                        strcpy_s(func_name, Token.GetData());

                        fi.segment_id = Segment.id;
                        if (bExtern)
                            fi.offset = INVALID_FUNC_OFFSET;
                        else if (bImport)
                            fi.offset = INVALID_FUNC_OFFSET;
                        else
                            fi.offset = Segment.CodeSize();
                        fi.name = func_name;
                        fi.return_type = Token_type;
                        fi.arguments = 0;
                        fi.decl_file_name = DebugSourceFileName;
                        fi.decl_line = DebugSourceLine;
                        fi.imported_func = nullptr;
                        declaredLocals.clear();
                        fi.native = bImport;
                        fi.varargs = false;
                        fi.default_args.clear();
                        func_code = FuncTab.AddFunc(fi);

                        if (func_code == INVALID_FUNC_CODE)
                        {
                            const auto existing = FuncTab.FindFunc(fi.name);
                            const S_TOKEN_TYPE declared_return = fi.return_type;
                            const std::string declared_name = fi.name;
                            FuncTab.GetFunc(fi, existing);
                            if (bImport && fi.segment_id == INTERNAL_SEGMENT_ID)
                            {
                                if (!ReturnTypesMatch(declared_return, fi.return_type))
                                {
                                    SetError("native declaration of built-in function '%s' returns %s, the engine's "
                                             "returns %s",
                                             declared_name.c_str(), Token.GetTypeName(declared_return),
                                             Token.GetTypeName(fi.return_type));
                                    bPass1Ok = false;
                                }
                                // a script may declare a built-in function as native (CL: Program\internal.c)
                                FuncInfo declared;
                                if (!ScanParameterList(INVALID_FUNC_CODE, false, declared))
                                    bPass1Ok = false;
                                else if (!IsIntFuncVarArgsNum(existing) &&
                                         (declared.arguments != fi.arguments || declared.varargs))
                                {
                                    SetError("native declaration of built-in function '%s' takes %u arguments, the "
                                             "engine's takes %u",
                                             declared_name.c_str(), declared.arguments, fi.arguments);
                                    bPass1Ok = false;
                                }
                                break;
                            }
                            SetError("Function [%s] already declared in: %s line %d", fi.name.c_str(),
                                     fi.decl_file_name.c_str(), fi.decl_line);
                            bPass1Ok = false; // its arguments and locals go nowhere (invalid func_code)
                            FuncInfo ignored;
                            ScanParameterList(INVALID_FUNC_CODE, false, ignored);
                            break;
                        }
                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            script_cache_.functions.emplace_back(storm::script_cache::Function{
                                fi, std::vector<storm::script_cache::FunctionLocalVariable>{}});
                        }
                        FuncInfo declared;
                        if (!ScanParameterList(func_code, bExtern, declared))
                            bPass1Ok = false;
                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            script_cache_.functions.back().info.default_args = declared.default_args;
                            script_cache_.functions.back().info.varargs = declared.varargs;
                        }
                        FuncTab.SetFuncSignature(func_code, std::move(declared.default_args), declared.varargs);
                        if (bImport)
                        {
                            // a library loaded earlier registered it before this declaration
                            if (auto pending = pendingNatives_.find(LowerName(fi.name)); pending != pendingNatives_.end())
                            {
                                IFUNCINFO info = pending->second;
                                pendingNatives_.erase(pending);
                                if (SetScriptFunction(&info) == INVALID_FUNCHANDLE)
                                    bPass1Ok = false;
                            }
                        }
                        break;
                    }
                    Token.StepBack();

                    // global variable
                    Token.StepBack();
                    do
                    {
                        Token.Get();
                        if (Token.GetData() == nullptr)
                        {
                            SetError("Invalid variable name");
                            return false;
                        }
                        vi.name = Token.GetData();
                        vi.segment_id = Segment.id;
                        vi.elements = 1;
                        vi.array = false;
                        vi.type = Token_type;
                        Token.Get();
                        const bool sized = Token.GetType() == SQUARE_OPEN_BRACKET && Token.Get() != SQUARE_CLOSE_BRACKET;
                        if (Token.GetType() == SQUARE_CLOSE_BRACKET)
                        {
                            vi.elements = kRuntimeArraySize; // "string a[];": sized by SetArraySize or @a = n
                            vi.array = true;
                            Token.Get();
                        }
                        else if (sized)
                        {
                            if (Token.GetType() != NUMBER)
                            {
                                def_code = DefTab.FindDef(Token.GetData());
                                if (def_code != INVALID_DEF_CODE)
                                {
                                    DefTab.GetDef(di, def_code);
                                    if (di.deftype == NUMBER)
                                    {
                                        lvalue = di.data4b;
                                    }
                                    else
                                    {
                                        SetError("Invalid array (%s) size", vi.name.c_str());
                                        return false;
                                    }
                                }
                                else
                                {
                                    SetError("Invalid array (%s) size", vi.name.c_str());
                                    return false;
                                }
                            }
                            else
                                lvalue = ParseIntLiteral(Token.GetData());
                            vi.elements = lvalue;
                            vi.array = true;
                            Token.Get(); // SQUARE_CLOSE_BRACKET
                            Token.Get();
                        }
                        var_code = VarTab.AddVar(vi);
                        if (var_code == INVALID_VAR_CODE)
                        {
                            SetError("Duplicate variable name: %s", vi.name.c_str());
                            bPass1Ok = false;
                            continue;
                        }

                        real_var = VarTab.GetVarX(var_code);
                        if (real_var == nullptr)
                        {
                            SetError("Registered variable %s has invalid code", vi.name.c_str());
                            return false;
                        }

                        bool bNeg;
                        if (Token.GetType() == OP_EQUAL)
                        {
                            // global var initialization
                            if (vi.elements > 1)
                            {
                                // array
                                if (SkipAuxiliaryTokens() != BLOCK_IN)
                                {
                                    SetError("Invalid array '%s' initialization", vi.name.c_str());
                                    return false;
                                }
                                uint32_t aindex;
                                aindex = 0;

                                SkipAuxiliaryTokens();
                                while (Token.GetType() != END_OF_PROGRAMM)
                                {
                                    if (Token.GetType() == OP_MINUS)
                                    {
                                        bNeg = true;
                                        Token.Get();
                                    }
                                    else
                                        bNeg = false;
                                    if (Token.GetType() == BLOCK_OUT)
                                        break;
                                    if (Token.GetType() == END_OF_PROGRAMM)
                                        break;

                                    switch (Token.GetType())
                                    {
                                    case TRUE_CONST:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(1);
                                        break;
                                    case FALSE_CONST:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(0);
                                        break;

                                    case NUMBER:
                                        if (vi.type != VAR_INTEGER)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        if (bNeg)
                                            real_var->value->Set(-ParseIntLiteral(Token.GetData()), aindex);
                                        else
                                            real_var->value->Set(ParseIntLiteral(Token.GetData()), aindex);
                                        aindex++;
                                        break;
                                    case FLOAT_NUMBER:
                                        if (vi.type != VAR_FLOAT)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        if (bNeg)
                                            real_var->value->Set(-static_cast<float>(atof(Token.GetData())), aindex);
                                        else
                                            real_var->value->Set(static_cast<float>(atof(Token.GetData())), aindex);
                                        aindex++;
                                        break;
                                    case STRING:
                                        if (vi.type != VAR_STRING)
                                        {
                                            SetError("Invalid array '%s' initialization parameter", vi.name.c_str());
                                            return false;
                                        }
                                        real_var->value->Set(Token.GetData(), aindex);
                                        aindex++;
                                        break;
                                    case UNKNOWN:

                                        break;
                                    default:
                                        break;
                                    }
                                    SkipAuxiliaryTokens();
                                    if (Token.GetType() == BLOCK_OUT)
                                        break;
                                    if (Token.GetType() != COMMA)
                                    {
                                        SetError("Invalid array '%s' initialization parameters list", vi.name.c_str());
                                        return false;
                                    }
                                    SkipAuxiliaryTokens();
                                }
                                SkipAuxiliaryTokens();
                            }
                            else if (vi.type == VAR_OBJECT)
                            {
                                if (NextScanToken() != BLOCK_IN ||
                                    !ScanObjectLiteral(real_var->value->GetAClass()))
                                {
                                    SetError("Invalid object '%s' initialization", vi.name.c_str());
                                    bPass1Ok = false;
                                }
                                Token.Get();
                            }
                            else
                            {
                                // single variable
                                if (Token.Get() == OP_MINUS)
                                {
                                    bNeg = true;
                                    Token.Get();
                                }
                                else
                                    bNeg = false;

                                switch (Token.GetType())
                                {
                                case TRUE_CONST:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    real_var->value->Set(1);
                                    break;
                                case FALSE_CONST:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    real_var->value->Set(0);
                                    break;
                                case NUMBER:
                                    if (vi.type != VAR_INTEGER)
                                        break;
                                    if (bNeg)
                                        real_var->value->Set(-ParseIntLiteral(Token.GetData()));
                                    else
                                        real_var->value->Set(ParseIntLiteral(Token.GetData()));
                                    break;
                                case FLOAT_NUMBER:
                                    if (vi.type != VAR_FLOAT)
                                        break;
                                    if (bNeg)
                                        real_var->value->Set(-static_cast<float>(atof(Token.GetData())));
                                    else
                                        real_var->value->Set(static_cast<float>(atof(Token.GetData())));
                                    break;
                                case STRING:
                                    if (vi.type != VAR_STRING)
                                        break;
                                    real_var->value->Set(Token.GetData());
                                    break;
                                case UNKNOWN:

                                    break;
                                default:
                                    break;
                                }
                                Token.Get();
                            }
                        }

                        if (script_cache_mode_ != kCacheDisabled)
                        {
                            auto &cached_var = script_cache_.variables.emplace_back(vi);
                            cached_var.value = std::make_unique<DATA>();
                            *cached_var.value = *real_var->value;
                        }
                    } while (Token.GetType() == COMMA);
                    Token.StepBack();
                    break;
                }
                // ScanParameterList consumed the parameter list after the function name
                SetError("Invalid function declaration");
                bPass1Ok = false;
            }
            else
            {
                // local variables: a name declared again (sibling blocks, two for loops) gets its own
                // slot, pass 2 binds each declaration to it in the same order
                if (Token.TypeCast() || !IsIdentifier(Token.GetData()))
                    break; // a type used as a function-style cast, e.g. int(x)
                lvi.type = Token_type;
                Token.StepBack();
                resumed = false;
                bool first_declarator = true;
                do
                {
                    Token.Get();
                    if (!first_declarator && Token.GetType() != UNKNOWN)
                    {
                        resumed = true; // "int i, ref x": the main loop reads the next declaration
                        Token.StepBack();
                        break;
                    }
                    first_declarator = false;
                    if (!IsIdentifier(Token.GetData()))
                    {
                        SetError("Invalid variable name");
                        bPass1Ok = false;
                        break;
                    }
                    lvi.name = Token.GetData();
                    lvi.elements = 1;
                    lvi.array = false;
                    Token.Get();
                    if (Token.GetType() == SQUARE_OPEN_BRACKET)
                    {
                        lvi.elements = ScanLocalArraySize();
                        lvi.array = true;
                    }
                    if (Token.GetType() == OP_EQUAL)
                    {
                        const uint32_t items = SkipInitializer(resumed);
                        if (lvi.elements == 0)
                            lvi.elements = items;
                    }
                    lvi.name = LocalSlotName(lvi.name, declaredLocals[LowerName(lvi.name)]++);
                    if (func_code != INVALID_FUNC_CODE && !FuncTab.AddFuncVar(func_code, lvi))
                    {
                        SetError("Duplicate variable name: %s", lvi.name.c_str());
                        bPass1Ok = false;
                    }

                    if (script_cache_mode_ != kCacheDisabled)
                    {
                        script_cache_.functions.back().local_variables.emplace_back(lvi);
                    }
                } while (!resumed && Token.GetType() == COMMA);
                if (!resumed)
                    Token.StepBack();
            }
            break;

        case BLOCK_IN:
            inout++;
            break;
        case BLOCK_OUT:
            inout--;
            if (inout == 0)
                bFunctionBlock = false; // sign if we are out of function
            break;
        default:
            break;
        }
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
        case COMMENT:
        case DEBUG_FILE_NAME:
        case INCLIDE_FILE:
            break;
        default:
            prevToken2 = prevToken;
            prevToken = Token_type;
            if (Token_type == UNKNOWN)
                prevText = Token.GetData() ? Token.GetData() : "";
            break;
        }
    } while (Token_type != END_OF_PROGRAMM);

    if (inout != 0)
    {
        SetError("Missing '}' in segment");
        return false;
    }


    Current_line = 1;
    DebugSourceLine = DSL_INI_VALUE;
    uint32_t fnsize;

    if (bDebugInfo)
    {
        strcpy_s(DebugSourceFileName, Segment.name.c_str());
        fnsize = Segment.name.length();
        CompileToken(Segment, DEBUG_FILE_NAME, 3, (char *)&DebugSourceLine, sizeof(uint32_t), (char *)&fnsize,
                     sizeof(uint32_t), Segment.name.c_str(), fnsize);
        CompileToken(Segment, DEBUG_LINE_CODE, 1, (char *)&DebugSourceLine, sizeof(uint32_t));
    }

    uint32_t block_io_code;
    block_io_code = 0;
    inout = 0;
    bracket_inout = 0;
    bFunctionBlock = false;
    Token.SetProgram(program.data(), program.data());

    STRINGS_LIST BreakUpdateTable;
    // After an error, resume at the next top-level declaration so that one run reports every error.
    bool compiled = bPass1Ok;
    while (!CompileBlock(Segment, bFunctionBlock, inout, TVOID, INVALID_OFFSET, INVALID_OFFSET, BreakUpdateTable))
    {
        compiled = false;
        if (!SkipToTopLevel(inout))
            break;
        bFunctionBlock = false;
        inout = 0;
        BreakUpdateTable.Release();
    }
    if (!compiled || compileErrors_ != 0)
        return false;
    DOUBLE_DWORD ddw;
    uint32_t label_offset;
    for (n = 0; n < LabelUpdateTable.GetStringsCount(); n++)
    {
        LabelUpdateTable.GetStringData(n, &ddw);
        LabelTable.GetStringData(ddw.dw1, &label_offset);
        Segment.SetOffset(ddw.dw2, label_offset);
    }
    // Growth leaves spare capacity, and the segment keeps its buffer for as long as it stays loaded.
    Segment.code.shrink_to_fit();

    if (bWriteCodeFile)
    {
        // the compiled code of the script (ini script.codefiles), in the cache: never in the game folder
        const auto codeFolder = fs::GetCachePath() / "ScriptCode";
        std::error_code codeError;
        std::filesystem::create_directories(codeFolder, codeError);
        const auto codeFile = codeFolder / (std::filesystem::path(Segment.name.c_str()).filename().string() + ".b");
        std::ofstream code(codeFile, std::ios::binary | std::ios::trunc);
        code.write(Segment.code.data(), Segment.CodeSize());
        if (!code)
            spdlog::error("Cannot write the code file {}", codeFile.string());
    }
    if (script_cache_mode_ != kCacheDisabled)
    {
        SaveSegmentToCache(Segment);
    }
    return true;
}

bool COMPILER::IsIdentifier(const char *text)
{
    // ASCII letters, '_' and UTF-8 letters (CL has a Cyrillic parameter name)
    return text != nullptr && (std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_' ||
                               static_cast<unsigned char>(text[0]) >= 0x80);
}

std::string COMPILER::LowerName(const std::string &name)
{
    std::string lower(name);
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

std::string COMPILER::LocalSlotName(const std::string &name, uint32_t occurrence)
{
    // the first declaration keeps the plain name (arguments, legacy function-wide lookups)
    return occurrence == 0 ? name : name + "#" + std::to_string(occurrence);
}

std::string COMPILER::LabelKey(const std::string &func_name, const char *label)
{
    return func_name + ":" + label;
}

uint32_t COMPILER::ScanLocalArraySize()
{
    // pass 1, on '[': a constant size, 0 for "[]" (sized by its initializer) or
    // kRuntimeArraySize for a size computed at run time; leaves Token after ']'
    uint32_t size = kRuntimeArraySize;
    S_TOKEN_TYPE t = Token.Get();
    if (t == SQUARE_CLOSE_BRACKET)
    {
        Token.Get();
        return 0;
    }
    if (const auto constant = ConstantArraySize(); constant != 0)
    {
        if (Token.Get() == SQUARE_CLOSE_BRACKET)
        {
            Token.Get();
            return constant;
        }
    }
    for (uint32_t depth = 0; t != END_OF_PROGRAMM; t = Token.Get())
    {
        if (t == DEBUG_LINEFEED)
            DebugSourceLine++;
        else if (t == SQUARE_OPEN_BRACKET)
            depth++;
        else if (t == SQUARE_CLOSE_BRACKET && depth-- == 0)
            break;
    }
    Token.Get();
    return size;
}

uint32_t COMPILER::ConstantArraySize()
{
    // the current token as an array size: a number or a numeric #define; 0 otherwise
    if (Token.GetType() == NUMBER)
        return static_cast<uint32_t>(ParseIntLiteral(Token.GetData()));
    if (Token.GetType() == UNKNOWN && Token.GetData())
    {
        if (const auto def_code = DefTab.FindDef(Token.GetData()); def_code != INVALID_DEF_CODE)
        {
            DEFINFO di;
            DefTab.GetDef(di, def_code);
            if (di.deftype == NUMBER)
                return static_cast<uint32_t>(di.data4b);
        }
    }
    return 0;
}

uint32_t COMPILER::SkipInitializer(bool &resumed)
{
    resumed = false;
    // pass 1, on '=' of a declaration. A {...} list is skipped and its items counted; a plain
    // expression is skipped only up to the ',' of a following declarator. Otherwise (';', or a
    // missing ';' before the next statement, which old scripts have, or a bracket left open) the
    // scan resumes right after '=' as it always did.
    char *const start = Token.GetProgramControl();
    const uint32_t start_line = DebugSourceLine;
    uint32_t depth = 0, items = 0;
    bool empty = true;
    S_TOKEN_TYPE t = Token.Get();
    while (t == DEBUG_LINEFEED || t == COMMENT)
    {
        DebugSourceLine += t == DEBUG_LINEFEED ? 1 : Token.TokenLines();
        t = Token.Get();
    }
    const bool list = t == BLOCK_IN;
    if (!list)
        depth = 0;
    for (;; t = Token.Get())
    {
        switch (t)
        {
        case END_OF_PROGRAMM:
            return items;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            continue;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            continue;
        case BLOCK_IN:
        case OPEN_BRACKET:
        case SQUARE_OPEN_BRACKET:
            if (list && depth == 1 && empty)
            {
                items++;
                empty = false;
            }
            if (t == BLOCK_IN && !list)
                break; // a block: the ';' is missing
            depth++;
            continue;
        case BLOCK_OUT:
        case CLOSE_BRACKET:
        case SQUARE_CLOSE_BRACKET:
            if (depth == 0)
                break;
            depth--;
            if (list && depth == 0)
            {
                Token.Get(); // the ',' or ';' after the list
                return items;
            }
            continue;
        case COMMA:
            if (depth == 0)
                return items; // next declarator
            if (depth == 1 && list)
                empty = true;
            continue;
        case IF_BLOCK:
        case ELSE_BLOCK:
        case FOR_BLOCK:
        case WHILE_BLOCK:
        case SWITCH_COMMAND:
        case FUNCTION_RETURN:
        case SEPARATOR:
            break; // no expression or list holds these: an unclosed bracket ends here, and pass 2 reports it
        default:
            if (list && depth == 1 && empty)
            {
                items++;
                empty = false;
            }
            continue;
        }
        // not a list and no following declarator: resume after '='
        Token.SetProgramControl(start);
        DebugSourceLine = start_line;
        resumed = true;
        return 0;
    }
}

bool COMPILER::BindPassedArguments(const FuncInfo &fi, FuncFrame &frame, uint32_t passed_args)
{
    // fewer arguments: the declared defaults fill the rest; more: the extra ones of a "..." function
    // move off the stack into the frame, where GetArg finds them
    if (passed_args < frame.arguments)
    {
        const auto defaults = static_cast<uint32_t>(fi.default_args.size());
        if (frame.arguments - passed_args > defaults)
        {
            SetError("function '%s' called with %u arguments, needs at least %u", fi.name.c_str(), passed_args,
                     frame.arguments - defaults);
            return false;
        }
        for (uint32_t n = passed_args; n < frame.arguments; n++)
        {
            const auto &value = fi.default_args[n - (frame.arguments - defaults)];
            DATA *pV = SStack.Push();
            switch (value.type)
            {
            case NUMBER:
                pV->Set(value.int_value);
                break;
            case FLOAT_NUMBER:
                pV->Set(value.float_value);
                break;
            case STRING:
                pV->Set(value.string_value);
                break;
            case VAR_FREF:
                pV->ClearType();
                pV->SetFref(value.string_value);
                break;
            default: // nullptr for a ref or aref parameter
                pV->SetType(fi.local_vars[n].type);
                break;
            }
        }
        return true;
    }
    if (!fi.varargs)
    {
        SetError("function '%s' called with %u arguments, takes %u", fi.name.c_str(), passed_args, frame.arguments);
        return false;
    }
    const uint32_t extra = passed_args - frame.arguments;
    const uint32_t first = SStack.GetDataNum() - extra;
    frame.varargs.resize(extra);
    for (uint32_t n = 0; n < extra; n++)
    {
        frame.varargs[n].SetVCompiler(this);
        frame.varargs[n].Copy(SStack.Read(first, n));
    }
    for (uint32_t n = 0; n < extra; n++)
        SStack.Pop();
    return true;
}

const std::string &COMPILER::CurrentFunctionName() const
{
    static const std::string none;
    const auto *fi = FuncTab.GetFuncInfo(CurrentFuncCode);
    return fi ? fi->name : none;
}

bool COMPILER::ReturnTypesMatch(S_TOKEN_TYPE declared, S_TOKEN_TYPE engine)
{
    // engine UNKNOWN: returns values of several types (GetEventData); ptr and int are one value
    auto same = [](S_TOKEN_TYPE t) { return t == VAR_PTR ? VAR_INTEGER : t; };
    return engine == UNKNOWN || same(declared) == same(engine);
}

DATA *COMPILER::Deref(DATA *pV)
{
    // the value a stack entry stands for; nullptr for a null reference (without an error)
    if (pV->Data_type != VAR_REFERENCE)
        return pV;
    return pV->pReference ? pV->pReference->GetVarPointer() : nullptr;
}

int32_t COMPILER::IntValue(DATA *pV)
{
    DATA *value = Deref(pV);
    if (value == nullptr)
    {
        SetError("integer operation on a null reference");
        return 0;
    }
    switch (value->GetType())
    {
    case VAR_INTEGER:
        return value->GetInt();
    case VAR_FLOAT:
        return static_cast<int32_t>(value->GetFloat());
    case VAR_STRING:
        return static_cast<int32_t>(strtoll(value->GetString(), nullptr, 10));
    default:
        SetError("integer operation on a %s value", Token.GetTypeName(value->GetType()));
        return 0;
    }
}

ATTRIBUTES *COMPILER::AttributesOf(DATA *pV)
{
    if (pV->GetType() == VAR_AREFERENCE)
        return pV->AttributesClass; // null for a missing attribute
    DATA *value = Deref(pV);
    if (value == nullptr || value->IsArray())
        return nullptr;
    if (value->GetType() == VAR_AREFERENCE)
        return value->AttributesClass;
    if (value->GetType() == VAR_OBJECT)
        return value->GetAClass();
    return nullptr;
}

int32_t COMPILER::TypeidOf(DATA *pV)
{
    DATA *value = Deref(pV);
    if (value == nullptr)
        return kTypeidRef;
    switch (value->GetType())
    {
    case VAR_INTEGER:
    case VAR_PTR:
        return kTypeidInt;
    case VAR_FLOAT:
        return kTypeidFloat;
    case VAR_STRING:
        return kTypeidString;
    case VAR_OBJECT:
        return kTypeidObject;
    case VAR_AREFERENCE:
        return kTypeidAref;
    case VAR_FREF:
        return kTypeidFref;
    default:
        return kTypeidRef;
    }
}

bool COMPILER::CastValue(DATA *pV, S_TOKEN_TYPE target)
{
    // replaces *pV (a stack value, possibly a reference) with its value converted to target;
    // TRUE_CONST stands for bool
    DATA *value = Deref(pV);
    if (target == TRUE_CONST)
    {
        const bool truth = value != nullptr && value->Truth();
        pV->ClearType();
        pV->Set(static_cast<int32_t>(truth));
        return true;
    }
    if (target == VAR_AREFERENCE)
    {
        // aref(object or reference): the attribute reference of its root
        ATTRIBUTES *attributes = value ? AttributesOf(value) : nullptr;
        pV->ClearType();
        pV->SetType(VAR_AREFERENCE);
        pV->SetAReference(attributes);
        return true;
    }
    if (target == VAR_FREF && (value == nullptr || value->IsNull()))
    {
        pV->ClearType();
        pV->SetFref({});
        return true;
    }
    if (value == nullptr)
    {
        SetError("%s(nullptr)", Token.GetTypeName(target));
        return false;
    }
    DATA result;
    result.SetVCompiler(this);
    if (value->GetType() == VAR_AREFERENCE)
        result.Set(value->GetAClass() ? to_string(value->GetAClass()->GetThisAttr()) : std::string());
    else
        result.Copy(value);
    if (!result.Convert(target))
        return false;
    pV->ClearType();
    pV->Copy(&result);
    return true;
}

uint32_t COMPILER::FindMethod(DATA *self, const char *method)
{
    // object->Method: the object's "classtable" attribute names a global object whose attribute
    // Method names the function
    ATTRIBUTES *object = self ? AttributesOf(self) : nullptr;
    ATTRIBUTES *table_name = object ? object->GetAttributeClass("classtable") : nullptr;
    if (!table_name)
    {
        SetError("->%s: the object has no class table", method);
        return INVALID_FUNC_CODE;
    }
    const std::string table = to_string(table_name->GetThisAttr());
    const auto *var = VarTab.GetVar(VarTab.FindVar(table));
    ATTRIBUTES *entry = var && var->value->GetAClass() ? var->value->GetAClass()->GetAttributeClass(method) : nullptr;
    if (!entry)
    {
        SetError("->%s: class table '%s' has no such method", method, table.c_str());
        return INVALID_FUNC_CODE;
    }
    const std::string function = to_string(entry->GetThisAttr());
    const auto func_code = FuncTab.FindFunc(function);
    if (func_code == INVALID_FUNC_CODE)
        SetError("->%s: function '%s' not found", method, function.c_str());
    return func_code;
}

S_TOKEN_TYPE COMPILER::NextScanToken()
{
    // pass 1: the next significant token, counting source lines
    for (;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        if (t == DEBUG_LINEFEED)
            DebugSourceLine++;
        else if (t == COMMENT)
            DebugSourceLine += Token.TokenLines();
        else
            return t;
    }
}

bool COMPILER::ScanObjectLiteral(ATTRIBUTES *object)
{
    // pass 1, after '{' of a global object's initializer: constant values only
    if (object == nullptr)
        return false;
    for (;;)
    {
        S_TOKEN_TYPE t = NextScanToken();
        if (t == BLOCK_OUT)
            return true;
        if (!((t == UNKNOWN || t == STRING) && Token.GetData()))
            return false;
        std::string key = Token.GetData();
        if (NextScanToken() != OP_EQUAL)
            return false;
        bool negative = false;
        t = NextScanToken();
        if (t == OP_MINUS)
        {
            negative = true;
            t = Token.Get();
        }
        std::string value = Token.GetData() ? Token.GetData() : "";
        switch (t)
        {
        case BLOCK_IN:
            if (!ScanObjectLiteral(object->CreateSubAClass(object, key.c_str())))
                return false;
            break;
        case STRING:
            object->SetAttribute(key, value.c_str());
            break;
        case NUMBER:
            value = std::to_string(negative ? -ParseIntLiteral(value.c_str()) : ParseIntLiteral(value.c_str()));
            object->SetAttribute(key, value.c_str());
            break;
        case FLOAT_NUMBER:
            sprintf_s(gs, "%.7f", (negative ? -1.0f : 1.0f) * static_cast<float>(atof(value.c_str())));
            object->SetAttribute(key, gs);
            break;
        case TRUE_CONST:
        case FALSE_CONST:
            object->SetAttribute(key, t == TRUE_CONST ? "1" : "0");
            break;
        case AND: // &Function: a function reference is the function's name
            if (Token.Get() != UNKNOWN || !Token.GetData())
                return false;
            object->SetAttribute(key, Token.GetData());
            break;
        case UNKNOWN: {
            // a define's value, or a global object by name (classtable)
            const auto def_code = DefTab.FindDef(value.c_str());
            if (def_code != INVALID_DEF_CODE)
            {
                DEFINFO di;
                DefTab.GetDef(di, def_code);
                if (di.deftype == STRING)
                    value = reinterpret_cast<const char *>(di.data4b);
                else if (di.deftype == NUMBER)
                    value = std::to_string(static_cast<int32_t>(di.data4b));
                else
                    return false;
            }
            else if (VarTab.FindVar(value) == INVALID_VAR_CODE)
                return false;
            object->SetAttribute(key, value.c_str());
            break;
        }
        default:
            return false;
        }
        t = NextScanToken();
        if (t == BLOCK_OUT)
            return true;
        if (t != COMMA)
            return false;
    }
}

S_TOKEN_TYPE COMPILER::PeekNextToken()
{
    char *const at = Token.GetProgramControl();
    S_TOKEN_TYPE t;
    do
        t = Token.Get();
    while (t == DEBUG_LINEFEED || t == DEBUG_FILE_NAME || t == COMMENT || t == INCLIDE_FILE || t == WHITESPACE);
    Token.SetProgramControl(at);
    return t;
}

bool COMPILER::PeekRangeFor(S_TOKEN_TYPE &loop_type)
{
    // after "for (": a ':' before the closing ')' makes it a range-for; loop_type is the type of
    // the variable declared right before ':' (the caller restores the position)
    loop_type = UNKNOWN;
    for (uint32_t depth = 0;;)
    {
        const S_TOKEN_TYPE t = Token.Get();
        switch (t)
        {
        case END_OF_PROGRAMM:
        case SEPARATOR:
        case BLOCK_IN:
            return false;
        case OPEN_BRACKET:
            depth++;
            break;
        case CLOSE_BRACKET:
            if (depth-- == 0)
                return false;
            break;
        case LABEL:
            return depth == 0 && loop_type != UNKNOWN;
        case VAR_INTEGER:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_FREF:
            if (depth == 0)
                loop_type = t;
            break;
        default:
            break;
        }
    }
}

bool COMPILER::CompileRangeFor(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE loop_type,
                               STRINGS_LIST &BreakUpdateTable)
{
    // for ([type index,] type item : container) body
    //   aref item: the container's attributes; otherwise the elements of the array it refers to
    const uint32_t depth = inout + 1;
    uint32_t index_slot = INVALID_VAR_CODE;
    uint32_t item_slot = INVALID_VAR_CODE;
    for (;;)
    {
        const S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
        if (t == LABEL)
            break;
        if (t == COMMA)
            continue;
        if (Token.TypeCast() || !IsIdentifier(Token.GetData()))
        {
            SetError("invalid range-for variable");
            return false;
        }
        if (item_slot != INVALID_VAR_CODE)
        {
            if (index_slot != INVALID_VAR_CODE)
            {
                SetError("range-for takes an index and an item variable");
                return false;
            }
            index_slot = item_slot;
        }
        item_slot = BindLocal(Token.GetData(), depth);
        if (item_slot == INVALID_VAR_CODE)
        {
            SetError("Undeclared identifier: %s", Token.GetData());
            return false;
        }
    }
    const uint32_t container = BindLocal(kRangeContainer, depth);
    const uint32_t position = BindLocal(kRangePosition, depth);
    if (container == INVALID_VAR_CODE || position == INVALID_VAR_CODE)
    {
        SetError("range-for: hidden locals missing");
        return false;
    }

    // container
    uint32_t var_code;
    S_TOKEN_TYPE t = CompileAuxiliaryTokens(Segment);
    const bool address = t == AND;
    if (address)
        t = Token.Get();
    const S_TOKEN_TYPE var_type = t == UNKNOWN ? DetectUnknown(var_code) : UNKNOWN;
    if (loop_type != VAR_AREFERENCE)
    {
        // an array variable, by reference
        if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE))
        {
            SetError("range-for over elements needs an array variable");
            return false;
        }
        CompileToken(Segment, SETREF);
        CompileToken(Segment, LOCAL_VARIABLE, 1, &container, sizeof(uint32_t));
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileAuxiliaryTokens(Segment);
    }
    else
    {
        if ((var_type == VARIABLE || var_type == LOCAL_VARIABLE) && IsPlainAttributePath())
        {
            if (!CompileAttributeRoot(Segment))
                return false;
        }
        else
        {
            Token.StepBack();
            if (address)
                Token.StepBack();
            if (!CompileExpression(Segment))
                return false;
        }
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, LOCAL_VARIABLE, 1, &container, sizeof(uint32_t));
    }
    if (Token.GetType() != CLOSE_BRACKET)
    {
        SetError("')' expected after the range-for container");
        return false;
    }
    // position = 0
    const int32_t zero = 0;
    CompileToken(Segment, STACK_PUSH);
    CompileToken(Segment, NUMBER, 1, &zero, sizeof(int32_t));
    CompileToken(Segment, STACK_POP);
    CompileToken(Segment, LOCAL_VARIABLE, 1, &position, sizeof(uint32_t));

    const uint32_t loop = Segment.CodeSize();
    CompileToken(Segment, RANGE_NEXT, 4, &container, sizeof(uint32_t), &position, sizeof(uint32_t), &item_slot,
                 sizeof(uint32_t), &index_slot, sizeof(uint32_t));
    uint32_t jump = 0;
    const uint32_t to_end = Segment.CodeSize() + 2;
    CompileToken(Segment, JUMP_Z, 1, &jump, sizeof(uint32_t));

    if (CompileAuxiliaryTokens(Segment) == BLOCK_IN)
    {
        inout++;
        if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, loop, 6, BreakUpdateTable))
            return false;
    }
    else
    {
        Token.StepBack();
        if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, loop, 6, BreakUpdateTable))
            return false;
        CloseLocalScopes(inout);
    }
    CompileToken(Segment, JUMP, 1, &loop, sizeof(uint32_t));
    Segment.SetOffset(to_end, Segment.CodeSize());
    UpdateOffsets(Segment, BreakUpdateTable, Segment.CodeSize(), "b");
    UpdateOffsets(Segment, BreakUpdateTable, loop, "c");
    return true;
}

bool COMPILER::AssignValue(DATA *destination, DATA *value)
{
    // destination = value, converting the value to the destination's type as MOVE does
    if (!destination)
    {
        SetError("assignment through a null reference");
        return false;
    }
    DATA *source = value->GetVarPointer();
    if (source && source->GetType() != destination->GetType())
    {
        ExpressionResult.ClearType();
        ExpressionResult.Copy(value);
        if (ExpressionResult.IsReference())
            ExpressionResult.RefConvert();
        if (!ExpressionResult.Convert(destination->GetType()))
            return false;
        source = ExpressionResult.GetVarPointer();
    }
    return destination->Copy(source);
}

uint32_t COMPILER::FindLocal(const char *name) const
{
    for (auto it = localScopes_.rbegin(); it != localScopes_.rend(); ++it)
        if (storm::iEquals(it->name, name))
            return it->slot;
    // arguments, and a local used after the block that declared it (function-wide, as before)
    return FuncTab.FindVar(CurrentFuncCode, name);
}

uint32_t COMPILER::BindLocal(const char *name, uint32_t depth)
{
    const auto slot = FuncTab.FindVar(CurrentFuncCode, LocalSlotName(name, localDeclarations_[LowerName(name)]++));
    if (slot != INVALID_VAR_CODE)
        localScopes_.push_back({name, slot, depth});
    return slot;
}

void COMPILER::CloseLocalScopes(uint32_t depth)
{
    while (!localScopes_.empty() && localScopes_.back().depth > depth)
        localScopes_.pop_back();
}

bool COMPILER::CompileLocalDeclarator(SEGMENT_DESC &Segment, uint32_t depth, bool first)
{
    // pass 2, on the name of a local declaration ("int a" or the "b" of "int a, b"). Plain
    // declarations compile exactly as before; arrays sized at run time and array initializers
    // compile here.
    const char *name = Token.GetData();
    if (Token.TypeCast() || !IsIdentifier(name))
    {
        SetError("variable name expected after type");
        return false;
    }
    const std::string var_name = name;
    const uint32_t slot = BindLocal(name, depth);
    if (slot == INVALID_VAR_CODE)
    {
        SetError("Undeclared identifier: %s", var_name.c_str());
        return false;
    }
    declListActive_ = true;
    char *const after_name = Token.GetProgramControl();
    S_TOKEN_TYPE next = Token.Get();
    bool array = false;
    if (next == SQUARE_OPEN_BRACKET)
    {
        char *const after_open = Token.GetProgramControl();
        next = Token.Get();
        if (next == SQUARE_CLOSE_BRACKET)
            array = true; // sized by its initializer (pass 1)
        else if (ConstantArraySize() != 0 && Token.Get() == SQUARE_CLOSE_BRACKET)
        {
            array = true;
            char *const after_close = Token.GetProgramControl();
            const bool initializer = Token.Get() == OP_EQUAL && Token.Get() == BLOCK_IN;
            Token.SetProgramControl(initializer ? after_close : after_name);
            if (!initializer)
            {
                // as before: the size compiles as a meaningless index expression
                if (!first)
                    CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
                return true;
            }
        }
        else
        {
            // sized at run time
            Token.SetProgramControl(after_open);
            if (!CompileExpression(Segment))
                return false;
            if (Token.GetType() != SQUARE_CLOSE_BRACKET)
            {
                SetError("missing ']'");
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, SET_ARRAY_SIZE);
            CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
            array = true;
        }
        char *const after_array = Token.GetProgramControl();
        next = Token.Get();
        if (next == OP_EQUAL && Token.Get() == BLOCK_IN)
            return CompileArrayInitializer(Segment, LOCAL_VARIABLE, slot);
        Token.SetProgramControl(after_array);
        return true;
    }
    if (next == OP_EQUAL || !first)
        CompileToken(Segment, LOCAL_VARIABLE, 1, &slot, sizeof(uint32_t));
    Token.StepBack();
    (void)array;
    return true;
}

bool COMPILER::CompileArrayInitializer(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code)
{
    // after "= {": element i = i-th expression
    for (uint32_t index = 0;; index++)
    {
        if (Token.Get() == BLOCK_OUT)
            return true; // empty list or trailing comma
        Token.StepBack();
        CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
        CompileToken(Segment, STACK_PUSH);
        CompileToken(Segment, NUMBER, 1, &index, sizeof(uint32_t));
        CompileToken(Segment, STACK_POP);
        CompileToken(Segment, LEFTOP_INDEX);
        if (!CompileExpression(Segment))
            return false;
        CompileToken(Segment, MOVE);
        CompileToken(Segment, LEFT_OPERAND);
        CompileToken(Segment, STACK_TOP);
        CompileToken(Segment, STACK_POP_VOID);
        if (Token.GetType() == BLOCK_OUT)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("',' or '}' expected in array initializer");
            return false;
        }
    }
}

bool COMPILER::ScanParameterList(uint32_t func_code, bool bExtern, FuncInfo &declared)
{
    // pass 1, after the name in a function header: registers the arguments up to the matching ')'.
    // declared receives the count, the values of defaulted trailing arguments and "...".
    declared.arguments = 0;
    declared.varargs = false;
    declared.default_args.clear();
    LocalVarInfo lvi;
    if (Token.Get() != OPEN_BRACKET)
    {
        SetError("Invalid function declaration: '(' expected");
        return false;
    }
    for (;;)
    {
        S_TOKEN_TYPE type = Token.Get();
        switch (type)
        {
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            continue;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            continue;
        case COMMA:
            continue;
        case CLOSE_BRACKET:
            return true;
        case DOT:
            if (Token.Get() != DOT || Token.Get() != DOT || declared.varargs)
            {
                SetError("Invalid function declaration: '...' expected");
                return false;
            }
            declared.varargs = true;
            continue;
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_FREF:
            break;
        default:
            SetError("Invalid function declaration: '%s'", Token.GetData() ? Token.GetData() : "");
            return false;
        }
        if (declared.varargs)
        {
            SetError("Invalid function declaration: '...' must be the last parameter");
            return false;
        }
        const char *name = Token.GetData();
        if (Token.TypeCast() || !IsIdentifier(name))
        {
            SetError("Invalid function declaration: parameter name expected");
            return false;
        }
        lvi.type = type;
        lvi.name = name;
        lvi.elements = 1;
        declared.arguments++;
        if (func_code != INVALID_FUNC_CODE)
        {
            FuncTab.AddFuncArg(func_code, lvi, bExtern);
            if (script_cache_mode_ != kCacheDisabled)
                script_cache_.functions.back().arguments.emplace_back(
                    storm::script_cache::FunctionLocalVariable{lvi, bExtern});
        }
        Token.Get();
        if (Token.GetType() == OP_EQUAL)
        {
            DefaultArgument value;
            if (!ScanDefaultArgument(type, value))
                return false;
            declared.default_args.push_back(std::move(value));
            Token.Get();
        }
        else if (!declared.default_args.empty())
        {
            SetError("Invalid function declaration: '%s' follows a parameter with a default value", lvi.name.c_str());
            return false;
        }
        if (Token.GetType() == CLOSE_BRACKET)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("Invalid function declaration: ',' or ')' expected after '%s'", lvi.name.c_str());
            return false;
        }
    }
}

bool COMPILER::ScanDefaultArgument(S_TOKEN_TYPE param_type, DefaultArgument &value)
{
    // a constant: [-]number, string, true/false, a #define, or nullptr for ref/aref parameters
    bool negative = false;
    S_TOKEN_TYPE type = Token.Get();
    if (type == OP_MINUS)
    {
        negative = true;
        type = Token.Get();
    }
    const char *text = Token.GetData() ? Token.GetData() : "";
    DEFINFO di;
    switch (type)
    {
    case NUMBER:
        value.type = NUMBER;
        value.int_value = ParseIntLiteral(text);
        break;
    case FLOAT_NUMBER:
        value.type = FLOAT_NUMBER;
        value.float_value = static_cast<float>(atof(text));
        break;
    case STRING:
        value.type = STRING;
        value.string_value = text;
        break;
    case TRUE_CONST:
    case FALSE_CONST:
        value.type = NUMBER;
        value.int_value = type == TRUE_CONST;
        break;
    case VAR_FREF: // fref(nullptr)
        if (Token.TypeCast() && Token.Get() == OPEN_BRACKET && Token.Get() == UNKNOWN &&
            storm::iEquals(Token.GetData(), "nullptr") && Token.Get() == CLOSE_BRACKET && param_type == VAR_FREF)
        {
            value.type = VAR_FREF;
            return true;
        }
        break;
    case AND: // &Function
        if (Token.Get() == UNKNOWN && Token.GetData() && param_type == VAR_FREF)
        {
            value.type = VAR_FREF;
            value.string_value = Token.GetData();
            return true;
        }
        break;
    case UNKNOWN:
        if (storm::iEquals(text, "nullptr") && !negative &&
            (param_type == VAR_REFERENCE || param_type == VAR_AREFERENCE || param_type == VAR_FREF))
        {
            value.type = param_type == VAR_FREF ? VAR_FREF : VAR_REFERENCE;
            return true;
        }
        if (const auto def_code = DefTab.FindDef(text); def_code != INVALID_DEF_CODE)
        {
            DefTab.GetDef(di, def_code);
            value.type = static_cast<S_TOKEN_TYPE>(di.deftype);
            if (di.deftype == NUMBER)
                std::memcpy(&value.int_value, &di.data4b, sizeof(int32_t));
            else if (di.deftype == FLOAT_NUMBER)
                std::memcpy(&value.float_value, &di.data4b, sizeof(float));
            else if (di.deftype == STRING && !negative)
                value.string_value = reinterpret_cast<const char *>(di.data4b);
            else
                break;
            break;
        }
        [[fallthrough]];
    default:
        SetError("unsupported default value '%s%s'", negative ? "-" : "", text);
        return false;
    }
    if (negative)
    {
        if (value.type == NUMBER)
            value.int_value = -value.int_value;
        else if (value.type == FLOAT_NUMBER)
            value.float_value = -value.float_value;
        else
        {
            SetError("unsupported default value '-%s'", text);
            return false;
        }
    }
    return true;
}

int32_t COMPILER::ParseIntLiteral(const char *text)
{
    // decimal (a leading 0 is not octal) or 0x hexadecimal; an optional u suffix; wraps to 32 bits
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        return static_cast<int32_t>(static_cast<uint32_t>(strtoull(text + 2, nullptr, 16)));
    return static_cast<int32_t>(atoll(text));
}

bool COMPILER::CompileCallArguments(SEGMENT_DESC &Segment, uint32_t &func_args)
{
    // after the '(' of a call: each argument leaves its value on the stack; stops on ')'
    func_args = 0;
    if (Token.Get() == CLOSE_BRACKET)
        return true;
    Token.StepBack();
    for (;;)
    {
        if (!CompileExpression(Segment))
        {
            SetError("invalid function argument");
            return false;
        }
        func_args++;
        if (Token.GetType() == CLOSE_BRACKET)
            return true;
        if (Token.GetType() != COMMA)
        {
            SetError("invalid function argument");
            return false;
        }
    }
}

// The statement form of a call refuses a wrong argument count (`doesnt accept`); a call inside an expression was never
// checked (also in the original engine), and the game's scripts have such calls, so they are reported and compiled as
// before. At run time a script function then takes its last arguments and the call ends with a stack error.
bool COMPILER::CheckCallArguments(uint32_t func_code, uint32_t func_args, bool in_expression)
{
    const FuncInfo *fi = FuncTab.GetFuncInfo(func_code);
    if (fi == nullptr)
    {
        SetError("function not found");
        return false;
    }
    const bool internal = fi->segment_id == INTERNAL_SEGMENT_ID;
    if (fi->native && !internal && fi->segment_id != IMPORTED_SEGMENT_ID && !ReportMissingNative(fi->name))
        return false;
    const auto declared = CallArgumentsDeclared(*fi, func_args, internal && IsIntFuncVarArgsNum(func_code));
    if (!declared)
        return true;
    if (in_expression)
    {
        if (!bDebugExpressionRun)
        {
            FindErrorSource();
            logError_->warn("COMPILE WARNING in {}({}): function '{}' declared with {} arguments is called with {}",
                            DebugSourceFileName, DebugSourceLine + 1, fi->name, *declared, func_args);
        }
        return true;
    }
    SetError(internal ? "function '%s(args:%d)' doesnt accept %d arguments" : "function %s(args:%d) doesnt accept %d arguments",
             fi->name.c_str(), *declared, func_args);
    return false;
}

bool COMPILER::ReportMissingNative(const std::string &name)
{
    if (!missingNativesAreErrors_ && !missingNatives_.insert(name).second)
        return true;
    if (!missingNativesAreErrors_)
    {
        // the host check links no engine library: listed separately, not a script error
        if (diagnosticsSink_)
            diagnosticsSink_->emplace_back("MISSING NATIVE in " + std::string(DebugSourceFileName) + "(" +
                                           std::to_string(DebugSourceLine + 1) + "): " + name);
        return true;
    }
    SetError("native function '%s' is not implemented by the engine", name.c_str());
    return false;
}

bool COMPILER::SkipToTopLevel(uint32_t depth)
{
    // skip the rest of the statement (at top level) or of the function body the error occurred in
    for (;;)
    {
        switch (Token.Get())
        {
        case END_OF_PROGRAMM:
            return false;
        case DEBUG_LINEFEED:
            DebugSourceLine++;
            break;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case DEBUG_FILE_NAME:
            DebugSourceLine = DSL_INI_VALUE;
            if (Token.GetData())
                strcpy_s(DebugSourceFileName, Token.GetData());
            break;
        case BLOCK_IN:
            depth++;
            break;
        case BLOCK_OUT:
            if (depth > 0)
                depth--;
            if (depth == 0)
                return true;
            break;
        case SEPARATOR:
            if (depth == 0)
                return true;
            break;
        default:
            break;
        }
    }
}

S_TOKEN_TYPE COMPILER::SkipAuxiliaryTokens()
{
    S_TOKEN_TYPE Token_type;
    do
    {
        Token_type = Token.Get();
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
        case DEBUG_FILE_NAME:
        case COMMENT:
        case INCLIDE_FILE:
        case WHITESPACE:
            break;
        default:
            return Token_type;
        }
    } while (Token_type != END_OF_PROGRAMM);
    return Token_type;
}

S_TOKEN_TYPE COMPILER::CompileAuxiliaryTokens(SEGMENT_DESC &Segment) //, bool & bFunctionBlock, DWORD & inout)
{
    uint32_t fnsize;
    S_TOKEN_TYPE Token_type;

    do
    {
        Token_type = Token.Get();
        switch (Token_type)
        {
        case DEBUG_LINEFEED:
            if (!bDebugInfo)
                break;
            DebugSourceLine++;
            CompileToken(Segment, DEBUG_LINE_CODE, 1, (char *)&DebugSourceLine, sizeof(uint32_t));
            break;
        case DEBUG_FILE_NAME:
            if (!bDebugInfo)
                break;
            DebugSourceLine = DSL_INI_VALUE;
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid DBG_FILE_NAME");
                return DEBUG_FILE_NAME;
            }
            strcpy_s(DebugSourceFileName, Token.GetData());
            fnsize = strlen(Token.GetData());

            CompileToken(Segment, DEBUG_FILE_NAME, 3, (char *)&DebugSourceLine, sizeof(uint32_t), (char *)&fnsize,
                         sizeof(uint32_t), Token.GetData(), strlen(Token.GetData()));
            break;
        case COMMENT:
            DebugSourceLine += Token.TokenLines();
            break;
        case INCLIDE_FILE:
        case WHITESPACE:
            break;
        default:
            return Token_type;
        }
    } while (Token_type != END_OF_PROGRAMM);
    return Token_type;
}

void COMPILER::CopyOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &srclist, STRINGS_LIST &dstlist, const char *sname)
{
    uint32_t n;
    uint32_t i;
    uint32_t jump_voffset;
    if (sname == nullptr)
    {
        for (n = 0; n < srclist.GetStringsCount(); n++)
        {
            srclist.GetStringData(n, (char *)&jump_voffset);

            i = dstlist.GetStringsCount();
            dstlist.AddString("c");
            dstlist.SetStringData(i, (char *)&jump_voffset);
        }
        return;
    }

    for (n = 0; n < srclist.GetStringsCount(); n++)
    {
        if (strcmp(sname, srclist.GetString(n)) != 0)
            continue;

        srclist.GetStringData(n, (char *)&jump_voffset);

        i = dstlist.GetStringsCount();
        dstlist.AddString("c");
        dstlist.SetStringData(i, (char *)&jump_voffset);
    }
}

void COMPILER::UpdateOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &list, uint32_t offset, const char *sname)
{
    uint32_t n;
    uint32_t jump_voffset;
    if (sname == nullptr)
    {
        for (n = 0; n < list.GetStringsCount(); n++)
        {
            list.GetStringData(n, (char *)&jump_voffset);
            Segment.SetOffset(jump_voffset, offset);
        }
        return;
    }
    for (n = 0; n < list.GetStringsCount(); n++)
    {
        if (strcmp(sname, list.GetString(n)) != 0)
            continue;
        list.GetStringData(n, (char *)&jump_voffset);
        Segment.SetOffset(jump_voffset, offset);
    }
}

bool COMPILER::CompileBlock(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE bound_type,
                            uint32_t continue_jump, uint32_t break_offset, STRINGS_LIST &BreakTable)
{
    S_TOKEN_TYPE Token_type;
    uint32_t awcode;
    uint32_t var_code;
    uint32_t balance_bracket;
    uint32_t balance_block;
    uint32_t jump_offset;
    uint32_t update_offset;
    uint32_t vuint32_t;
    uint32_t n;
    uint32_t unk_code;
    uint32_t func_args;
    STRINGS_LIST BreakUpdateTable;
    BreakUpdateTable.SetStringDataSize(sizeof(uint32_t));
    bool bExtern;
    bool bImport;
    int32_t lvalue;
    uint32_t def_code;
    DEFINFO di;
    uint32_t dwRCode;

    const VarInfo *real_var;
    LocalVarInfo lvi;

    jump_offset = 0xffbadbad;

    balance_block = 0;
    balance_bracket = 0;

    switch (bound_type)
    {
    case CLOSE_BRACKET:
        balance_bracket = 1;
        break;
    case BLOCK_OUT:
        balance_block = 1;
        break;
    }

    bExtern = false;
    bImport = false;
    do
    {
        Token_type = CompileAuxiliaryTokens(Segment);

        switch (Token_type)
        {
        case EVENT_HANDLER:
            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("Invalid event handler");
                return false;
            }

            if (Token.Get() == STRING)
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                strcpy_s(gs, Token.GetData());
            }
            else
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                def_code = DefTab.FindDef(Token.GetData());
                if (def_code != INVALID_DEF_CODE)
                {
                    DefTab.GetDef(di, def_code);
                    if (di.deftype != STRING)
                    {
                        SetError("Invalid event handler");
                        return false;
                    }
                    if (di.data4b == 0)
                    {
                        SetError("Invalid define");
                        return false;
                    }
                    strcpy_s(gs, (char *)di.data4b);
                }
                else
                {
                    SetError("Invalid event handler");
                    return false;
                }
            }
            if (Token.Get() != COMMA)
            {
                SetError("Invalid event handler");
                return false;
            }
            if (Token.Get() == STRING)
            {
                if (Token.GetData() == nullptr)
                {
                    SetError("Invalid event handler");
                    return false;
                }
                SetEventHandler(gs, Token.GetData(), 0, true);
                if (script_cache_mode_ != kCacheDisabled)
                {
                    script_cache_.event_handlers.emplace_back(storm::script_cache::EventHandler{gs, Token.GetData()});
                }
            }
            else
            {
                SetError("Invalid event handler");
                return false;
            }

            if (Token.Get() != CLOSE_BRACKET)
            {
                SetError("Invalid event handler");
                return false;
            }
            break;

        case GOTO_COMMAND:
            // LabelUpdateTable
            Token.Get(); // label name
            DOUBLE_DWORD ddw;
            ddw.dw1 = LabelTable.GetStringCode(LabelKey(CurrentFunctionName(), Token.GetData()).c_str());
            if (ddw.dw1 == INVALID_ORDINAL_NUMBER)
            {
                SetError("goto: no label '%s' in this function", Token.GetData());
                return false;
            }
            n = LabelUpdateTable.GetStringsCount();
            LabelUpdateTable.AddString("j");
            ddw.dw2 = Segment.CodeSize() + 2;
            LabelUpdateTable.SetStringData(n, (char *)&ddw);
            vuint32_t = 0xffffffff;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            break;
        case BREAK_COMMAND:
            if (break_offset == INVALID_OFFSET)
            {
                SetError("invalid break");
                return false;
            }
            vuint32_t = Segment.CodeSize() + 2;
            n = BreakTable.GetStringsCount();
            BreakTable.AddString("b");
            BreakTable.SetStringData(n, (char *)&vuint32_t);
            vuint32_t = INVALID_OFFSET;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            if (bound_type == BREAK_COMMAND)
            {
                return true;
            }
            break;
        case CONTINUE_COMMAND:
            if (continue_jump == INVALID_OFFSET)
            {
                SetError("invalid continue");
                return false;
            }
            vuint32_t = Segment.CodeSize() + 2;
            n = BreakTable.GetStringsCount();
            BreakTable.AddString("c");
            BreakTable.SetStringData(n, (char *)&vuint32_t);
            vuint32_t = INVALID_OFFSET;
            CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
            break;
        case SEPARATOR:
            CompileToken(Segment, Token_type);
            declListActive_ = declExpectName_ = false;
            if (bound_type == SEPARATOR)
            {
                return true;
            }
            break;
        case OPEN_BRACKET:
            balance_bracket++;
            CompileToken(Segment, Token_type);
            break;
        case CLOSE_BRACKET:
            balance_bracket--;
            CompileToken(Segment, Token_type);
            if (bound_type == CLOSE_BRACKET)
            {
                if (balance_bracket == 0)
                {
                    return true;
                }
            }
            break;
        case BLOCK_IN:
            inout++;
            balance_block++;
            break;
        case BLOCK_OUT:
            if (bound_type == BREAK_COMMAND && balance_block == 0)
            {
                Token.StepBack(); // the switch's '}' ends the last case (no break): the switch closes it
                return true;
            }

            if (inout == 0)
            {
                SetError("syntax error");
                return false;
            }
            inout--;
            CloseLocalScopes(inout);
            declListActive_ = declExpectName_ = false;

            if (balance_block == 0)
            {
                switch (bound_type)
                {
                case CLOSE_BRACKET:
                    SetError("missed ')'");
                    break;
                case BLOCK_OUT:
                    SetError("missed '}'");
                    break;
                default:
                    SetError("syntax error");
                    break;
                }
                return false;
            }
            balance_block--;
            if (inout == 0)
            {
                CompileToken(Segment, FUNCTION_RETURN_VOID);
                bFunctionBlock = false; // sign if we are out of function
                CurrentFuncCode = INVALID_FUNC_CODE; // no locals between functions
                break;
            }
            if (bound_type == BLOCK_OUT)
            {
                if (balance_block == 0)
                {
                    return true;
                }
            }
            break;
        case IF_BLOCK:

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            CompileExpression(Segment);

            if (Token.GetType() != CLOSE_BRACKET)
            {
                SetError("missed ')'");
                return false;
            }

            CompileToken(Segment, STACK_READ);
            CompileToken(Segment, EX);

            update_offset = Segment.CodeSize() + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, break_offset, BreakTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, continue_jump, break_offset, BreakTable))
                    return false;
            }
            Segment.SetOffset(update_offset, Segment.CodeSize());

            CompileToken(Segment, POP_EXPRESULT); // pop

            // the unbraced body of a statement ends with this if, or with its else
            if (bound_type == IF_BLOCK || (bound_type == SEPARATOR && PeekNextToken() != ELSE_BLOCK))
            {
                return true;
            }

            break;
        case ELSE_BLOCK:
            update_offset = Segment.CodeSize() + 2;
            CompileToken(Segment, JUMP_NZ, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, break_offset, BreakTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, Token_type == IF_BLOCK ? IF_BLOCK : SEPARATOR,
                                  continue_jump, break_offset, BreakTable))
                    return false;
            }
            Segment.SetOffset(update_offset, Segment.CodeSize());
            // an else if chain goes on with the next else; any other else completes its if
            if (bound_type == SEPARATOR && !(Token_type == IF_BLOCK && PeekNextToken() == ELSE_BLOCK))
            {
                return true;
            }
            break;

        case WHILE_BLOCK:
            BreakUpdateTable.Release();
            jump_offset = Segment.CodeSize();


            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);

            if (Token.GetType() != CLOSE_BRACKET)
            {
                SetError("missed ')'");
                return false;
            }


            update_offset = Segment.CodeSize() + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, jump_offset, 6, BreakUpdateTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, jump_offset, 6, BreakUpdateTable))
                    return false;
                // **** ?
            }
            CompileToken(Segment, JUMP, 1, (char *)&jump_offset, sizeof(uint32_t));
            Segment.SetOffset(update_offset, Segment.CodeSize());
            UpdateOffsets(Segment, BreakUpdateTable, Segment.CodeSize(), "b");
            UpdateOffsets(Segment, BreakUpdateTable, jump_offset, "c");
            BreakUpdateTable.Release();
            if (bound_type == SEPARATOR)
                return true; // the unbraced body of a statement ends with this statement
            break;
        case FOR_BLOCK:
            BreakUpdateTable.Release();

            // '('
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            {
                char *const at = Token.GetProgramControl();
                const uint32_t line = DebugSourceLine;
                S_TOKEN_TYPE loop_type = UNKNOWN;
                const bool range = PeekRangeFor(loop_type);
                Token.SetProgramControl(at);
                DebugSourceLine = line;
                if (range)
                {
                    if (!CompileRangeFor(Segment, bFunctionBlock, inout, loop_type, BreakUpdateTable))
                        return false;
                    BreakUpdateTable.Release();
                    if (bound_type == SEPARATOR)
                        return true; // the unbraced body of a statement ends with this statement
                    break;
                }
            }

            // var = initial_value
            if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, INVALID_OFFSET, INVALID_OFFSET, BreakTable))
                return false;

            jump_offset = Segment.CodeSize();

            // condition
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);

            // jump if zero
            update_offset = Segment.CodeSize() + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));

            S_TOKEN_TYPE sttResult; // var for inc/dec op
            S_TOKEN_TYPE forOp;
            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            sttResult = DetectUnknown(var_code);
            if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            forOp = Token.Get();
            if (!(forOp == OP_INC || forOp == OP_DEC))
            {
                SetError("invalid 'for' syntax");
                return false;
            }
            if (CompileAuxiliaryTokens(Segment) != CLOSE_BRACKET)
            {
                SetError("invalid 'for' syntax");
                return false;
            }

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                if (!CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, jump_offset, 6, BreakUpdateTable))
                    return false;
            }
            else
            {
                Token.StepBack();
                if (!CompileBlock(Segment, bFunctionBlock, inout, SEPARATOR, jump_offset, 6, BreakUpdateTable))
                    return false;
                // **** ?
            }

            uint32_t forcont_offset;
            forcont_offset = Segment.CodeSize();
            CompileToken(Segment, sttResult, 1, (char *)&var_code, sizeof(uint32_t));
            CompileToken(Segment, forOp);
            CompileToken(Segment, JUMP, 1, (char *)&jump_offset, sizeof(uint32_t));
            Segment.SetOffset(update_offset, Segment.CodeSize());
            UpdateOffsets(Segment, BreakUpdateTable, Segment.CodeSize(), "b");
            UpdateOffsets(Segment, BreakUpdateTable, forcont_offset, "c");
            BreakUpdateTable.Release();
            if (bound_type == SEPARATOR)
                return true; // the unbraced body of a statement ends with this statement
            break;
        case SWITCH_COMMAND:
            BreakUpdateTable.Release();
            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }


            CompileExpression(Segment);

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == BLOCK_IN)
            {
                inout++;
                std::vector<uint32_t> fallthrough; // jumps of cases without break into the next case
                auto *const outer = switchFallthrough_;
                switchFallthrough_ = &fallthrough;
                const bool compiled_switch =
                    CompileBlock(Segment, bFunctionBlock, inout, BLOCK_OUT, continue_jump, 6, BreakUpdateTable);
                switchFallthrough_ = outer;
                if (!compiled_switch)
                    return false;
            }
            else
            {
                SetError("missed '{'");
                return false;
            }
            UpdateOffsets(Segment, BreakUpdateTable, Segment.CodeSize(), "b");
            CopyOffsets(Segment, BreakUpdateTable, BreakTable, "c");
            // UpdateOffsets(Segment,BreakUpdateTable,continue_jump,"c");    // -> ***
            CompileToken(Segment, POP_VOID);
            BreakUpdateTable.Release();
            if (bound_type == SEPARATOR)
                return true; // the unbraced body of a statement ends with this statement
            break;
        case CASE_COMMAND:
            if (bound_type == BREAK_COMMAND && balance_block == 0)
            {
                // the previous case has no break: jump over this comparison into this case's code
                if (switchFallthrough_)
                {
                    switchFallthrough_->push_back(Segment.CodeSize() + 2);
                    vuint32_t = INVALID_OFFSET;
                    CompileToken(Segment, JUMP, 1, (char *)&vuint32_t, sizeof(uint32_t));
                }
                Token.StepBack();
                return true;
            }
            CompileExpression(Segment);
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, EX);


            if (Token.GetType() != LABEL)
            {
                SetError("missed ':'");
                return false;
            }

            CompileToken(Segment, STACK_COMPARE);

            update_offset = Segment.CodeSize() + 2;
            CompileToken(Segment, JUMP_Z, 1, (char *)&jump_offset, sizeof(uint32_t));
            if (switchFallthrough_)
            {
                for (const auto offset : *switchFallthrough_)
                    Segment.SetOffset(offset, Segment.CodeSize());
                switchFallthrough_->clear();
            }

            // ??? why INVALID_OFFSET on continue_jump here? dont remember; anyway now change to normal
            if (!CompileBlock(Segment, bFunctionBlock, inout, BREAK_COMMAND, continue_jump, 0, BreakTable))
                return false;

            Segment.SetOffset(update_offset, Segment.CodeSize());
            break;
        case INVALID_TOKEN:
            SetError("bad token");
            return false;
            break;


        case CALL:

            S_TOKEN_TYPE sttVar;
            uint32_t dwCallVarCode;

            Token_type = CompileAuxiliaryTokens(Segment);
            if (Token_type == OPEN_BRACKET)
            {
                // call (expression)(args): the expression's text names the function
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != CLOSE_BRACKET || CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
                {
                    SetError("call (name)(arguments) expected");
                    return false;
                }
                if (!CompileCallArguments(Segment, func_args))
                    return false;
                CompileToken(Segment, CALL_NAMED);
                CompileToken(Segment, ARGS_NUM, 1, &func_args, sizeof(uint32_t));
                CompileToken(Segment, POP_NZ);
                break;
            }
            if (Token_type != UNKNOWN)
            {
                SetError("invalid dynamic call");
                return false;
            }
            sttVar = DetectUnknown(dwCallVarCode);
            if (!(sttVar == VARIABLE || sttVar == LOCAL_VARIABLE))
            {
                SetError("invalid dynamic call");
                return false;
            }

            if (CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
            {
                SetError("missing '('");
                return false;
            }
            if (!CompileCallArguments(Segment, func_args))
                return false;

            CompileToken(Segment, CALL);
            CompileToken(Segment, Token_type, 1, (char *)&dwCallVarCode, sizeof(uint32_t));
            CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));
            CompileToken(Segment, POP_NZ); // pop return value if function returned value

            break;

        case UNKNOWN: {
            uint32_t func_code;
            uint32_t func_args;
            if (declExpectName_)
            {
                declExpectName_ = false;
                if (!CompileLocalDeclarator(Segment, inout, false))
                    return false;
                break;
            }
            if (inout == 0 && Token.GetData() && storm::iEquals(Token.GetData(), "nullptr"))
                break; // "ref g = nullptr;": global initializers are not code
            if (Token.Get() == LABEL)
            {
                // "name:" starts a statement: a goto label (registered by pass 1) or a switch's "default:"
                // (the cases before it jump past it when one matched)
                Token.StepBack();
                Token.StepBack();
                Token.Get();
                if (!storm::iEquals(Token.GetData(), "default"))
                {
                    const auto lab_code =
                        LabelTable.GetStringCode(LabelKey(CurrentFunctionName(), Token.GetData()).c_str());
                    if (lab_code == INVALID_ORDINAL_NUMBER)
                    {
                        SetError("misplaced label '%s'", Token.GetData());
                        return false;
                    }
                    uint32_t label_offset = Segment.CodeSize();
                    LabelTable.SetStringData(lab_code, &label_offset);
                }
                Token.Get(); // ':'
                break;
            }
            Token.StepBack(); // back to the identifier (restores its data)
            Token.StepBack();
            Token.Get();
            if (IsAttributeCall())
            {
                // object.attr(args); calls the function the attribute names
                Token.StepBack();
                if (!CompileExpression(Segment))
                    return false;
                CompileToken(Segment, POP_NZ); // drop a returned value
                Token.StepBack();
                break;
            }
            if (Token.GetData() && IsAttributeIntrinsic(Token.GetData()))
            {
                // deleteattr(&a.b); touchattr(a.b); as statements
                if (!CompileAttributeIntrinsic(Segment))
                    return false;
                CompileToken(Segment, STACK_POP_VOID);
                // the intrinsic read the ';' as an expression would: give it back, so that it ends this statement
                // (an unbraced if/else/loop body is one statement)
                Token.StepBack();
                break;
            }
            if (S_TOKEN_TYPE call_type = DetectCallThrough(); call_type != UNKNOWN)
            {
                // object->Method(...) or fref_variable(...) as a statement
                if (!CompileCallThrough(Segment, call_type))
                    return false;
                CompileToken(Segment, POP_NZ); // drop a returned value
                break;
            }

            if (DetectUnknown(func_code) == CALL_FUNCTION)
            {
                if (CompileAuxiliaryTokens(Segment) != OPEN_BRACKET)
                {
                    SetError("missing '('");
                    return false;
                }
                if (!CompileCallArguments(Segment, func_args) || !CheckCallArguments(func_code, func_args))
                    return false;

                CompileToken(Segment, CALL_FUNCTION, 1, (char *)&func_code, sizeof(uint32_t));
                CompileToken(Segment, ARGS_NUM, 1, (char *)&func_args, sizeof(uint32_t));
                if (FuncTab.GetFuncInfo(func_code)->return_type != TVOID)
                    CompileToken(Segment, STACK_POP_VOID);
            }
            else if (!CompileUnknown(Segment))
                return false;
        }
        break;

        case EXTERN:
            Token_type = Token.Get();
            bExtern = true;
        case IMPORT:
            if (!bExtern)
            {
                Token_type = Token.Get();
                bImport = true;
            }
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
        case VAR_INTEGER: // create global variable
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_FREF:
        case TVOID:
            if (inout > 0)
            {
                // local variable declaration
                if (!CompileLocalDeclarator(Segment, inout, true))
                    return false;
                break;
            }
            if (Token.GetData() && strcmp(Token.GetData(), "[") == 0)
            {
                Token.Get(); // "type []Name(": a function returning an array (checked by pass 1)
                Token.Get();
            }
            Token_type = DetectUnknown(unk_code);
            if (Token_type == CALL_FUNCTION)
            {
                // function declaration

                bFunctionBlock = true;

                CurrentFuncCode = FuncTab.FindFunc(Token.GetData());
                localScopes_.clear();
                localDeclarations_.clear();
                if (!bExtern)
                    if (!bImport)
                    {
                        if (!FuncTab.SetFuncOffset(Token.GetData(), Segment.CodeSize()))
                        {
                            SetError("Invalid function name: %s", Token.GetData());
                            return false;
                        }

                        auto func_name = Token.GetData();
                        auto cmp = [&func_name](const auto &func) { return storm::iEquals(func.info.name, func_name); };
                        auto it = std::ranges::find_if(script_cache_.functions, cmp);
                        if (it != script_cache_.functions.end())
                        {
                            it->info.offset = Segment.CodeSize();
                        }
                    }
                bExtern = false;
                bImport = false;
                // skip function arguments list in function declaration
                if (Token.Get() != OPEN_BRACKET)
                {
                    SetError("Invalid syntax");
                    return false;
                }

                // pass 1 checked the parameter list (ScanParameterList)
                for (uint32_t depth = 0;;)
                {
                    Token_type = Token.Get();
                    if (Token_type == END_OF_PROGRAMM)
                        break;
                    if (Token_type == OPEN_BRACKET)
                        depth++;
                    else if (Token_type == CLOSE_BRACKET && depth-- == 0)
                        break;
                }
            }

            // ... skip var or func declaration
            break;

        case FUNCTION_RETURN:
        case FUNCTION_RETURN_VOID:
            Token_type = Token.Get();
            if (Token_type == SEPARATOR)
                CompileToken(Segment, FUNCTION_RETURN_VOID);
            else
            {
                Token.StepBack();
                CompileExpression(Segment); // this will cause function result be pushed into stack
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, EX);
                CompileToken(Segment, FUNCTION_RETURN);
            }
            Token.StepBack(); // ???

            break;


        case OP_SIZEOF: {
            // @array = size
            uint32_t var_code;
            if (Token.Get() != UNKNOWN)
            {
                SetError("array expected after '@'");
                return false;
            }
            const S_TOKEN_TYPE var_type = DetectUnknown(var_code);
            if (!(var_type == VARIABLE || var_type == LOCAL_VARIABLE) || Token.Get() != OP_EQUAL)
            {
                SetError("'@array = size' expected");
                return false;
            }
            if (!CompileExpression(Segment))
                return false;
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, BX);
            CompileToken(Segment, RESIZE_ARRAY);
            CompileToken(Segment, var_type, 1, &var_code, sizeof(uint32_t));
            Token.StepBack();
            break;
        }
        case OP_BOOL_NEG:
        case OP_BOOL_EQUAL:
        case OP_GREATER:
        case OP_GREATER_OR_EQUAL:
        case OP_LESSER:
        case OP_LESSER_OR_EQUAL:
        case OP_NOT_EQUAL:
        case OP_MINUS:
        case OP_PLUS:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_POWER:
        case OP_MODUL:
        case OP_INC:
        case OP_DEC:
        case OP_BOOL_AND:
        case OP_BOOL_OR:
        case COMMA:
        case AND:
        case END_OF_PROGRAMM:
            CompileToken(Segment, Token_type);
            if (Token_type == COMMA && declListActive_)
                declExpectName_ = true; // the next declarator of "int a, b"
            break;


        case OP_INCADD:
        case OP_DECADD:
        case OP_MULTIPLYEQ:
        case OP_DIVIDEEQ:
        case OP_OREQ:
        case OP_ANDEQ:
            CompileExpression(Segment);
            CompileToken(Segment, Token_type);
            CompileToken(Segment, STACK_POP_VOID);
            if (Token.GetType() == bound_type)
            {
                Token.StepBack();
            }

            break;

        case OP_COLON_EQUAL: // CL: assignment that keeps the value's type; attributes hold text anyway
        case OP_EQUAL:
            if (inout == 0 && !bDebugExpressionRun)
                break; // skip global vars initialization
            CompileExpression(Segment);

            CompileToken(Segment, MOVE);
            CompileToken(Segment, LEFT_OPERAND);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, STACK_POP_VOID);
            Token.StepBack();

            break;


        case SQUARE_OPEN_BRACKET:
            if (Token.Get() == SQUARE_CLOSE_BRACKET)
                break; // "type name[]" of a global array sized at run time
            Token.StepBack();
            if (!CompileExpression(Segment))
            {
                return false;
            }
            CompileToken(Segment, STACK_POP);
            CompileToken(Segment, LEFTOP_INDEX);
            // a[i][j] = ...: an element of the array a[i] refers to
            while (Token.Get() == SQUARE_OPEN_BRACKET)
            {
                if (!CompileExpression(Segment))
                    return false;
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);
                CompileToken(Segment, LEFTOP_SUBINDEX);
            }
            Token.StepBack();

            break;
        case SQUARE_CLOSE_BRACKET:
            SetError("']' without matching '['");
            break;

        case MAKEREF_COMMAND:
            uint32_t dwRCode1, dwRCode2;
            S_TOKEN_TYPE sttVarType1, sttVarType2;

            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 1st operand");
                return false;
            }

            sttVarType1 = DetectUnknown(dwRCode1);

            if (!(sttVarType1 == VARIABLE || sttVarType1 == LOCAL_VARIABLE))
            {
                SetError("invalid 1st operand");
                return false;
            }


            if (Token.Get() != COMMA)
            {
                SetError("missed ','");
                return false;
            }

            if (Token.Get() != UNKNOWN)
            {
                SetError("invalid 1st operand");
                return false;
            }

            sttVarType2 = DetectUnknown(dwRCode2);

            if (!(sttVarType2 == VARIABLE || sttVarType2 == LOCAL_VARIABLE))
            {
                SetError("invalid 2st operand");
                return false;
            }

            if (Token.Get() == SQUARE_OPEN_BRACKET)
            {
                CompileExpression(Segment);

                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                CompileToken(Segment, SETREF_BXINDEX);
                CompileToken(Segment, sttVarType1, 1, (char *)&dwRCode1, sizeof(uint32_t));
                CompileToken(Segment, sttVarType2, 1, (char *)&dwRCode2, sizeof(uint32_t));

                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missed ']'");
                    return false;
                }
                if (Token.Get() != CLOSE_BRACKET)
                {
                    SetError("missed ')'");
                    return false;
                }
            }
            else
            {
                if (Token.GetType() != CLOSE_BRACKET)
                {
                    SetError("missed ')'");
                    return false;
                }
                CompileToken(Segment, SETREF);
                CompileToken(Segment, sttVarType1, 1, (char *)&dwRCode1, sizeof(uint32_t));
                CompileToken(Segment, sttVarType2, 1, (char *)&dwRCode2, sizeof(uint32_t));
            }


            break;


        case MAKEAREF_COMMAND:

            // CompileToken(Segment,Token_type);    // debug
            S_TOKEN_TYPE sttVarType;

            if (Token.Get() != OPEN_BRACKET)
            {
                SetError("missed '('");
                return false;
            }
            Token_type = Token.Get();
            if (Token_type != UNKNOWN)
            {
                SetError("invalid 1st argument");
                return false;
            }
            Token_type = DetectUnknown(dwRCode);
            if (!(Token_type == VARIABLE || Token_type == LOCAL_VARIABLE))
            {
                SetError("invalid 1st argument");
                return false;
            }

            if (Token_type == VARIABLE)
            {
                real_var = VarTab.GetVarX(dwRCode);
                if (real_var == nullptr)
                {
                    SetError("Invalid var code");
                    return false;
                }

                if (!(real_var->type == VAR_AREFERENCE || real_var->type == VAR_REFERENCE))
                {
                    SetError("not aref or ref");
                    return false;
                }
            }
            else
            {
                FuncTab.GetVar(lvi, CurrentFuncCode, dwRCode);
                if (!(lvi.type == VAR_AREFERENCE || lvi.type == VAR_REFERENCE))
                {
                    SetError("not aref or ref");
                    return false;
                }
            }

            CompileToken(Segment, STACK_ALLOC);
            CompileToken(Segment, SETREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, Token_type, 1, (char *)&dwRCode, sizeof(uint32_t));

            if (Token.Get() != COMMA)
            {
                SetError("missed ','");
                return false;
            }

            sttVarType = Token.Get();
            if (sttVarType != UNKNOWN)
            {
                SetError("invalid 2d argument");
                return false;
            }
            sttVarType = DetectUnknown(dwRCode);
            if (!(sttVarType == VARIABLE || sttVarType == LOCAL_VARIABLE))
            {
                SetError("invalid 2d argument");
                return false;
            }

            Token_type = Token.Get();
            if (Token_type == SQUARE_OPEN_BRACKET)
            {
                CompileExpression(Segment);
                if (Token.GetType() != SQUARE_CLOSE_BRACKET)
                {
                    SetError("missing ']'");
                    return false;
                }
                CompileToken(Segment, STACK_POP);
                CompileToken(Segment, BX);

                // move ap to AP register
                CompileToken(Segment, MOVEAP_BXINDEX);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
                Token_type = Token.Get();

                CompileToken(Segment, PUSH_OBJID_BXINDEX);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
            }
            else
            {
                // bove atributes root to AP
                CompileToken(Segment, MOVEAP);
                CompileToken(Segment, AP);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));

                CompileToken(Segment, PUSH_OBJID);
                CompileToken(Segment, sttVarType, 1, (char *)&dwRCode, sizeof(uint32_t));
            }

            if (Token_type != DOT)
            {
                // set attribute pointer to referenced var
                CompileToken(Segment, SETAREF);
                CompileToken(Segment, STACK_TOP);
                CompileToken(Segment, AP);
                CompileToken(Segment, STACK_POP_VOID);
                break;
            }

            // advance ap

            sttResult = Token.Get();
            uint32_t dwAWCode;
            uint32_t dwVarCode;
            do
            {
                if (Token.GetType() == OPEN_BRACKET && !IsAccessVariable())
                {
                    // .(expression): the attribute named by its text, created when missing as the words are
                    if (!CompileAccessExpression(Segment, kAccessCreate))
                        return false;
                }
                else if (Token.GetType() == OPEN_BRACKET)
                {
                    // access var
                    sttResult = Token.Get();
                    if (sttResult != UNKNOWN)
                    {
                        SetError("Invalid access var syntax");
                        return false;
                    }
                    sttResult = DetectUnknown(dwVarCode);
                    if (!(sttResult == VARIABLE || sttResult == LOCAL_VARIABLE))
                    {
                        SetError("not variable: %s", Token.GetData());
                        return false;
                    }
                    CompileToken(Segment, VERIFY_AP);
                    CompileToken(Segment, sttResult, 1, &dwVarCode, sizeof(uint32_t));
                    if (Token.Get() != CLOSE_BRACKET)
                    {
                        SetError("missing ')'");
                        return false;
                    }
                }
                else
                {
                    // access word
                    if (Token.GetData() == nullptr)
                    {
                        SetError("Invalid access string");
                        return false;
                    }
                    Token.LowCase();
                    dwAWCode = SCodec.Convert(Token.GetData());
                    CompileToken(Segment, VERIFY_AP);
                    CompileToken(Segment, ACCESS_WORD_CODE, 1, &dwAWCode, sizeof(uint32_t));
                }
                sttResult = Token.Get();
                if (sttResult == DOT)
                    Token.Get(); // get next token (after dot)
            } while (sttResult == DOT);

            CompileToken(Segment, SETAREF);
            CompileToken(Segment, STACK_TOP);
            CompileToken(Segment, AP);
            CompileToken(Segment, STACK_POP_VOID);
            break;

        case DOT:
            Token_type = Token.Get();
            if (Token_type == OPEN_BRACKET && !IsAccessVariable())
            {
                // .(expression) on the left side: the expression's text names the attribute
                if (!CompileExpression(Segment))
                    return false;
                if (Token.GetType() != CLOSE_BRACKET)
                {
                    SetError("no mathcing ')'");
                    return false;
                }
                CompileToken(Segment, ACCESS_EXPR);
                break;
            }
            if (Token_type == OPEN_BRACKET)
            {
                // access word inside variable
                Token.Get(); // variable name
                var_code = FindLocal(Token.GetData());
                if (var_code != INVALID_VAR_CODE)
                {
                    Token.Get();
                    if (Token.GetType() != CLOSE_BRACKET)
                    {
                        SetError("no mathcing ')'");
                        return false;
                    }
                    CompileToken(Segment, ACCESS_VAR);
                    CompileToken(Segment, LOCAL_VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
                    break;
                }

                var_code = VarTab.FindVar(Token.GetData());
                if (var_code != INVALID_VAR_CODE)
                {
                    Token.Get();
                    if (Token.GetType() != CLOSE_BRACKET)
                    {
                        SetError("no mathcing ')'");
                        return false;
                    }
                    CompileToken(Segment, ACCESS_VAR);
                    CompileToken(Segment, VARIABLE, 1, (char *)&var_code, sizeof(uint32_t));
                    break;
                }

                SetError("Invalid access string");
                return false;
            }
            // else its access word
            if (Token.GetData() == nullptr)
            {
                SetError("Invalid access string");
                return false;
            }

            Token.LowCase();
            awcode = SCodec.Convert(Token.GetData());
            CompileToken(Segment, ACCESS_WORD_CODE, 1, &awcode, sizeof(uint32_t));
            break;

        case TRUE_CONST:
            lvalue = 1;
            CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
            break;
        case FALSE_CONST:
            lvalue = 0;
            CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
            break;

        case NUMBER:
            CompileNumber(Segment);
            break;
        case FLOAT_NUMBER:
            CompileFloatNumber(Segment);
            break;
        case STRING:
            CompileString(Segment);
            break;

        case LABEL: // register label

            break;
        case DEFINE_COMMAND: // create define

            break;

        default:

            break;
        }
    } while (Token_type != END_OF_PROGRAMM);

    return true;
}

bool COMPILER::BC_Jump(SEGMENT_DESC &Segment, uint32_t offset)
{
    if (offset >= Segment.CodeSize())
    {
        SetError("invalid jump");
        return false;
    }
    InstructionPointer = offset;
    return true;
}

// The expression register as the integer a conditional jump tests. A register that does not convert leaves `value`
// as it was (the jump then follows the previous condition, as it always did).
inline void COMPILER::ReadCondition(int32_t &value)
{
    if (ExpressionResult.Data_type == VAR_INTEGER && !ExpressionResult.bArray)
    {
        value = ExpressionResult.lValue;
        return;
    }
    ExpressionResult.Convert(VAR_INTEGER);
    ExpressionResult.Get(value);
}

// Reads the token at InstructionPointer: its type, where its data starts (ip, TLR_DataOffset) and the data size
// (one byte, or 0xff and four bytes); InstructionPointer moves to the next token.
S_TOKEN_TYPE COMPILER::BC_TokenGet(uint32_t &ip, uint32_t &token_data_size)
{
    const char *const code = pRunCodeBase;
    uint32_t at = InstructionPointer;
    const auto type = static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(code[at])); // more than 127 token types
    uint32_t size = static_cast<uint8_t>(code[at + 1]);
    at += 2;
    if (size == 0xff)
    {
        memcpy(&size, &code[at], sizeof(uint32_t));
        at += sizeof(uint32_t);
    }
    TokenLastReadResult = type;
    ip = at;
    TLR_DataOffset = at;
    token_data_size = size;
    InstructionPointer = at + size;
    return type;
}

// For the callers that read the token's data through TLR_DataOffset.
S_TOKEN_TYPE COMPILER::BC_TokenGet()
{
    uint32_t ip;
    uint32_t token_data_size;
    return BC_TokenGet(ip, token_data_size);
}

S_TOKEN_TYPE COMPILER::NextTokenType()
{
    return static_cast<S_TOKEN_TYPE>(static_cast<uint8_t>(pRunCodeBase[InstructionPointer]));
}

bool COMPILER::BC_CallFunction(uint32_t func_code, uint32_t &ip, DATA *&pVResult)
{
    CompilerStage = CS_RUNTIME;

    const FuncInfo *const call_pfi = FuncTab.GetFuncPtr(func_code);
    if (call_pfi == nullptr)
    {
        SetError("Invalid function call");
        return false;
    }
    // no deep copy: the table entry address is stable (see FuncTable::funcs_)
    const FuncInfo &call_fi = *call_pfi;
    const uint32_t call_segment_id = call_fi.segment_id;
    const S_TOKEN_TYPE call_return_type = call_fi.return_type;
    const SIMPORTFUNC call_imported_func = call_fi.imported_func;

    // TODO: only do if stack debug if enabled (should be runtime configurable)
    // push function details to call stack
    storm::ringbuffer_stack_push_guard push_guard(callStack_);

    // number f arguments pushed into stack for this function call
    if (BC_TokenGet() != ARGS_NUM)
    {
        push_guard.push(std::make_tuple(call_fi.decl_file_name.c_str(), call_fi.decl_line, call_fi.name.c_str(), kNoArguments, 0U));
        SetError("missing args_num token");
        return false;
    }
    const uint32_t arguments = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]);

    const uint32_t check_sp = SStack.GetDataNum() - arguments;
    push_guard.push(std::make_tuple(call_fi.decl_file_name.c_str(), call_fi.decl_line, call_fi.name.c_str(), check_sp, arguments));

    // the caller's position, restored after the callee has run
    const uint32_t mem_InstructionPointer = InstructionPointer;
    const uint32_t mem_ip = ip;
    FuncFrame *const mem_pfi = pRun_fi;
    const char *const mem_codebase = pRunCodeBase;
    const auto restore = [&] {
        pRun_fi = mem_pfi;
        InstructionPointer = mem_InstructionPointer;
        ip = mem_ip;
        if (pRun_fi)
            RunningSegmentID = pRun_fi->segment_id;
        pRunCodeBase = mem_codebase;
    };

    if (call_segment_id == INTERNAL_SEGMENT_ID)
    {
        BC_CallIntFunction(func_code, pVResult, arguments);
    }
    else if (call_segment_id == IMPORTED_SEGMENT_ID)
    {
        // a native reads its arguments after it pushes its result into the same stack slot (the
        // value's type is reset by then): give numbers and text the declared parameter types now
        // (CL passes ints to float parameters, e.g. Degree2Radian(14))
        const uint32_t first = SStack.GetDataNum() - arguments;
        for (uint32_t n = 0; n < arguments && n < call_fi.local_vars.size(); n++)
        {
            const S_TOKEN_TYPE declared = call_fi.local_vars[n].type;
            DATA *argument = SStack.Read(first, n);
            const S_TOKEN_TYPE type = argument->GetType();
            auto scalar = [](S_TOKEN_TYPE t) { return t == VAR_INTEGER || t == VAR_FLOAT || t == VAR_STRING; };
            if (type != declared && scalar(type) && scalar(declared) && !argument->IsArray())
                argument->Convert(declared);
        }
        pVResult = nullptr;
        const uint32_t nResult = call_imported_func(&SStack);
        if (nResult == IFUNCRESULT_OK)
        {
            if (call_return_type != TVOID)
            {
                pVResult = SStack.Read();
            }
        }
    }
    else
    {
        BC_Execute(func_code, pVResult, nullptr, arguments);
    }

    restore();

    if (abort_execution_)
    {
        // A native function aborted the running scripts (AbortExecution): no stack check, the caller's frame
        // returns too and the outermost one drops what the aborted frames left.
        return false;
    }

    if (check_sp != SStack.GetDataNum() - (pVResult ? 1 : 0))
    {
        if (call_segment_id != INTERNAL_SEGMENT_ID && call_segment_id != IMPORTED_SEGMENT_ID &&
            arguments != call_fi.arguments)
            SetError("function '%s' stack error: declared with %u arguments, called with %u", call_fi.name.c_str(),
                     call_fi.arguments, arguments);
        else
            SetError("function '%s' stack error", call_fi.name.c_str());
        SStack.DropStrayValues(check_sp, pVResult);
    }

    return true;
}

std::optional<int32_t> COMPILER::FindIntegerDefine(const char *name)
{
    DEFINFO di;
    const uint32_t def_code = DefTab.FindDef(name);
    if (def_code == INVALID_DEF_CODE || !DefTab.GetDef(di, def_code) || di.deftype != NUMBER)
        return std::nullopt;
    int32_t value;
    memcpy(&value, &di.data4b, sizeof(value));
    return value;
}

bool COMPILER::BC_Execute(uint32_t function_code, DATA *&pVReturnResult, const char *pDbgExpSource,
                          uint32_t passed_args)
{
    uint32_t segment_index;
    uint32_t token_data_size;
    uint32_t ip;
    uint32_t n;
    uint32_t jump_offset;
    uint32_t func_code;
    uint32_t nLeftOperandCode;
    uint32_t bLeftOperandType;
    int32_t nLeftOperandIndex;
    S_TOKEN_TYPE Token_type;
    // fi: stable table entry (or an empty one for debug expressions), never copied.
    // frame: this invocation's scalar snapshot, including its own stack offset.
    static const FuncInfo empty_fi;
    const FuncInfo *pfi = &empty_fi;
    FuncFrame frame(empty_fi);
    const VarInfo *real_var;
    DATA *pV;
    DATA *pVResult;
    //    DATA   ExpressionResult;    // while compile expression not ready, each function have its own register
    const char *pCodeBase;
    int32_t lvalue;
    S_TOKEN_TYPE vtype;
    DATA *pVV;
    uint32_t var_code;
    const char *pAccess_string;
    DATA *pVar;
    // AbortExecution unwinds every frame (CALL_FUNCTION returns false); the outermost one drops the stack values
    // the aborted frames left and clears the request.
    struct ExecuteDepth
    {
        COMPILER &compiler;
        const FuncFrame &frame;
        ~ExecuteDepth()
        {
            if (--compiler.execute_depth_ != 0)
                return;
            storm::ScriptRunning(false);
            if (compiler.abort_execution_)
            {
                compiler.abort_execution_ = false;
                compiler.SStack.InvalidateFrom(frame.stack_offset);
            }
        }
    } execute_depth{*this, frame};
    if (execute_depth_++ == 0)
        storm::ScriptRunning(true);
    entid_t eid;
    ATTRIBUTES *pLeftOperandAClass;
    DATA *pLeftOperandElement = nullptr; // a[i][j] = ...: the element itself
    bool bDebugWaitForThisFunc;
    DATA *pVDst;
    DATA *pVSrc;
    bool bUseIndex;
    int32_t dwBXIndex;

    CompilerStage = CS_RUNTIME;

    bDebugWaitForThisFunc = false;

    pVReturnResult = nullptr;
    pVResult = nullptr;

    if (pDbgExpSource == nullptr)
    {
        if (const auto *table_fi = FuncTab.GetFuncPtr(function_code); table_fi != nullptr)
        {
            pfi = table_fi;
            frame = FuncFrame(*table_fi);
        }
        else
        {
            SetError("Invalid function: %s", pfi->name.c_str());
            return false;
        }

        if (frame.offset == INVALID_FUNC_OFFSET)
        {
            SetError("Function (%s) isnt loaded", pfi->name.c_str());
            return false;
        }

        if (frame.segment_id == INTERNAL_SEGMENT_ID)
        {
            SetError("Function (%s) is internal", pfi->name.c_str());
            return false;
        }

        if (frame.segment_id == IMPORTED_SEGMENT_ID)
        {
            SetError("Function (%s) is imported", pfi->name.c_str());
            return false;
        }

        segment_index = GetSegmentIndex(frame.segment_id);
        if (segment_index == INVALID_SEGMENT_INDEX)
        {
            SetError("Function (%s) segment not loaded", pfi->name.c_str());
            return false;
        }
        if (SegmentTable[segment_index].code.empty())
        {
            SetError("Segment (%s) not loaded", SegmentTable[segment_index].name.c_str());
            return false;
        }


        RunningSegmentID = frame.segment_id;
        if (passed_args != kArgsAsDeclared && passed_args != frame.arguments &&
            !BindPassedArguments(*pfi, frame, passed_args))
            return false;
        frame.stack_offset = SStack.GetDataNum() - frame.arguments; // set stack offset

        for (n = 0; n < frame.arguments; n++)
        {
            if (pfi->local_vars[n].type == VAR_REFERENCE)
                continue;
            pV = SStack.Read(frame.stack_offset, n);
            if (pV->GetType() != pfi->local_vars[n].type)
            {
                if (pV->GetType() == VAR_REFERENCE && pV->pReference == nullptr &&
                    (pfi->local_vars[n].type == VAR_AREFERENCE || pfi->local_vars[n].type == VAR_FREF))
                    continue; // nullptr for an aref or fref parameter
                pV = pV->GetVarPointer();
                if (!pV)
                {
                    SetError("uninitialized function argument");
                    return false;
                }

                if (pfi->local_vars[n].type == VAR_AREFERENCE && pV->GetType() == VAR_OBJECT)
                    continue;

                // TODO: remove and fix
                if (false && pV->GetType() != pfi->local_vars[n].type)
                {
                    SetWarning("wrong type of argument %d  %s(%s) <-- [%s]", n, pfi->name.c_str(),
                               Token.GetTypeName(pfi->local_vars[n].type), Token.GetTypeName(pV->GetType()));
                }
            }
        }

        for (n = frame.arguments; n < pfi->local_vars.size(); n++)
        {
            pV = SStack.Push();
            if (pfi->local_vars[n].elements == 0 || (pfi->local_vars[n].array && pfi->local_vars[n].elements == 1))
            {
                pV->SetType(pfi->local_vars[n].type); // an array sized at run time, or of one element
                pV->SetArraySize(static_cast<uint32_t>(pfi->local_vars[n].elements));
            }
            else
                pV->SetType(pfi->local_vars[n].type, pfi->local_vars[n].elements);
        }

        pRun_fi = &frame; // set pointer to 'this' function info

        InstructionPointer = frame.offset;

        pCodeBase = SegmentTable[segment_index].code.data();
        pRunCodeBase = pCodeBase;
    }
    else
    {
        InstructionPointer = 0;
        pCodeBase = pDbgExpSource;
        pRunCodeBase = pCodeBase;
    }

    nLeftOperandIndex = INVALID_ARRAY_INDEX;
    nLeftOperandCode = INVALID_VAR_CODE;
    bLeftOperandType = 0xffffffff;
    pLeftOperandAClass = nullptr;

    // a call from the engine (outermost BC_Execute) gets a time limit: an endless script loop stops
    // with an error and the script stack instead of hanging the engine
    struct CallDepth
    {
        COMPILER &compiler;
        explicit CallDepth(COMPILER &c) : compiler(c)
        {
            if (compiler.scriptCallDepth_++ == 0)
            {
                compiler.scriptCallStart_ = std::chrono::steady_clock::now();
                compiler.scriptCallAborted_ = false;
            }
        }
        ~CallDepth()
        {
            compiler.scriptCallDepth_--;
        }
    } call_depth(*this);

    do
    {
        // the clock is read every 2^20 statements; after a stop every frame of the call returns
        if (scriptCallAborted_ || ((++scriptStatements_ & 0xFFFFF) == 0 && scriptCallTimeLimitMs_ != 0 &&
                                   !ScriptCallInTime()))
            return false;
        // A sixth of the tokens only mark the source line and the end of a statement.
        do
        {
            Token_type = BC_TokenGet(ip, token_data_size);
            if (Token_type == DEBUG_LINE_CODE && core_internal.Exit_flag)
                return false;
        } while (Token_type == DEBUG_LINE_CODE || Token_type == SEPARATOR);

        switch (Token_type)
        {
        case ARGS_NUM:
            psnip_trap();
            break;
        case STACK_COMPARE:
            pV = SStack.Read();
            ExpressionResult.CompareAndSetResult(pV, OP_BOOL_EQUAL);
            break;
        case POP_EXPRESULT:
            pV = SStack.Pop();
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pV);
            break;
        case PUSH_EXPRESULT:
            pV = SStack.Push();
            pV->Copy(&ExpressionResult);
            break;
        case POP_NZ:
            ReadCondition(lvalue);
            if (lvalue == 0)
                break;
            SStack.Pop();
            break;
        case POP_VOID:
            SStack.Pop();
            break;
        case JUMP:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case JUMP_Z:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            ReadCondition(lvalue);
            if (lvalue)
                break;
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case JUMP_NZ:
            memcpy(&jump_offset, &pCodeBase[ip], sizeof(uint32_t));
            ReadCondition(lvalue);
            if (lvalue == 0)
                break;
            if (!BC_Jump(SegmentTable[segment_index], jump_offset))
                return false;
            break;
        case LOCAL_VARIABLE:
            pLeftOperandAClass = nullptr;            // reset attribute
            pLeftOperandElement = nullptr;
            nLeftOperandIndex = INVALID_ARRAY_INDEX; // reset index
            nLeftOperandCode = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]);
            bLeftOperandType = LOCAL_VARIABLE;
            break;
        case VARIABLE:
            pLeftOperandAClass = nullptr;            // reset attribute
            pLeftOperandElement = nullptr;
            nLeftOperandIndex = INVALID_ARRAY_INDEX; // reset index
            nLeftOperandCode = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]);
            bLeftOperandType = VARIABLE;
            break;
        case ACCESS_WORD_CODE:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }

            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }
                pLeftOperandAClass =
                    pLeftOperandAClass->VerifyAttributeClassByCode(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass =
                pLeftOperandAClass->VerifyAttributeClassByCode(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
            break;
            break;
        case ACCESS_WORD:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }
                pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass = pLeftOperandAClass->VerifyAttributeClass((char *)&pRunCodeBase[TLR_DataOffset]);
            break;
            break;
        case ACCESS_VAR:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
            {
                if (pLeftOperandAClass == nullptr)
                {
                    if (bLeftOperandType == VARIABLE)
                    {
                        real_var = VarTab.GetVar(nLeftOperandCode);
                        if (real_var == nullptr)
                        {
                            SetError("Global variable not found");
                            return false;
                        }

                        pV = real_var->value.get();
                    }
                    else
                    {
                        pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pV == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    pLeftOperandAClass = pV->GetAClass();
                }
                if (pLeftOperandAClass == nullptr)
                {
                    SetError("AClass ERROR n1");
                    return false;
                }

                vtype = BC_TokenGet();
                var_code = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]);
                if (!(vtype == VARIABLE || vtype == LOCAL_VARIABLE))
                {
                    SetError("invalid access var");
                    return false;
                }
                if (vtype == VARIABLE)
                {
                    real_var = VarTab.GetVar(var_code);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pVV = real_var->value.get();
                }
                else
                {
                    pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                    if (pVV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pVV->Get(pAccess_string);

                pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, pAccess_string);
                break;
            }
            if (pLeftOperandAClass == nullptr)
            {
                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pV = real_var->value.get();
                }
                else
                {
                    pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                    if (pV == nullptr)
                    {
                        SetError("Local variable not found");
                        return false;
                    }
                }
                pLeftOperandAClass = pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }

            vtype = BC_TokenGet();
            var_code = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]);
            if (!(vtype == VARIABLE || vtype == LOCAL_VARIABLE))
            {
                SetError("invalid access var");
                return false;
            }
            if (vtype == VARIABLE)
            {
                real_var = VarTab.GetVar(var_code);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVV = real_var->value.get();
            }
            else
            {
                pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pVV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            pVV->Get(pAccess_string);

            pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, pAccess_string);

            break;
            break;
        case DEBUG_FILE_NAME:

            break;
        case BLOCK_IN:
        case BLOCK_OUT:
            SetError("Unallowed instraction");
            return false;
            break;
        default:
            SetError("Incorrect instruction code");
            return false;

        case CALL:                                                  // undetermined function call
            vtype = BC_TokenGet();                                  // read variable
            var_code = CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]); // var code
            if (vtype == VARIABLE)
            {
                real_var = VarTab.GetVar(var_code);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVV = real_var->value.get();
            }
            else
            {
                pVV = SStack.Read(pRun_fi->stack_offset, var_code);
                if (pVV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            pVV = pVV->GetVarPointer();
            if (pVV->GetType() != VAR_STRING && pVV->GetType() != VAR_FREF)
            {
                SetError("string call argument isnt string var");
                return false;
            }
            if (pVV->GetType() == VAR_FREF)
            {
                if (pVV->sValue.empty())
                {
                    SetError("call through a null function reference");
                    return false;
                }
                pAccess_string = pVV->sValue.c_str(); // a function reference holds the name
            }
            else
                pVV->Get(pAccess_string); // func name
            func_code = FuncTab.FindFunc(pAccess_string);
            if (func_code == INVALID_FUNC_CODE)
            {
                SetError("function '%s' not found", pAccess_string);
                return false;
            }
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            ExpressionResult.SetFresh(pVResult != nullptr ? 1 : 0);
            break;
        case CALL_FUNCTION:
            memcpy(&func_code, &pCodeBase[ip], sizeof(uint32_t));
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            break;
        case FUNCTION_RETURN_VOID:
            if (frame.return_type != TVOID)
            {
                SetError("function must return value");
                return false;
            }
            SStack.InvalidateFrom(frame.stack_offset);

            return true;
        case FUNCTION_RETURN:
            if (frame.return_type == TVOID)
            {
                SetError("void function return value");

                return false;
            }
            // at this moment result expression placed in EX register

            if (pDbgExpSource == nullptr) // skip stack unwind for dbg expression process    // ????????
                SStack.InvalidateFrom(frame.stack_offset);

            pV = SStack.Push();
            pV->Copy(&ExpressionResult);
            pVReturnResult = pV;

            if (pDbgExpSource == nullptr) // skip test for dbg expression process
                if (frame.return_type != pV->GetType())
                {
                    if (frame.return_type == VAR_INTEGER && pV->GetType() == VAR_PTR)
                    {
                        pV->Convert(VAR_INTEGER);
                        return true;
                    }
                    // a number converts to the declared number type, as in C: a float parameter
                    // keeps the int an argument gave it
                    const bool numbers = (frame.return_type == VAR_INTEGER || frame.return_type == VAR_FLOAT) &&
                                         (pV->GetType() == VAR_INTEGER || pV->GetType() == VAR_FLOAT);
                    if (numbers)
                    {
                        pV->Convert(frame.return_type);
                        return true;
                    }

                    SetError("%s function return %s value", Token.GetTypeName(frame.return_type),
                             Token.GetTypeName(pV->GetType()));
                    return false;
                }
            return true;


        case OP_MULTIPLYEQ:
        case OP_DIVIDEEQ:
        case OP_INCADD:
        case OP_DECADD:
        case OP_OREQ:
        case OP_ANDEQ:
            S_TOKEN_TYPE OpType;
            OpType = Token_type;
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
            }
            pVV = SStack.Read();
            switch (OpType)
            {
            case OP_INCADD:
                pV->Plus(pVV);
                break;
            case OP_DECADD:
                pV->Minus(pVV);
                break;
            case OP_MULTIPLYEQ:
                pV->Multiply(pVV);
                break;
            case OP_DIVIDEEQ:
                pV->Divide(pVV);
                break;
            case OP_OREQ:
            case OP_ANDEQ: {
                const int32_t a = IntValue(pV), b = IntValue(pVV);
                pV->Set(OpType == OP_OREQ ? (a | b) : (a & b));
                break;
            }
            }

            break;

        case OP_INC:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
                pV->Inc();
            else
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
                pV->Inc();
            }
            break;
        case OP_DEC:
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            if (bLeftOperandType == VARIABLE)
            {
                real_var = VarTab.GetVar(nLeftOperandCode);
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pV = real_var->value.get();
            }
            else
            {
                pV = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
            }
            if (nLeftOperandIndex == INVALID_ARRAY_INDEX)
                pV->Dec();
            else
            {
                pV = static_cast<DATA *>(pV->GetArrayElement(nLeftOperandIndex));
                if (pV == nullptr)
                {
                    SetError("invalid array index");
                    return false;
                }
                pV->Dec();
            }
            break;



        case GOTO_COMMAND:
            break;
        case CONTINUE_COMMAND:
            break;
        case BREAK_COMMAND:
            break;

        case CASE_COMMAND:
            return false;

        case UNKNOWN:
            break;
        case VAR_OBJECT:
        case VAR_STRING:
        case VAR_FLOAT:
        case VAR_INTEGER:
        case VAR_PTR:
            break;
        case OPEN_BRACKET:
            break;
        case CLOSE_BRACKET:
            break;
        case COMMA:

            break;
        case DOT:

            break;

        case NUMBER:

            break;
        case FLOAT_NUMBER:

            break;
        case STRING:

            break;

        case PUSH_OBJID_BXINDEX:
        case PUSH_OBJID:

            if (Token_type == PUSH_OBJID_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pV = SStack.Push();
            pV->ClearType();

            pVV = GetOperand(pCodeBase, ip, &Token_type);

            if (!(Token_type == VARIABLE || Token_type == LOCAL_VARIABLE))
            {
                SetError("invalid arg for push_objid");
                return false;
            }
            if (bUseIndex)
            {
                pVV = pVV->GetArrayElement(dwBXIndex);
            }

            if (!(pVV->GetType() == VAR_AREFERENCE || pVV->GetType() == VAR_REFERENCE || pVV->GetType() == VAR_OBJECT))
            {
                SetError("not aref or ref or object");
            }

            pVV->Get(eid);
            pV->Set(eid);
            break;

        case SETAREF:

            pV = SStack.Pop();
            pV->Get(eid);

            // destination
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVDst = SStack.Read();
                break;
            default:
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            if (!pVDst)
            {
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            pVDst = pVDst->GetVarPointer();
            if (!pVDst)
            {
                SetError("invalid argument for setaref (dst)");
                return false;
            }
            if (pVDst->GetType() != VAR_AREFERENCE)
            {
                SetError("isnt aref");
                return false;
            }

            // source
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case AP:
                pVDst->Set(eid);
                pVDst->SetAReference(rAP);

                break;
            default:
                SetError("invalid argument for setref (src)");
                return false;
            }

            break;
        case SETREF_BXINDEX:
        case SETREF:

            if (Token_type == SETREF_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pVDst = nullptr;
            pVSrc = nullptr;

            // destination
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVDst = SStack.Read();
                pVDst->ClearType();
                pVDst->SetType(VAR_REFERENCE);
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVDst = real_var->value.get();
                if (!pVDst)
                    return false;
                if (pVDst->GetType() != VAR_REFERENCE)
                {
                    SetError("'%s' isnt reference", real_var->name.c_str());
                    return false;
                }
                break;
            case LOCAL_VARIABLE:
                pVDst = SStack.Read(pRun_fi->stack_offset, CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (pVDst == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                if (!pVDst)
                    return false;
                if (pVDst->GetType() != VAR_REFERENCE)
                {
                    SetError("Local variable is not reference");
                    return false;
                }
                break;
            default:
                SetError("invalid argument for setref (dst)");
                return false;
            }
            if (!pVDst)
            {
                SetError("invalid argument for setref (dst)");
                return false;
            }

            // source
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case VARIABLE:
                real_var = VarTab.GetVar(CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    return false;
                }

                pVSrc = real_var->value.get();
                if (bUseIndex)
                    pVSrc = pVSrc->GetArrayElement(dwBXIndex);
                if (!pVSrc)
                    return false;
                break;
            case LOCAL_VARIABLE:
                pVSrc = SStack.Read(pRun_fi->stack_offset, CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (pVSrc == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                if (bUseIndex)
                    pVSrc = pVSrc->GetArrayElement(dwBXIndex);
                if (!pVSrc)
                    return false;
                break;
            default:
                SetError("invalid argument for setref (src)");
                return false;
            }

            pVDst->SetReference(pVSrc->GetVarPointer());

            break;
        case MOVE:

            pVDst = nullptr;
            pVSrc = nullptr;
            if (pLeftOperandElement)
            {
                // a[i][j] = value
                BC_TokenGet(ip, token_data_size); // LEFT_OPERAND
                if (BC_TokenGet(ip, token_data_size) != STACK_TOP)
                {
                    SetError("incorrect SRC");
                    return false;
                }
                pVSrc = SStack.Read();
                if (pVSrc->GetType() == VAR_REFERENCE && pLeftOperandElement->GetType() == VAR_REFERENCE)
                    pLeftOperandElement->SetReference(pVSrc->GetReference());
                else
                    AssignValue(pLeftOperandElement->GetVarPointer(), pVSrc);
                break;
            }
            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case LEFT_OPERAND:
                if (nLeftOperandCode == INVALID_VAR_CODE)
                {
                    SetError("no Lvalue");
                    return false;
                }

                if (bLeftOperandType == VARIABLE)
                {
                    real_var = VarTab.GetVar(nLeftOperandCode);
                    if (real_var == nullptr)
                    {
                        SetError("Global variable not found");
                        return false;
                    }

                    pVDst = real_var->value.get();
                }
                else
                {
                    if (pRun_fi)
                    {
                        pVDst = SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                        if (pVDst == nullptr)
                        {
                            SetError("Local variable not found");
                            return false;
                        }
                    }
                    else
                    {
                        pVDst = nullptr;
                        SetError("Internal error, line 4808! Current function not defined...");
                        return false;
                    }
                }
                break;
            default:
                SetError("invalid DST");
                return false;
            }

            if (pVDst == nullptr)
            {
                SetError("no LValue");
                return false;
            }

            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case STACK_TOP:
                pVSrc = SStack.Read();
                if (!pVSrc)
                    return false;
                break;
            default:
                SetError("incorrect SRC");
                return false;
            }

            if (pLeftOperandAClass != nullptr)
            {
                const char *pChar;

                ExpressionResult.ClearType();
                ExpressionResult.Copy(pVSrc);
                // an object (a variable, a reference to one or a literal) assigned to an attribute
                // replaces the attribute's value and children with a copy of the object's tree
                if (DATA *object = Deref(pVSrc); object && object->GetType() == VAR_OBJECT && !object->IsArray())
                {
                    if (!object->GetAClass())
                    {
                        SetError("object without attributes assigned to an attribute");
                        return false;
                    }
                    *pLeftOperandAClass = object->GetAClass()->Copy();
                }
                else
                {
                    // 2 signs precision --------------------------------
                    if (ExpressionResult.GetType() == VAR_FLOAT)
                    {
                        float fV1;
                        ExpressionResult.Get(fV1);
                        char text[storm::script::kFixed7Size];
                        storm::script::FormatFixed7(fV1, text);
                        ExpressionResult.Set(text);
                    }
                    // 2 signs precision --------------------------------
                    ExpressionResult.Convert(VAR_STRING);
                    if (!ExpressionResult.Get(pChar))
                        break;
                    pLeftOperandAClass->SetValue(pChar);
                }

                if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
                {
                    // the element of an object array, directly or through a reference to it
                    pVDst = pVDst->GetVarPointer();
                    if (pVDst)
                        pVDst = pVDst->GetArrayElement(nLeftOperandIndex);
                }
                if (!pVDst)
                    return false;

                pVDst->Get(eid);
                if (bEntityUpdate)
                {
                    core_internal.Entity_AttributeChanged(eid, pLeftOperandAClass);
                }
                break;
            }

            if (pVSrc->GetType() == VAR_REFERENCE && nLeftOperandIndex != INVALID_ARRAY_INDEX)
            {
                // refs[i] = reference: an element of an array of references (directly or through
                // a reference to it) refers to the value
                DATA *container = pVDst->IsArray() ? pVDst : pVDst->GetVarPointer();
                DATA *element = container && container->IsArray() ? container->GetArrayElement(nLeftOperandIndex)
                                                                  : nullptr;
                if (element && element->GetType() == VAR_REFERENCE)
                {
                    element->SetReference(pVSrc->GetReference());
                    break;
                }
            }
            if (pVSrc->GetType() == VAR_REFERENCE && pVDst->GetType() == VAR_REFERENCE)
            {
                pVDst->SetReference(pVSrc->GetReference());
                break;
            }

            pVDst = pVDst->GetVarPointer();
            if (!pVDst)
            {
                SetError("no DST");
                return false;
            }

            if (nLeftOperandIndex != INVALID_ARRAY_INDEX)
                pVDst = pVDst->GetArrayElement(nLeftOperandIndex);
            if (!pVDst)
                return false;

            DATA *pVTTSrc;
            DATA *pVTTDst;
            pVTTSrc = pVSrc->GetVarPointer();
            pVTTDst = pVDst->GetVarPointer();

            if (pVTTSrc->GetType() != pVTTDst->GetType())
            {
                // ??? think about a little later
                ExpressionResult.ClearType();
                ExpressionResult.Copy(pVSrc);
                if (ExpressionResult.IsReference())
                    ExpressionResult.RefConvert();
                if (!ExpressionResult.Convert(pVTTDst->GetType())) /// ??????????
                {
                    break; // skip unallowed conversion, write to log(in Convert func)
                }
                pVSrc = ExpressionResult.GetVarPointer(); // converted source
            }

            // if not attribute operation proceed to variables copy

            pVDst->Copy(pVSrc);

            break;

        case STACK_ALLOC:
            SStack.Push();
            break;

        case STACK_WRITE_BXINDEX:
        case STACK_PUSH:
        case STACK_WRITE:

            if (Token_type == STACK_WRITE_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            if (Token_type == STACK_PUSH)
                pV = SStack.Push();
            else
                pV = SStack.Read();

            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case NUMBER:
                pV->Set(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                break;
            case FLOAT_NUMBER:
                pV->Set(CodeOperand<float>(&pRunCodeBase[TLR_DataOffset]));
                break;
            case STRING:
            {
                // The constant: its size with the terminator (4 bytes), then the text.
                uint32_t size;
                memcpy(&size, &pRunCodeBase[TLR_DataOffset], sizeof(size));
                pV->SetText(std::string_view(&pRunCodeBase[TLR_DataOffset + 4], size - 1));
            }
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                pVV = real_var->value.get();
                if (pVV->IsReference() && !pVV->IsArray()) // an array of references indexes as any array
                {
                    if (bUseIndex)
                    {
                        pVV = pVV->GetVarPointer();
                        pVV = pVV->GetArrayElement(dwBXIndex);
                        if (!pVV)
                        {
                            SetError("invalid ref");
                            break;
                        }

                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVV);
                    }
                    else
                    {
                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVV->GetReference());
                    }
                    break;
                }

                if (bUseIndex)
                {
                    pVV = pVV->GetArrayElement(dwBXIndex);
                    if (!pVV)
                    {
                        SetError("invalid array index");
                        break;
                    }
                    pV->Copy(pVV);
                }
                else
                    pV->Copy(pVV);

                break;
            case LOCAL_VARIABLE:
                pVar = SStack.Read(pRun_fi->stack_offset, CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (pVar == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                if (pVar->IsReference() && !pVar->IsArray()) // an array of references indexes as any array
                {
                    if (bUseIndex)
                    {
                        pVar = pVar->GetVarPointer();
                        pVar = pVar->GetArrayElement(dwBXIndex);
                        if (!pVar)
                        {
                            SetError("invalid ref");
                            break;
                        }

                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVar);
                    }
                    else
                    {
                        pV->SetType(VAR_REFERENCE);
                        pV->SetReference(pVar->GetReference());
                    }
                    break;
                }


                if (bUseIndex)
                {
                    pVar = pVar->GetArrayElement(dwBXIndex);
                    if (!pVar)
                    {
                        SetError("invalid array index");
                        break;
                    }
                    pV->Copy(pVar);
                }
                else
                    pV->Copy(pVar);

                break;
            case AX:
                pV->Copy(&rAX);
                break;
            case BX:
                pV->Copy(&rBX);
                break;
            case EX:
                pV->Copy(&ExpressionResult);
                break;
            case AP_VALUE:
                if (!rAP)
                {
                    SetError("no rAP data");
                    pV->Set("error");
                    break;
                }
                pV->SetText(rAP->HasValue() ? std::string_view(rAP->GetValue()) : std::string_view());
                break;
            default:
                SetError("invalid argument for STACK_PUSH");
                return false;
            }
            break;

        case STACK_POP_VOID:
            SStack.Pop();
            break;
        case STACK_POP:
        case STACK_READ:
            if (SStack.GetDataNum() == 0)
                break;
            if (Token_type == STACK_POP)
                pV = SStack.Pop();
            else
                pV = SStack.Read();

            Token_type = BC_TokenGet(ip, token_data_size);
            switch (Token_type)
            {
            case VARIABLE:
                real_var = VarTab.GetVar(CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                real_var->value->Copy(pV);
                break;
            case LOCAL_VARIABLE:
                pVar = SStack.Read(pRun_fi->stack_offset, CodeOperand<uint32_t>(&pCodeBase[ip]));
                if (pVar == nullptr)
                {
                    SetError("Local variable not found");
                    return false;
                }
                pVar->Copy(pV);
                break;
            case AX:
                rAX.ClearType();
                rAX.Copy(pV);
                break;
            case BX:
                rBX.ClearType();
                rBX.Copy(pV);
                break;
            case EX:
                ExpressionResult.ClearType();
                ExpressionResult.Copy(pV);
                break;
            case LEFTOP_INDEX:
                pV->Convert(VAR_INTEGER);
                pV->Get(nLeftOperandIndex);
                break;
            default:
                SetError("invalid argument for STACK_POP");
                return false;
            }
            break;

        case OP_BIT_OR:
        case OP_BIT_AND:
        case OP_SHL:
        case OP_SHR: {
            pV = GetOperand(pCodeBase, ip);
            pVV = GetOperand(pCodeBase, ip);
            if (!pV || !pVV)
                return false;
            const auto a = static_cast<uint32_t>(IntValue(pV));
            const auto b = static_cast<uint32_t>(IntValue(pVV));
            uint32_t r = 0;
            switch (Token_type)
            {
            case OP_BIT_OR:
                r = a | b;
                break;
            case OP_BIT_AND:
                r = a & b;
                break;
            case OP_SHL:
                r = b < 32 ? a << b : 0;
                break;
            default:
                r = b < 32 ? a >> b : 0;
                break;
            }
            pV->ClearType();
            pV->Set(static_cast<int32_t>(r));
            break;
        }
        case OP_BIT_NOT:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            lvalue = ~IntValue(pV);
            pV->ClearType();
            pV->Set(lvalue);
            break;
        case OP_CAST: {
            // int(x) float(x) string(x) bool(x) fref(x): a converted copy of the value on the stack top
            uint32_t target;
            memcpy(&target, &pCodeBase[ip], sizeof(uint32_t));
            pV = SStack.Read();
            if (!CastValue(pV, static_cast<S_TOKEN_TYPE>(target)))
                return false;
            break;
        }
        case PUSH_NULL:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            break;
        case OP_IS_NULL: {
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            const bool null = pV->IsNull();
            pV->ClearType();
            pV->Set(static_cast<int32_t>(null));
            break;
        }
        case OP_TYPEID: {
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            const int32_t id = TypeidOf(pV);
            pV->ClearType();
            pV->Set(id);
            break;
        }
        case PUSH_AP:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_AREFERENCE);
            pV->SetAReference(rAP);
            break;
        case ATTR_DEFAULT: {
            // stack: attribute reference, default value
            uint32_t target;
            memcpy(&target, &pCodeBase[ip], sizeof(uint32_t));
            pVV = SStack.Pop(); // default
            pV = SStack.Read(); // attribute reference -> result
            ATTRIBUTES *attribute = pV->AttributesClass; // the attribute reference PUSH_AP made, null if missing
            if (attribute != nullptr)
            {
                pV->ClearType();
                pV->Set(to_string(attribute->GetThisAttr()));
            }
            else
            {
                pV->ClearType();
                pV->Copy(pVV);
            }
            if (!CastValue(pV, static_cast<S_TOKEN_TYPE>(target)))
                return false;
            break;
        }
        case ADVANCE_AP_QUIET: {
            // as ADVANCE_AP without creating or reporting missing attributes: rAP becomes null
            Token_type = BC_TokenGet();
            const char *name = nullptr;
            if (Token_type == ACCESS_WORD_CODE)
            {
                if (rAP)
                    rAP = rAP->GetAttributeClassByCode(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                break;
            }
            pVV = Token_type == VARIABLE ? VarTab.GetVar(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]))->value.get()
                                         : SStack.Read(pRun_fi->stack_offset, CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pVV);
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(name))
            {
                SetError("invalid type for attribute var");
                return false;
            }
            if (rAP)
                rAP = rAP->FindAClass(rAP, name);
            break;
        }
        case OP_IN: {
            // stack: key, object reference / attribute reference -> 1 if the attribute (path) exists
            pVV = SStack.Pop();
            pV = SStack.Read();
            ATTRIBUTES *root = AttributesOf(pVV);
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pV);
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("'in': the key must be a string");
                return false;
            }
            const bool found = root != nullptr && root->FindAClass(root, key) != nullptr;
            pV->ClearType();
            pV->Set(static_cast<int32_t>(found));
            break;
        }
        case OP_SIZEOF_REF: {
            // @x: elements of an array, attributes of an object or attribute reference
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            DATA *target = Deref(pV);
            int32_t count = 0;
            if (target && target->IsArray())
                count = static_cast<int32_t>(target->GetElementsNum());
            else if (ATTRIBUTES *attributes = AttributesOf(pV))
                count = static_cast<int32_t>(attributes->GetAttributesNum());
            else if (!(target == nullptr || pV->IsNull() || (target && target->IsNull())))
            {
                // a null reference has no attributes: 0 (CL: x = a.b$aref; @x when b is missing)
                SetError("'@' of a %s value", Token.GetTypeName(target->GetType()));
                return false;
            }
            pV->ClearType();
            pV->Set(count);
            break;
        }
        case RESIZE_ARRAY: // @array = BX
            pV = GetOperand(pCodeBase, ip);
            if (!pV || !(pV = pV->GetVarPointer()) || !pV->IsArray())
            {
                SetError("'@x = size': x is not an array");
                return false;
            }
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            if (lvalue < 1)
            {
                SetError("array size %d", lvalue);
                return false;
            }
            pV->SetElementsNum(static_cast<uint32_t>(lvalue));
            break;
        case CALL_METHOD: {
            uint32_t args;
            const char *method = &pRunCodeBase[TLR_DataOffset + sizeof(uint32_t)];
            memcpy(&args, &pCodeBase[InstructionPointer + 2], sizeof(uint32_t)); // ARGS_NUM data
            func_code = FindMethod(SStack.Read(SStack.GetDataNum() - args, 0), method);
            if (func_code == INVALID_FUNC_CODE)
                return false;
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            ExpressionResult.SetFresh(pVResult != nullptr ? 1 : 0);
            break;
        }
        case RANGE_NEXT: {
            // container, position, item, index (locals): the next element or attribute, EX = 0 at the end
            uint32_t slots[4];
            memcpy(slots, &pCodeBase[ip], sizeof(slots));
            DATA *container = SStack.Read(pRun_fi->stack_offset, slots[0]);
            DATA *position = SStack.Read(pRun_fi->stack_offset, slots[1]);
            DATA *item = SStack.Read(pRun_fi->stack_offset, slots[2]);
            const int32_t at = position->GetInt();
            DATA *array = container->GetType() == VAR_REFERENCE ? Deref(container) : nullptr;
            ATTRIBUTES *attributes = array ? nullptr : AttributesOf(container);
            const int32_t count = array ? (array->IsArray() ? static_cast<int32_t>(array->GetElementsNum()) : 1)
                                        : attributes ? static_cast<int32_t>(attributes->GetAttributesNum()) : 0;
            ExpressionResult.ClearType();
            if (at >= count)
            {
                ExpressionResult.Set(int32_t{0});
                break;
            }
            if (array)
            {
                DATA *element = array->IsArray() ? array->GetArrayElement(at) : array;
                if (item->GetType() == VAR_REFERENCE)
                    item->SetReference(element->GetVarPointer());
                else
                    item->Copy(element);
            }
            else
                item->SetAReference(attributes->GetAttributeClass(static_cast<uint32_t>(at)));
            if (slots[3] != INVALID_VAR_CODE)
            {
                // the key: the position, or for attributes into a string key the attribute's name
                DATA *key = SStack.Read(pRun_fi->stack_offset, slots[3]);
                if (!array && key->GetType() == VAR_STRING)
                    key->Set(attributes->GetAttributeClass(static_cast<uint32_t>(at))->GetThisName());
                else
                    key->Set(at);
            }
            position->Set(at + 1);
            ExpressionResult.Set(int32_t{1});
            break;
        }
        case OP_SLICE: {
            // stack: slot or text, [start], [end]; operand: the text (a variable or the stack top)
            uint32_t flags;
            memcpy(&flags, &pCodeBase[ip], sizeof(uint32_t));
            int32_t end = INT32_MAX, start = 0;
            if (flags & 2)
                end = IntValue(SStack.Pop());
            if (flags & 1)
                start = IntValue(SStack.Pop());
            const bool character = flags & 4; // text[i]
            pVV = GetOperand(pCodeBase, ip);
            pV = SStack.Read();
            if (!pVV || !pV)
                return false;
            ExpressionResult.ClearType();
            if (DATA *text = Deref(pVV))
                ExpressionResult.Copy(text);
            const char *chars = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(chars))
            {
                SetError("slice of a value that is not text");
                return false;
            }
            const auto length = static_cast<int32_t>(strlen(chars));
            auto clamp = [length](int32_t i) { return std::clamp(i < 0 ? length + i : i, 0, length); };
            start = clamp(start);
            end = character ? std::min(start + 1, length) : clamp(end);
            const std::string slice = start < end ? std::string(chars + start, end - start) : std::string();
            pV->ClearType();
            pV->Set(slice);
            break;
        }
        case ARRAY_BUILD: {
            // stack: count items -> an array of the first item's type
            uint32_t count;
            memcpy(&count, &pCodeBase[ip], sizeof(uint32_t));
            std::vector<DATA> items(count);
            for (uint32_t n = count; n-- > 0;)
            {
                items[n].SetVCompiler(this);
                DATA *value = Deref(SStack.Pop());
                if (!value)
                {
                    SetError("array literal item is a null reference");
                    return false;
                }
                items[n].Copy(value);
            }
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(count ? items[0].GetType() : VAR_INTEGER);
            pV->SetArraySize(count);
            for (uint32_t n = 0; n < count; n++)
                pV->GetArrayElement(n)->Copy(&items[n]);
            break;
        }
        case ADVANCE_AP_EXPR: {
            // stack: saved attribute reference, key -> rAP = its attribute named key
            uint32_t mode;
            memcpy(&mode, &pCodeBase[ip], sizeof(uint32_t));
            ExpressionResult.ClearType();
            ExpressionResult.Copy(SStack.Pop());
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("attribute name expression is not text");
                return false;
            }
            pV = SStack.Pop();
            rAP = pV->AttributesClass;
            if (!rAP)
            {
                if (mode != kAccessQuiet)
                    SetError("missed attribute before (%s)", key);
                break;
            }
            ATTRIBUTES *base = rAP;
            rAP = mode == kAccessCreate ? base->CreateSubAClass(base, key) : base->FindAClass(base, key);
            if (!rAP && mode == kAccessRead)
                SetError("missed attribute: %s", key);
            break;
        }
        case ACCESS_EXPR: {
            // left side: the attribute named by the text on the stack
            if (nLeftOperandCode == INVALID_VAR_CODE)
            {
                SetError("no Lvalue");
                return false;
            }
            ExpressionResult.ClearType();
            ExpressionResult.Copy(SStack.Pop());
            const char *key = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(key))
            {
                SetError("attribute name expression is not text");
                return false;
            }
            if (pLeftOperandAClass == nullptr)
            {
                pV = bLeftOperandType == VARIABLE ? VarTab.GetVar(nLeftOperandCode)->value.get()
                                                  : SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                pLeftOperandAClass =
                    nLeftOperandIndex == INVALID_ARRAY_INDEX ? pV->GetAClass() : pV->GetAClass(nLeftOperandIndex);
            }
            if (pLeftOperandAClass == nullptr)
            {
                SetError("AClass ERROR n1");
                return false;
            }
            pLeftOperandAClass = pLeftOperandAClass->CreateSubAClass(pLeftOperandAClass, key);
            break;
        }
        case CALL_NAMED: {
            // stack: function name, arguments: calls it; the result (if any) replaces the name
            uint32_t args;
            memcpy(&args, &pCodeBase[InstructionPointer + 2], sizeof(uint32_t)); // ARGS_NUM data
            const uint32_t name_slot = SStack.GetDataNum() - args - 1;
            DATA *name = Deref(SStack.Read(name_slot, 0));
            std::string function;
            if (name && name->GetType() == VAR_FREF)
                function = name->sValue;
            else if (name)
            {
                ExpressionResult.ClearType();
                ExpressionResult.Copy(name);
                const char *text = nullptr;
                if (ExpressionResult.Convert(VAR_STRING) && ExpressionResult.Get(text))
                    function = text;
            }
            func_code = function.empty() ? INVALID_FUNC_CODE : FuncTab.FindFunc(function);
            if (func_code == INVALID_FUNC_CODE)
            {
                SetError("call: function '%s' not found", function.c_str());
                return false;
            }
            pVResult = nullptr;
            if (!BC_CallFunction(func_code, ip, pVResult))
                return false;
            if (pVResult)
            {
                SStack.Read(name_slot, 0)->ClearType();
                SStack.Read(name_slot, 0)->Copy(pVResult);
            }
            SStack.Pop();
            ExpressionResult.SetFresh(pVResult != nullptr ? 1 : 0);
            break;
        }
        case NEW_ARRAY: {
            // stack: size -> a reference to a new array of the type
            uint32_t type;
            memcpy(&type, &pCodeBase[ip], sizeof(uint32_t));
            pV = SStack.Read();
            const int32_t size = IntValue(pV);
            if (size < 0)
            {
                SetError("new array of %d elements", size);
                return false;
            }
            DATA &array = newArrays_.emplace_back();
            array.SetVCompiler(this);
            array.SetType(static_cast<S_TOKEN_TYPE>(type));
            array.SetArraySize(static_cast<uint32_t>(size));
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            pV->SetReference(&array);
            break;
        }
        case STACK_INDEX: {
            // the stack top (an array or a reference to one) becomes a reference to its element BX
            pV = SStack.Read();
            DATA *array = Deref(pV);
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            DATA *element = array && array->IsArray() ? array->GetArrayElement(lvalue) : nullptr;
            if (!element)
            {
                SetError("[%d]: not an array element", lvalue);
                return false;
            }
            pV->ClearType();
            pV->SetType(VAR_REFERENCE);
            pV->SetReference(element);
            break;
        }
        case LEFTOP_SUBINDEX: {
            // left side a[i][j]: from the element a[i] (an array or a reference to one) to its element BX
            DATA *base = pLeftOperandElement;
            if (!base)
            {
                base = bLeftOperandType == VARIABLE ? VarTab.GetVar(nLeftOperandCode)->value.get()
                                                    : SStack.Read(pRun_fi->stack_offset, nLeftOperandCode);
                DATA *container = base->IsArray() ? base : base->GetVarPointer();
                base = container && nLeftOperandIndex != INVALID_ARRAY_INDEX && container->IsArray()
                           ? container->GetArrayElement(nLeftOperandIndex)
                           : nullptr;
            }
            DATA *array = base ? Deref(base) : nullptr;
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            pLeftOperandElement = array && array->IsArray() ? array->GetArrayElement(lvalue) : nullptr;
            if (!pLeftOperandElement)
            {
                SetError("[%d]: not an array element", lvalue);
                return false;
            }
            break;
        }
        case AP_FROM_STACK: // rAP = the attributes of the value on the stack (popped)
            pV = SStack.Pop();
            rAP = pV ? AttributesOf(pV) : nullptr;
            if (!rAP)
                SetError("attribute of a call result that has no attributes");
            break;
        case AP_CHILD: // rAP = its BX-th attribute (aref[i])
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            rAP = rAP && lvalue >= 0 && static_cast<size_t>(lvalue) < rAP->GetAttributesNum()
                      ? rAP->GetAttributeClass(static_cast<uint32_t>(lvalue))
                      : nullptr;
            if (!rAP)
                SetError("aref[%d]: no such attribute", lvalue);
            break;
        case ATTR_PARENT: {
            pV = SStack.Read();
            ATTRIBUTES *attribute = AttributesOf(pV);
            ATTRIBUTES *parent = attribute ? attribute->GetParent() : nullptr;
            pV->ClearType();
            pV->SetType(VAR_AREFERENCE);
            pV->SetAReference(parent);
            break;
        }
        case ATTR_DELETE: {
            // leaves 0 for the statement form to drop
            pV = SStack.Read();
            ATTRIBUTES *attribute = AttributesOf(pV);
            ATTRIBUTES *parent = attribute ? attribute->GetParent() : nullptr;
            if (parent)
                parent->DeleteAttributeClassX(attribute);
            pV->ClearType();
            pV->Set(int32_t{0});
            break;
        }
        case OBJ_NEW:
            pV = SStack.Push();
            pV->ClearType();
            pV->SetType(VAR_OBJECT);
            break;
        case OBJ_SET: {
            // stack: object, value
            const char *key = &pRunCodeBase[TLR_DataOffset + sizeof(uint32_t)];
            pVV = SStack.Pop();
            pV = SStack.Read();
            ATTRIBUTES *object = pV->GetAClass();
            if (!object)
            {
                SetError("object literal without attributes");
                return false;
            }
            DATA *value = Deref(pVV);
            if (value && value->GetType() == VAR_OBJECT && !value->IsArray())
            {
                ATTRIBUTES *child = object->CreateSubAClass(object, key);
                if (value->GetAClass())
                    *child = value->GetAClass()->Copy();
                break;
            }
            ExpressionResult.ClearType();
            ExpressionResult.Copy(pVV);
            if (ExpressionResult.GetType() == VAR_FLOAT)
            {
                float f;
                ExpressionResult.Get(f);
                sprintf_s(gs, "%.7f", f);
                ExpressionResult.Set(gs);
            }
            const char *text = nullptr;
            if (!ExpressionResult.Convert(VAR_STRING) || !ExpressionResult.Get(text))
            {
                SetError("object literal: '%s' has no text value", key);
                return false;
            }
            object->SetAttribute(key, text);
            break;
        }
        case SET_ARRAY_SIZE: // array local sized at run time: BX elements
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            rBX.Convert(VAR_INTEGER);
            rBX.Get(lvalue);
            if (lvalue < 0)
            {
                SetError("negative array size %d", lvalue);
                return false;
            }
            pV->SetArraySize(static_cast<uint32_t>(lvalue));
            break;
        case OP_BOOL_CONVERT:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->BoolConvert();
            break;
        case OP_BOOL_AND:
        case OP_BOOL_OR:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;

            pV->BoolConvert();
            pV->RefConvert();
            pV->CompareAndSetResult(pVV, Token_type);
            break;
        case OP_NOT_EQUAL:
        case OP_LESSER_OR_EQUAL:
        case OP_LESSER:
        case OP_GREATER_OR_EQUAL:
        case OP_GREATER:
        case OP_BOOL_EQUAL:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->CompareAndSetResult(pVV, Token_type);
            break;
        case OP_PLUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            // a reference on the left is its value: the operation must not write into the referenced variable
            pV->RefConvert();
            pV->Plus(pVV);
            break;
        case OP_MINUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->Minus(pVV);
            break;
        case OP_MULTIPLY:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->Multiply(pVV);
            break;
        case OP_DIVIDE:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->Divide(pVV);
            break;
        case OP_MODUL:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->Modul(pVV);
            break;
        case OP_POWER:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pVV = GetOperand(pCodeBase, ip);
            if (!pVV)
                return false;
            pV->RefConvert();
            pV->Power(pVV);
            break;
        case OP_SMINUS:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->RefConvert();
            pV->Inverse();
            break;
        case OP_BOOL_NEG:
            pV = GetOperand(pCodeBase, ip);
            if (!pV)
                return false;
            pV->RefConvert();
            pV->Neg();
            break;

        case MOVEAP_BXINDEX:
        case MOVEAP:

            if (Token_type == MOVEAP_BXINDEX)
            {
                bUseIndex = true;
                rBX.Get(dwBXIndex);
            }
            else
                bUseIndex = false;

            pV = GetOperand(pCodeBase, ip, &Token_type);
            switch (Token_type)
            {
            case VARIABLE:
            case LOCAL_VARIABLE:
                SetError("not implemented");
                return false;
                break;
            case AP:
                pVV = GetOperand(pCodeBase, ip, &Token_type);
                if (Token_type == AP)
                    break;
                if (!pVV)
                    return false;
                if (bUseIndex)
                    pVV = pVV->GetArrayElement(dwBXIndex);
                if (!pVV)
                {
                    SetError("invalid array index");
                    return false;
                }
                rAP = pVV->GetAClass();
                break;
            }
            break;
        case VERIFY_AP:
        case ADVANCE_AP:

            S_TOKEN_TYPE sttV = Token_type;

            const char *pChar;
            if (!rAP)
            {
                SetError("null ap");
                BC_TokenGet();
                break;
            }

            Token_type = BC_TokenGet();
            switch (Token_type)
            {
            case ACCESS_WORD_CODE:
                if (sttV == VERIFY_AP)
                    rAP = rAP->VerifyAttributeClassByCode(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                else
                    rAP = rAP->GetAttributeClassByCode(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                if (!rAP)
                    SetError("missed attribute: %s", SCodec.Convert(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset])));
                break;
            case VARIABLE:
                real_var = VarTab.GetVar(CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                if (real_var == nullptr)
                {
                    SetError("Global variable not found");
                    break;
                }

                ExpressionResult.ClearType();
                ExpressionResult.Copy(real_var->value.get());
                if (!ExpressionResult.Convert(VAR_STRING))
                {
                    SetError("invalid type for attribute var");
                    break;
                }
                ExpressionResult.Get(pChar);
                if (sttV == VERIFY_AP)
                    rAP = rAP->CreateSubAClass(rAP, pChar);
                else
                    rAP = rAP->FindAClass(rAP, pChar);

                if (!rAP)
                    SetError("missed attribute: %s", pChar);
                break;
            case LOCAL_VARIABLE:
                pV = SStack.Read(pRun_fi->stack_offset, CodeOperand<int32_t>(&pRunCodeBase[TLR_DataOffset]));
                if (pV == nullptr)
                {
                    SetError("Local variable not found");
                    break;
                }

                ExpressionResult.ClearType();
                ExpressionResult.Copy(pV);
                if (!ExpressionResult.Convert(VAR_STRING))
                {
                    SetError("invalid type for attribute var");
                    break;
                }
                ExpressionResult.Get(pChar);

                if (sttV == VERIFY_AP)
                    rAP = rAP->CreateSubAClass(rAP, pChar);
                else
                    rAP = rAP->FindAClass(rAP, pChar);


                if (!rAP)
                    SetError("missed attribute: %s", pChar);

                break;
            default:
                SetError("invalid arg type for ADVANCE_AP");
                return false;
            }


            break;

        }
    } while (Token_type != END_OF_PROGRAMM);

    return false;
}

void COMPILER::CompileNumber(SEGMENT_DESC &Segment)
{
    int32_t lvalue;
    lvalue = ParseIntLiteral(Token.GetData());
    CompileToken(Segment, NUMBER, 1, (char *)&lvalue, sizeof(int32_t));
}

void COMPILER::CompileFloatNumber(SEGMENT_DESC &Segment)
{
    float fvalue;
    fvalue = static_cast<float>(atof(Token.GetData()));
    CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&fvalue, sizeof(float));
}

void COMPILER::CompileString(SEGMENT_DESC &Segment)
{
    uint32_t string_size;
    const char *pData = Token.GetData();
    if (pData != nullptr)
    {
        string_size = strlen(Token.GetData()) + 1;
        CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), Token.GetData(), string_size);
    }
    else
    {
        string_size = strlen("") + 1;
        CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), "", string_size);
    }
}

bool COMPILER::CompileUnknown(SEGMENT_DESC &Segment)
{
    uint32_t code;
    switch (DetectUnknown(code))
    {
    case CALL_FUNCTION:
        CompileToken(Segment, CALL_FUNCTION, 1, (char *)&code, sizeof(uint32_t));
        return true;
    case LOCAL_VARIABLE:
        CompileToken(Segment, LOCAL_VARIABLE, 1, (char *)&code, sizeof(uint32_t));
        return true;
    case VARIABLE:
        CompileToken(Segment, VARIABLE, 1, (char *)&code, sizeof(uint32_t));
        return true;
    case DEFINE_VAL: {
        DEFINFO di;
        DefTab.GetDef(di, code);
        switch (di.deftype)
        {
        case NUMBER:
            CompileToken(Segment, NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
            break;
        case FLOAT_NUMBER:
            CompileToken(Segment, FLOAT_NUMBER, 1, (char *)&di.data4b, sizeof(uint32_t));
            break;
        case STRING:
            uint32_t string_size;
            string_size = strlen((char *)di.data4b) + 1;
            CompileToken(Segment, STRING, 2, (char *)&string_size, sizeof(uint32_t), (char *)di.data4b, string_size);
            break;
        }
        return true;
    }
    default:
        break;
    }
    SetError("Undeclared identifier: %s", Token.GetData());
    return false;
}

// On an identifier: whether "(" follows it. The token stream is left where it was.
bool COMPILER::NextIsOpenBracket()
{
    const bool call = Token.Get() == OPEN_BRACKET;
    Token.StepBack();
    Token.StepBack();
    Token.Get();
    return call;
}

S_TOKEN_TYPE COMPILER::DetectUnknown(uint32_t &code)
{
    const uint32_t func_code = FuncTab.FindFunc(Token.GetData());

    uint32_t var_code = FindLocal(Token.GetData());
    if (func_code != INVALID_FUNC_CODE)
    {
        // A local or parameter shadows a function of the same name except where the name is called: which functions
        // are known depends on the segments loaded so far, a local's meaning must not
        if (var_code != INVALID_VAR_CODE && !NextIsOpenBracket())
        {
            code = var_code;
            return LOCAL_VARIABLE;
        }
        code = func_code;
        return CALL_FUNCTION;
    }

    if (var_code != INVALID_VAR_CODE)
    {
        code = var_code;
        return LOCAL_VARIABLE;
    }

    var_code = VarTab.FindVar(Token.GetData());
    if (var_code != INVALID_VAR_CODE)
    {
        code = var_code;
        return VARIABLE;
    }

    const uint32_t def_code = DefTab.FindDef(Token.GetData());
    if (def_code != INVALID_DEF_CODE)
    {
        code = def_code;
        return DEFINE_VAL;
    }

    return UNKNOWN;
}

void COMPILER::ExitProgram()
{
    const uint32_t function_code = FuncTab.FindFunc("ExitMain");
    if (function_code != INVALID_FUNC_CODE)
    {
        DATA *pResult;
        BC_Execute(function_code, pResult);
    }
    core_internal.Exit();
}

void COMPILER::ClearEvents()
{
    EventTab.Clear();
}

bool COMPILER::OnLoad()
{
    const uint32_t function_code = FuncTab.FindFunc("OnLoad");
    DATA *pResult;
    const bool executed = BC_Execute(function_code, pResult);
    if (executed)
        AttachEngineSegments();
#ifdef STORM_TOUCH_UI
    else
        storm::android::traceLandAdapter(0, "onload-execution-failed");
#endif
#ifdef STORM_SCENE_SWEEP
    LoadSceneSweep(); // a loaded save replaced the script state, the sweep's segment with it
#endif
    return true;
}

// The script state as a save holds it: the head (save_state.hpp writeHead) and every global variable with its
// attributes, except those of attached segments (Attach): the engine loads them again with every program.
std::vector<char> COMPILER::SerializeState()
{
    std::vector<uint32_t> attached;
    for (const SEGMENT_DESC &segment : SegmentTable)
        if (segment.attached)
            attached.push_back(segment.id);
    const auto saved = [&](const VarInfo &info) { return std::ranges::find(attached, info.segment_id) == attached.end(); };

    // A variable of an unloaded segment is written as the one before it again (the format's old semantics).
    std::vector<const VarInfo *> variables;
    const VarInfo *last_var{nullptr};
    for (uint32_t n = 0; n < VarTab.GetVarNum(); n++)
    {
        const VarInfo *real_var = VarTab.GetVar(n);
        if (real_var != nullptr && !saved(*real_var))
            continue;
        if (real_var == nullptr)
            real_var = last_var;
        else
            last_var = real_var;
        variables.push_back(real_var);
    }

    storm::save::StateWriter out(*this);
    // The table has a first segment with a name: a save runs from a script, and a segment stays in the table only
    // when the file it names compiled.
    storm::save::writeHead(out, ProgramDirectory, SCodec, SegmentTable.front().name,
                           static_cast<uint32_t>(variables.size()));
    // Built after OnSave has run: the script may still delete attributes there.
    const storm::save::ReferenceIndex references(VarTab, saved);
    for (const VarInfo *variable : variables)
    {
        out.string(variable->name.c_str()); // ***
        out.variable(variable->value.get(), variable->name.c_str(), references);
    }
    return out.take();
}

// The game's name for its saves, kept in the file's header.
std::string COMPILER::SaveFileInfo()
{
    auto *info = static_cast<VDATA *>(core_internal.GetScriptVariable("savefile_info"));
    return info && info->GetString() ? info->GetString() : "save";
}

bool COMPILER::SaveState(std::vector<char> &image)
{
    DATA *pResult;
    const uint32_t function_code = FuncTab.FindFunc("OnSave");
    if (function_code != INVALID_FUNC_CODE)
        BC_Execute(function_code, pResult);

    // Serialize into memory only: the caller writes the whole record once (CoreImpl::SetSaveData).
    const auto state = SerializeState();

    std::string error;
    const bool built =
        storm::save::buildState(SaveFileInfo().c_str(), state.data(), static_cast<uint32_t>(state.size()), image, error);
    if (!built)
    {
        spdlog::error("Cannot build save image: {}", error);
    }

    return built;
}

// Whether a saved state is of the running program, asked before the running game is erased: "" when its program
// directory is this one and every segment it names is loaded now, else why not. A load compiles the program again
// from what the running one was compiled from, never from a place a file names: the state's segments become the
// running program's own names for them.
std::string COMPILER::BindStateProgram(storm::save::ParsedState &state) const
{
    // The game's paths take either slash and any case of ASCII letters.
    const auto folded = [](std::string path) {
        std::ranges::replace(path, '/', '\\');
        while (!path.empty() && path.back() == '\\')
            path.pop_back();
        return storm::AsciiLower(std::move(path));
    };
    if (folded(state.programDirectory.value_or("")) != folded(ProgramDirectory ? ProgramDirectory : ""))
        return std::string("the save is of another program: its scripts are not in this game's '") +
               (ProgramDirectory ? ProgramDirectory : "") + "'";
    if (state.segments.empty())
        return "the save names no script segment";
    for (std::string &segment : state.segments)
    {
        const auto running = std::ranges::find_if(SegmentTable, [&](const SEGMENT_DESC &loaded) {
            return !loaded.bUnload && folded(loaded.name) == folded(segment);
        });
        if (running == SegmentTable.end())
            return "the save is of another program: it names a script segment this game does not run";
        segment = running->name;
    }
    return {};
}

// `raw` is the uncompressed script state (save_file.hpp) and `state` its structure, both checked before the caller
// erased the running game (save_state.hpp, BindStateProgram): from here on nothing of the file can fail. What
// remains is the program itself, which compiled in this session (a segment that does not compile again).
bool COMPILER::LoadState(const std::vector<char> &raw, const storm::save::ParsedState &state)
{
    char *const directory = std::exchange(ProgramDirectory, nullptr); // Release forgets it
    Release();
    ProgramDirectory = directory;

    // 4. SCodec data: in saved order, which gives every name its saved code
    for (const auto &name : state.codec)
        SCodec.Convert(name.c_str());

    // 3.  Segments names
    // 3.a Initialize internal functions
    // 3.b Load preprocess
    InitInternalFunctions();
    LoadPreprocess();

    for (const auto &segment : state.segments)
    {
        if (!BC_LoadSegment(segment.c_str()))
            return false;
    }

    // 5. Variables table, all variables created during previous step, just read value
    storm::save::applyVariables(raw.data(), raw.size(), state.variables, VarTab, &SCodec,
                                [this](const std::string &line) { SetError("%s", line.c_str()); });
#ifdef STORM_SCENE_SWEEP
    WriteStateDump("load"); // the state as loaded, before the game's OnLoad runs
#endif

    // call to script function "OnLoad()"
    OnLoad();

    return true;
}

bool COMPILER::EventPosted(const char *name)
{
    for (uint32_t n = 0; n < EventMsg.GetClassesNum(); n++)
        if (const S_EVENTMSG *event = EventMsg.Read(n); event && !event->bInvalide && event->pEventName && storm::iEquals(event->pEventName, name))
            return true;
    return false;
}

void COMPILER::AddPostEvent(S_EVENTMSG *pEM)
{
    pEM->NoteAttributes();
    EventMsg.Add(pEM);
}

// An attribute posted with the event and deleted before its time: the handler gets an empty aref.
void COMPILER::ForgetDeletedAttributes(S_EVENTMSG *pEM)
{
    pEM->ForgetDeletedAttributes([&](size_t position) {
        SetError("PostEvent(\"%s\"): its argument %zu names a deleted attribute (deleted before the event's time): "
                 "the handler gets an empty aref", pEM->pEventName, position + 1);
    });
}

void *COMPILER::GetSaveData(const char *file_name, int32_t &data_size)
{
    data_size = 0;
    if (file_name == nullptr)
    {
        return nullptr;
    }

    const auto path = std::filesystem::u8path(fio->ConvertPathResource(file_name));
    std::string error;
    storm::save::Bytes data;
    if (!std::filesystem::exists(path))
    {
        SetError("cant open save file: %s", file_name);
        return nullptr;
    }
    // Corrupt or truncated extended data is reported as "no data" instead of
    // handing the caller a buffer of uninitialized bytes.
    if (!storm::save::readExtDataFromFile(path, data, error))
    {
        spdlog::warn("Ignoring damaged save description in '{}': {}", path.string(), error);
        return nullptr;
    }
    if (data.empty())
    {
        return nullptr;
    }

    char *pResult = new char[data.size()];
    memcpy(pResult, data.data(), data.size());
    data_size = static_cast<int32_t>(data.size());
    return pResult;
}

uint32_t COMPILER::SetScriptFunction(IFUNCINFO *pFuncInfo)
{
    FuncInfo fi;

    if (pFuncInfo->pFuncName == nullptr)
    {
        SetError("Invalid function name in SetScriptFunction(...)");
        return INVALID_FUNCHANDLE;
    }

    const uint32_t nFuncHandle = FuncTab.FindFunc(pFuncInfo->pFuncName);
    if (nFuncHandle != INVALID_FUNC_CODE)
    {
        FuncTab.GetFuncX(fi, nFuncHandle);
        if (fi.arguments != pFuncInfo->nArguments)
        {
            SetError("arguments num mismatch in function '%s'", pFuncInfo->pFuncName);
            return INVALID_FUNCHANDLE;
        }
    }
    else
    {
        // not declared (yet): bound when a later declaration appears (ScanParameterList's caller);
        // scripts that never declare it (another game's) cannot call it, so it is not an error
        pendingNatives_[LowerName(pFuncInfo->pFuncName)] = *pFuncInfo;
        if (undeclaredNatives_)
            undeclaredNatives_->emplace_back(pFuncInfo->pFuncName);
        else
            spdlog::info("Script native '{}' is registered but not declared by the scripts", pFuncInfo->pFuncName);
        return INVALID_FUNCHANDLE;
    }

    fi.name = pFuncInfo->pFuncName;
    fi.imported_func = pFuncInfo->pFuncAddress;
    fi.arguments = pFuncInfo->nArguments;
    fi.segment_id = IMPORTED_SEGMENT_ID;
    fi.offset = INVALID_FUNC_OFFSET;
    fi.stack_offset = 0xffffffff;

    if (pFuncInfo->pReturnValueName == nullptr)
        fi.return_type = TVOID;
    else
    {
        const S_TOKEN_TYPE TokenType = Token.Keyword2TokenType(pFuncInfo->pReturnValueName);
        switch (TokenType)
        {
        case TVOID:
        case VAR_INTEGER:
        case VAR_PTR:
        case VAR_FLOAT:
        case VAR_STRING:
        case VAR_OBJECT:
        case VAR_REFERENCE:
        case VAR_AREFERENCE:
            fi.return_type = TokenType;
            break;
        default:
            SetError("Invalid import function '%s' return type '%s'", pFuncInfo->pFuncName,
                     pFuncInfo->pReturnValueName);
            return INVALID_FUNCHANDLE;
        }
    }

    const uint32_t funch = FuncTab.AddFunc(fi);
    return funch;
}

DATA *COMPILER::GetOperand(const char *pCodeBase, uint32_t &ip, S_TOKEN_TYPE *pTokenType)
{
    uint32_t token_data_size;
    const VarInfo *real_var;
    DATA *pVar;

    const S_TOKEN_TYPE sttResult = BC_TokenGet(ip, token_data_size);
    if (pTokenType)
        *pTokenType = sttResult;
    switch (sttResult)
    {
    case STACK_TOP:
        pVar = SStack.Read();
        return pVar;
    case VARIABLE:
        real_var = VarTab.GetVar(CodeOperand<uint32_t>(&pCodeBase[ip]));
        if (real_var == nullptr)
        {
            SetError("Global variable not found");
            break;
        }
        if (!real_var->value.get())
        {
            SetError("invalid global variable");
            break;
        }
        return real_var->value.get();
    case LOCAL_VARIABLE:
        pVar = SStack.Read(pRun_fi->stack_offset, CodeOperand<uint32_t>(&pCodeBase[ip]));
        if (pVar == nullptr)
        {
            SetError("Local variable not found");
            return nullptr;
        }
        return pVar;
    case AX:
        return &rAX;
    case BX:
        return &rBX;
    case EX:
        return &ExpressionResult;
    case AP:
        return nullptr;
    default:
        SetError("invalid operand");
        return nullptr;
    }
    return nullptr;
}

int COMPILER::CallDepth(const char *func_name) const
{
    auto calls = callStack_;
    for (int depth = 0; !calls.empty(); ++depth, calls.pop())
        if (storm::iEquals(std::get<2>(calls.back()), func_name))
            return depth;
    return -1;
}

DATA *COMPILER::CallArgument(const char *func_name, uint32_t index)
{
    auto calls = callStack_;
    for (; !calls.empty(); calls.pop())
    {
        const auto &[file, line, name, offset, count] = calls.back();
        if (!storm::iEquals(name, func_name))
            continue;
        if (offset == kNoArguments || index >= count || offset + index >= SStack.GetDataNum())
            return nullptr;
        return SStack.Read(offset, index);
    }
    return nullptr;
}

bool COMPILER::ScriptCallInTime()
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                              scriptCallStart_).count();
    if (elapsed <= scriptCallTimeLimitMs_)
        return true;
    SetError("script call stopped after %lld ms (limit %u ms, engine.ini [script] call_time_limit): an endless loop?",
             static_cast<long long>(elapsed), scriptCallTimeLimitMs_);
    CollectCallStack();
    scriptCallAborted_ = true;
    return false;
}

void COMPILER::CollectCallStack() const
{
    logStack_->trace("Call stack:");
    auto callStackCopy = callStack_;
    while (!callStackCopy.empty())
    {
        const auto &record = callStackCopy.back();

        const auto &filename = std::get<0>(record);
        const auto &line = std::get<1>(record);
        const auto &name = std::get<2>(record);

        if (filename[0] == '\0')
        {
            logStack_->trace("{} (EVENT)", name);
        }
        else if (strcmp(filename, "engine") == 0)
        {
            logStack_->trace("{} (INTERNAL FUNCTION)", name);
        }
        else
        {
            logStack_->trace("{} (FUNCTION) at {}:{}", name, filename, line);
        }

        callStackCopy.pop();
    }
}

void COMPILER::LoadVariablesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto vi = VarInfo();

        vi.segment_id = segment.id;
        vi.name = reader.ReadArray();
        vi.type = reader.Read<decltype(vi.type)>();
        vi.elements = reader.Read<decltype(vi.elements)>();
        vi.array = reader.Read<bool>();

        const auto var_code = VarTab.AddVar(vi);
        if (var_code == INVALID_VAR_CODE)
        {
            // Name taken by a segment loaded since the entry was written; the
            // compiler reports it as a duplicate with file and line.
            throw storm::script_cache::ReaderException();
        }
        const auto real_var = VarTab.GetVarX(var_code);

        if (!vi.IsArray())
        {
            ReadScriptData(reader, vi.type, real_var->value.get());
        }
        else
        {
            for (size_t j = 0; j < vi.elements; ++j)
            {
                ReadScriptData(reader, vi.type, real_var->value->GetArrayElement(j));
            }
        }
    }
}

void COMPILER::LoadFunctionsFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto fi = FuncInfo();
        fi.segment_id = segment.id;

        fi.name = reader.ReadArray();
        fi.offset = reader.Read<decltype(fi.offset)>();
        fi.return_type = reader.Read<decltype(fi.return_type)>();
        fi.decl_file_name = reader.ReadArray();
        fi.decl_line = reader.Read<decltype(fi.decl_line)>();
        fi.native = reader.Read<bool>();
        fi.varargs = reader.Read<bool>();
        fi.default_args.resize(reader.Read<size_t>());
        for (auto &value : fi.default_args)
        {
            value.type = reader.Read<S_TOKEN_TYPE>();
            value.int_value = reader.Read<int32_t>();
            value.float_value = reader.Read<float>();
            value.string_value = reader.ReadArray();
        }

        auto func_code = FuncTab.AddFunc(fi);

        // args
        auto count = reader.Read<size_t>();
        for (size_t j = 0; j < count; ++j)
        {
            auto lvi = LocalVarInfo();

            lvi.name = reader.ReadArray();
            lvi.type = reader.Read<decltype(lvi.type)>();
            lvi.elements = reader.Read<decltype(lvi.elements)>();
            lvi.array = reader.Read<bool>();

            auto is_extern = reader.Read<bool>();

            FuncTab.AddFuncArg(func_code, lvi, is_extern);
        }

        // local variables
        count = reader.Read<size_t>();
        for (size_t j = 0; j < count; ++j)
        {
            auto lvi = LocalVarInfo();

            lvi.name = reader.ReadArray();
            lvi.type = reader.Read<decltype(lvi.type)>();
            lvi.elements = reader.Read<decltype(lvi.elements)>();
            lvi.array = reader.Read<bool>();

            FuncTab.AddFuncVar(func_code, lvi);
        }
    }
}

void COMPILER::LoadScriptLibrariesFromCache(storm::script_cache::BufferReader &reader)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto name = std::string(reader.ReadArray());

        auto cls = core_internal.FindVMA(name.c_str());
        if (!cls)
        {
            SetWarning("cant load library '%s'", name.c_str());
            continue;
        }

        auto lib = static_cast<SCRIPT_LIBRIARY *>(cls->CreateClass());
        if (lib)
        {
            lib->Init();
        }

        LibriaryFuncs.emplace_back(lib, name.c_str());
    }
}

void COMPILER::LoadEventHandlersFromCache(storm::script_cache::BufferReader &reader)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto event = std::string(reader.ReadArray());
        auto name = std::string(reader.ReadArray());

        SetEventHandler(event.c_str(), name.c_str(), 0, true);
    }
}

void COMPILER::LoadByteCodeFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto code = reader.ReadArray();
    segment.code.assign(code.begin(), code.end());

    // relocations data
    auto strings = std::unordered_map<uint32_t, std::string>();
    auto variables = std::unordered_map<uint32_t, std::string>();
    auto functions = std::unordered_map<uint32_t, std::string>();

    auto size = reader.Read<size_t>();
    strings.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto string_code = reader.Read<uint32_t>();
        auto string = reader.ReadArray();
        strings.emplace(string_code, string);
    }

    size = reader.Read<size_t>();
    variables.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto variable_code = reader.Read<uint32_t>();
        auto name = reader.ReadArray();
        variables.emplace(variable_code, name);
    }

    size = reader.Read<size_t>();
    functions.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        auto function_code = reader.Read<uint32_t>();
        auto name = reader.ReadArray();
        functions.emplace(function_code, name);
    }

    pRunCodeBase = segment.code.data();
    InstructionPointer = 0;
    S_TOKEN_TYPE token_type = END_OF_PROGRAMM;
    do
    {
        token_type = BC_TokenGet();
        if (token_type != ACCESS_WORD_CODE && token_type != VARIABLE && token_type != CALL_FUNCTION)
            continue; // no operand to relocate (the last token has none)
        const auto cached = CodeOperand<uint32_t>(&pRunCodeBase[TLR_DataOffset]);
        uint32_t relocated;
        if (token_type == ACCESS_WORD_CODE)
            relocated = SCodec.Convert(strings[cached].c_str());
        else if (token_type == VARIABLE)
            relocated = VarTab.FindVar(variables[cached]);
        else
            relocated = FuncTab.FindFunc(functions[cached]);
        std::memcpy(segment.code.data() + TLR_DataOffset, &relocated, sizeof(relocated));
    } while (token_type != END_OF_PROGRAMM);
}

std::filesystem::path COMPILER::GetSegmentCachePath(const SEGMENT_DESC &segment) const
{
    auto path = CacheFolder() / segment.name;
    path.replace_extension(".b");
    return path;
}

void COMPILER::SaveVariablesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.variables.size());
    for (auto &vi : script_cache_.variables)
    {
        writer.WriteArray(vi.name);
        writer.WriteData(vi.type);
        writer.WriteData(vi.elements);
        writer.WriteData(vi.array);

        if (!vi.IsArray())
        {
            WriteScriptData(writer, vi.type, vi.value.get());
        }
        else
        {
            for (size_t i = 0; i < vi.elements; ++i)
            {
                WriteScriptData(writer, vi.type, vi.value->GetArrayElement(i));
            }
        }
    }
}

void COMPILER::SaveFunctionsToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.functions.size());
    for (auto &[fi, arguments, local_variables] : script_cache_.functions)
    {
        writer.WriteArray(fi.name);
        writer.WriteData(fi.offset);
        writer.WriteData(fi.return_type);
        writer.WriteArray(fi.decl_file_name);
        writer.WriteData(fi.decl_line);
        writer.WriteData(fi.native);
        writer.WriteData(fi.varargs);
        writer.WriteData(fi.default_args.size());
        for (const auto &value : fi.default_args)
        {
            writer.WriteData(value.type);
            writer.WriteData(value.int_value);
            writer.WriteData(value.float_value);
            writer.WriteArray(value.string_value);
        }

        writer.WriteData(arguments.size());
        for (auto &[lvi, is_extern] : arguments)
        {
            writer.WriteArray(lvi.name);
            writer.WriteData(lvi.type);
            writer.WriteData(lvi.elements);
            writer.WriteData(lvi.array);
            writer.WriteData(is_extern);
        }

        writer.WriteData(local_variables.size());
        for (auto &lvi : local_variables)
        {
            writer.WriteArray(lvi.name);
            writer.WriteData(lvi.type);
            writer.WriteData(lvi.elements);
            writer.WriteData(lvi.array);
        }
    }
}

void COMPILER::SaveScriptLibrariesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.script_libs.size());
    for (auto &lib_name : script_cache_.script_libs)
    {
        writer.WriteArray(lib_name);
    }
}

void COMPILER::SaveEventHandlersToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.event_handlers.size());
    for (auto &[event, handler] : script_cache_.event_handlers)
    {
        writer.WriteArray(event);
        writer.WriteArray(handler);
    }
}

void COMPILER::SaveByteCodeToCache(storm::script_cache::BufferWriter &writer, const SEGMENT_DESC &segment)
{
    auto code = std::string_view(segment.code.data(), segment.code.size());
    writer.WriteArray(code);

    // relocations
    auto strings = std::unordered_map<uint32_t, std::string>();
    auto variables = std::unordered_map<uint32_t, std::string>();
    auto functions = std::unordered_map<uint32_t, std::string>();

    pRunCodeBase = segment.code.data();
    InstructionPointer = 0;
    auto token_type = S_TOKEN_TYPE();
    do
    {
        token_type = BC_TokenGet();
        if (token_type != ACCESS_WORD_CODE && token_type != VARIABLE && token_type != CALL_FUNCTION)
            continue; // no operand to relocate (the last token has none)
        const auto relocated_data = CodeOperand<uint32_t>(&pRunCodeBase[TLR_DataOffset]);

        if (token_type == ACCESS_WORD_CODE)
        {
            strings.emplace(relocated_data, SCodec.Convert(relocated_data));
        }
        else if (token_type == VARIABLE)
        {
            auto vi = VarTab.GetVarX(relocated_data);
            variables.emplace(relocated_data, vi->name);
        }
        else if (token_type == CALL_FUNCTION)
        {
            auto fi = FuncInfo();
            FuncTab.GetFuncX(fi, relocated_data);
            functions.emplace(relocated_data, fi.name);
        }
    } while (token_type != END_OF_PROGRAMM);

    writer.WriteData(strings.size());
    for (auto &[string_code, string] : strings)
    {
        writer.WriteData(string_code);
        writer.WriteArray(string);
    }

    writer.WriteData(variables.size());
    for (auto &[variable_code, name] : variables)
    {
        writer.WriteData(variable_code);
        writer.WriteArray(name);
    }

    writer.WriteData(functions.size());
    for (auto &[function_code, name] : functions)
    {
        writer.WriteData(function_code);
        writer.WriteArray(name);
    }
}

void COMPILER::SaveSegmentToCache(const SEGMENT_DESC &segment)
{
    storm::script_cache::BufferWriter writer;

    // defines (in case of new compiled code depending on defines from cache)
    SaveDefinesToCache(writer);

    // variables with values
    SaveVariablesToCache(writer);

    // functions
    SaveFunctionsToCache(writer);

    // script libs
    SaveScriptLibrariesToCache(writer);

    // event handlers
    SaveEventHandlersToCache(writer);

    // bytecode
    SaveByteCodeToCache(writer, segment);

    const auto data = writer.GetDataBuffer();
    if (const auto error = storm::script_cache::writeEntry(GetSegmentCachePath(segment),
                                                           std::string_view(std::data(data), std::size(data)));
        !error.empty())
    {
        spdlog::warn("Script cache: {}; the segment is compiled again next start", error);
    }
}

void COMPILER::SaveDefinesToCache(storm::script_cache::BufferWriter &writer)
{
    writer.WriteData(script_cache_.defines.size());
    for (auto &[name, type, value] : script_cache_.defines)
    {
        writer.WriteArray(name);
        writer.WriteData(type);
        switch (type)
        {
        case NUMBER:
            writer.WriteData(static_cast<int32_t>(value));
            break;

        case FLOAT_NUMBER: {
            // data4b holds the float's bit pattern (Compile), not a number to convert.
            float bits;
            memcpy(&bits, &value, sizeof(bits));
            writer.WriteData(bits);
            break;
        }

        case STRING:
            writer.WriteArray(reinterpret_cast<const char *>(value));
            break;
        }
    }
}

bool COMPILER::LoadSegmentFromCache(SEGMENT_DESC &segment)
{
    const auto path = GetSegmentCachePath(segment);
    std::vector<char> data;
    switch (storm::script_cache::readEntry(path, data))
    {
    case storm::script_cache::Entry::Missing:
        return false;
    case storm::script_cache::Entry::Damaged: {
        spdlog::warn("Script cache entry '{}' is damaged; recompiling", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return false;
    }
    case storm::script_cache::Entry::Read:
        break;
    }

    storm::script_cache::BufferReader reader(std::move(data));

    try
    {
        // defines (in case of new compiled code depending on defines from cache)
        LoadDefinesFromCache(reader, segment);

        // variables with values
        LoadVariablesFromCache(reader, segment);

        // functions
        LoadFunctionsFromCache(reader, segment);

        // script libraries
        LoadScriptLibrariesFromCache(reader);

        // event handlers
        LoadEventHandlersFromCache(reader);

        // bytecode
        LoadByteCodeFromCache(reader, segment);
    }
    catch (const storm::script_cache::ReaderException &)
    {
        // Intact file that does not parse or apply: a layout bug or a name clash,
        // not disk damage. Undo what was registered for this segment and compile from source.
        spdlog::error("Script cache entry '{}' cannot be applied; recompiling", path.string());
        std::error_code ec;
        std::filesystem::remove(path, ec);
        segment.code.clear();
        FuncTab.InvalidateBySegmentID(segment.id);
        VarTab.InvalidateBySegmentID(segment.id);
        EventTab.InvalidateBySegmentID(segment.id);
        DefTab.InvalidateBySegmentID(segment.id);
        return false;
    }

    return true;
}

void COMPILER::LoadDefinesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment)
{
    const auto size = reader.Read<size_t>();
    for (size_t i = 0; i < size; ++i)
    {
        auto di = DEFINFO();

        di.segment_id = segment.id;
        auto name = std::string(reader.ReadArray());
        di.name = const_cast<char *>(name.c_str());
        di.deftype = reader.Read<uint32_t>();

        switch (di.deftype)
        {
        case NUMBER:
            di.data4b = static_cast<uintptr_t>(reader.Read<int32_t>());
            break;

        case FLOAT_NUMBER: {
            const float bits = reader.Read<float>();
            di.data4b = 0;
            memcpy(&di.data4b, &bits, sizeof(bits));
            break;
        }

        case STRING: {
            auto value = reader.ReadArray();
            const auto new_ptr = new char[value.size() + 1];
            std::ranges::copy(value, new_ptr);
            new_ptr[value.size()] = '\0';
            di.data4b = reinterpret_cast<uintptr_t>(new_ptr);
            break;
        }
        }

        DefTab.AddDef(di);
    }
}

void STRING_CODEC::VariableChanged()
{
}
