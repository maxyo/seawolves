#pragma once

#include <optional>
#include <set>
#include <string_view>
#include <tuple>

#include "data.h"
#include "message.h"
#include "s_deftab.h"
#include "s_eventmsg.h"
#include "s_eventtab.h"
#include "s_functab.h"
#include "s_postevents.h"
#include "s_stack.h"
#include "s_vartab.h"
#include "script_libriary.h"
#include "string_codec.h"
#include "strings_list.h"
#include "token.h"
#include "logging.hpp"
#include "script_cache.h"
#include "platform/platform.hpp"

#include "platform/frame_work.hpp"
#include "ringbuffer_stack.hpp"
#include "script_program.hpp"

#include <chrono>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

#define IOBUFFER_SIZE 65535

struct SEGMENT_DESC
{
    std::string name;
    uint32_t offset;
    uint32_t size;
    uint32_t id;
    uint32_t lines;
    bool bUnload;
    bool attached = false; // loaded by the engine for itself (COMPILER::Attach): its globals never enter a save
    std::vector<char> code; // the bytecode

    STRINGS_LIST *Files_list;

    [[nodiscard]] uint32_t CodeSize() const
    {
        return static_cast<uint32_t>(code.size());
    }
    // Writes the target of a jump compiled before its target was known.
    void SetOffset(uint32_t at, uint32_t offset)
    {
        std::memcpy(&code[at], &offset, sizeof(offset));
    }
};
// A script can load a segment while another one runs from its bytecode: when SegmentTable grows, a segment must move
// (keeping its buffer), not be copied.
static_assert(std::is_nothrow_move_constructible_v<SEGMENT_DESC>);

struct OFFSET_INFO
{
    uint32_t offset;
    uint32_t segment_id;
};


struct DOUBLE_DWORD
{
    uint32_t dw1;
    uint32_t dw2;
};

class SLIBHOLDER
{
  public:
    std::unique_ptr<SCRIPT_LIBRIARY> library;
    std::string name;

    SLIBHOLDER(SCRIPT_LIBRIARY *library, std::string name)
        : library(library), name(std::move(name))
    {
    }
};


enum COMPILER_STAGE
{
    CS_SYSTEM,
    CS_COMPILATION,
    CS_RUNTIME
};

class CoreImpl;

namespace storm::save
{
class ReferenceIndex;
struct ParsedState;
}

class COMPILER : public VIRTUAL_COMPILER, public storm::script::Program
{
    friend CoreImpl;

  public:
    bool bBreakOnError;

    COMPILER();
    ~COMPILER();

    VSTRING_CODEC *GetVSC()
    {
        return &SCodec;
    }

    // Appends the text of a script file and a line break, which keeps the tokens of two files apart. False when the
    // file cannot be read.
    bool AppendFile(std::string &program, const char *file_name);
    std::string SourcePath(const char *file_name) const;
    void DTrace(const char *data_PTR, ...);
    void SetError(const char *data_PTR, ...);
    void SetWarning(const char *data_PTR, ...);
    bool CreateProgram(const char *file_name);
    bool AddProgramFile(const char *file_name);

    void UnloadSegment(const char *segment_name);
    uint32_t GetSegmentIndex(uint32_t segment_id);

    void ProcessFrame(uint32_t DeltaTime);
    bool Run();
    void Release();
    void SetProgramDirectory(const char *dir_name);
    VDATA *ProcessEvent(const char *event_name, MESSAGE message);
    VDATA *ProcessEvent(const char *event_name);
    void SetEventHandler(const char *event_name, const char *func_name, int32_t flag, bool bStatic = false);
    void DelEventHandler(const char *event_name, const char *func_name);
    bool HasActiveEventHandler(const char *event_name, const char *func_name);
    // The running call of the script function `func_name` nearest the top of the call stack:
    // its depth (0 = the innermost call), or -1 when it is not running; and argument `index`
    // of that call (nullptr when it has fewer). For tagging what a script does at its source
    // (the touch UI: which game function wrote a log line).
    int CallDepth(const char *func_name) const;
    DATA *CallArgument(const char *func_name, uint32_t index);

    bool Completed()
    {
        return bCompleted;
    }

    void ExitProgram();
    void ClearEvents();
    // From a native function the running script calls: every script frame of the current call stack returns at
    // once (no further script statement runs); the outermost frame drops their stack values. No-op outside scripts.
    void AbortExecution()
    {
        if (execute_depth_)
            abort_execution_ = true;
    }

    DATA *BC_CallIntFunction(uint32_t func_code, DATA *&pVResult, uint32_t arguments);

    uint32_t GetIntFunctionsNum();
    bool InitInternalFunctions();

    bool Compile(SEGMENT_DESC &Segment);
    void CompileToken(SEGMENT_DESC &Segment, S_TOKEN_TYPE Token_type, uint32_t data_blocks_num = 0, ...);
    bool BC_LoadSegment(const char *file_name);
    bool BC_SegmentIsLoaded(const char *file_name);
    // passed_args: number of arguments the caller pushed (kArgsAsDeclared: as many as declared)
    static constexpr uint32_t kArgsAsDeclared = 0xffffffff;
    bool BC_Execute(uint32_t function_code, DATA *&pVReturnResult, const char *pDbgExpSource = nullptr,
                    uint32_t passed_args = kArgsAsDeclared);
    bool BindPassedArguments(const FuncInfo &fi, FuncFrame &frame, uint32_t passed_args);
    bool BC_CallFunction(uint32_t func_code, uint32_t &ip, DATA *&pVResult);
    // Value of an integer #define of a loaded script segment; nullopt when there is none.
    std::optional<int32_t> FindIntegerDefine(const char *name);
    void FindErrorSource();
    S_TOKEN_TYPE BC_TokenGet(uint32_t &ip, uint32_t &token_data_size);
    S_TOKEN_TYPE BC_TokenGet();
    void ReadCondition(int32_t &value);

    S_TOKEN_TYPE TokenLastReadResult;
    uint32_t execute_depth_ = 0;  // nested BC_Execute frames

  private:
    // "script work:" in the log every 30 s: where the script time of a frame comes from, and the segments read from the
    // script cache. Every event raised while no script runs is timed whole (the events and natives it calls included),
    // by its name and by the frame phase it was raised from (CoreImpl::Event; an event the VM raises itself, a posted
    // one, counts as "script").
    struct EventWork
    {
        uint64_t calls = 0;
        int64_t ns = 0;
        int64_t max_ns = 0;
    };
    std::unordered_map<uint64_t, EventWork> event_work_; // event code | phase << 32
    storm::platform::frame_work::Phase event_origin_ = storm::platform::frame_work::Phase::Script;
    std::chrono::steady_clock::time_point work_window_ = std::chrono::steady_clock::now();
    uint32_t work_frames_ = 0;
    struct CacheLoads
    {
        uint32_t count = 0;
        int64_t ms = 0, slowest_ms = 0;
        std::string slowest;
    } cache_loads_;
    // Every handler of the event is in an attached segment (the touch interface's events).
    bool AttachedEvent(const EVENTINFO &event) const;

  public:
    // The "script work:" line now (ProcessFrame writes it every 30 s), and a new window.
    void ReportScriptWork();
    void SetEventOrigin(storm::platform::frame_work::Phase phase)
    {
        if (execute_depth_ == 0)
            event_origin_ = phase;
    }
    bool abort_execution_ = false; // AbortExecution: unwinding the current script call stack
    uint32_t TLR_DataOffset;
    const char *pRunCodeBase;
    S_TOKEN_TYPE TokenType()
    {
        return TokenLastReadResult;
    }

    S_TOKEN_TYPE SkipAuxiliaryTokens();
    S_TOKEN_TYPE NextTokenType();
    bool SkipToTopLevel(uint32_t depth);
    static bool IsIdentifier(const char *text);
    static std::string LowerName(const std::string &name);
    static std::string LocalSlotName(const std::string &name, uint32_t occurrence);
    static std::string LabelKey(const std::string &func_name, const char *label);
    static constexpr uint32_t kRuntimeArraySize = 0; // elements of an array sized at run time
    uint32_t ScanLocalArraySize();
    uint32_t ConstantArraySize();
    uint32_t SkipInitializer(bool &resumed);
    // pass 2: block scoped locals
    uint32_t FindLocal(const char *name) const;
    bool AssignValue(DATA *destination, DATA *value);
    int32_t IntValue(DATA *pV);
    static DATA *Deref(DATA *pV);
    static ATTRIBUTES *AttributesOf(DATA *pV);
    int32_t TypeidOf(DATA *pV);
    bool IsOfType(DATA *pV, int32_t test);
    bool CastValue(DATA *pV, S_TOKEN_TYPE target);
    static bool ReturnTypesMatch(S_TOKEN_TYPE declared, S_TOKEN_TYPE engine);
    const std::string &CurrentFunctionName() const;
    // script function name of a CALL_FUNCTION code (codes depend on the order functions were seen)
    const char *GetFunctionName(uint32_t code) const
    {
        const auto *fi = FuncTab.GetFuncInfo(code);
        return fi ? fi->name.c_str() : "?";
    }
    uint32_t BindLocal(const char *name, uint32_t depth);
    bool CompileLocalDeclarator(SEGMENT_DESC &Segment, uint32_t depth, bool first);
    bool CompileArrayInitializer(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code);
    void CloseLocalScopes(uint32_t depth);
    bool ScanParameterList(uint32_t func_code, bool bExtern, FuncInfo &declared);
    bool ScanDefaultArgument(S_TOKEN_TYPE param_type, DefaultArgument &value);
    static int32_t ParseIntLiteral(const char *text);
    bool CompileCallArguments(SEGMENT_DESC &Segment, uint32_t &func_args);
    // in_expression: a count mismatch only warns, as calls inside expressions were never checked
    // (TEHO has a few) and still compile as before
    bool CheckCallArguments(uint32_t func_code, uint32_t func_args, bool in_expression = false);
    bool ReportMissingNative(const std::string &name);

    uint32_t CurrentFuncCode;
    void CompileNumber(SEGMENT_DESC &Segment);
    void CompileFloatNumber(SEGMENT_DESC &Segment);
    void CompileString(SEGMENT_DESC &Segment);
    bool CompileUnknown(SEGMENT_DESC &Segment);
    bool CompileBlock(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE bound_type,
                      uint32_t continue_jump, uint32_t break_offset, STRINGS_LIST &BreakTable);
    S_TOKEN_TYPE CompileAuxiliaryTokens(SEGMENT_DESC &Segment); //, bool & bFunctionBlock, DWORD & inout);
    bool BC_Jump(SEGMENT_DESC &Segment, uint32_t offset);
    void UpdateOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &list, uint32_t offset, const char *sname = nullptr);
    S_TOKEN_TYPE DetectUnknown(uint32_t &code);
    bool NextIsOpenBracket();

    void DumpAttributes(ATTRIBUTES *pA, int32_t level);

    bool IsIntFuncVarArgsNum(uint32_t code);
    S_TOKEN_TYPE GetIntFunctionReturnType(uint32_t code);
    bool IntFuncArgumentsInRange(uint32_t code, uint32_t arguments);
    uint32_t GetInternalFunctionArgumentsNum(uint32_t code);
    bool CreateMessage(MESSAGE *pMs, uint32_t stack_offset, uint32_t vindex, bool s2s = false);
    void ProcessEvent(const char *event_name, MESSAGE *pMs);
    DATA *PushEventValue(MESSAGE &message);

    bool SaveState(std::vector<char> &image);
    std::string SaveFileInfo();
#ifdef STORM_SCENE_SWEEP
    void WriteStateDump(const char *label); // scene_sweep.cpp
#endif
    std::vector<char> SerializeState(); // the script state of a save (save_state.hpp)
    // storm::script::Program
    storm::script::Attached Attach(const char *segment) override;
    bool Executing() const override
    {
        return execute_depth_ != 0;
    }
    std::string RunningSegment() const override;
    void Abort() override
    {
        AbortExecution();
    }
    void ForEachFunction(const std::function<void(const storm::script::Function &)> &visit) const override;
    void ForEachGlobal(const std::function<void(std::string_view name)> &visit) const override;
    void ForEachEvent(const std::function<void(std::string_view name)> &visit) const override;
    std::string BindStateProgram(storm::save::ParsedState &state) const;
    bool LoadState(const std::vector<char> &raw, const storm::save::ParsedState &state);
    bool OnLoad();

    void AddPostEvent(S_EVENTMSG *pEM);
    bool EventPosted(const char *name);
    void ForgetDeletedAttributes(S_EVENTMSG *pEM);

    void LoadPreprocess();

    void CopyOffsets(SEGMENT_DESC &Segment, STRINGS_LIST &srclist, STRINGS_LIST &dstlist, const char *sname);



    void *GetSaveData(const char *file_name, int32_t &data_size);


    uint32_t SetScriptFunction(IFUNCINFO *pFuncInfo);

    bool CompileExpression(SEGMENT_DESC &Segment);
    bool CompileTernary(SEGMENT_DESC &Segment);
    bool CompileBinaryLevel(SEGMENT_DESC &Segment, bool (COMPILER::*operand)(SEGMENT_DESC &),
                            std::initializer_list<S_TOKEN_TYPE> ops, S_TOKEN_TYPE emitted_for_and);
    bool CompileBitOr(SEGMENT_DESC &Segment);
    bool CompileBitAnd(SEGMENT_DESC &Segment);
    bool CompileShift(SEGMENT_DESC &Segment);
    bool CompileCast(SEGMENT_DESC &Segment);
    int32_t TypeKeywordId();
    bool CompileTypeTest(SEGMENT_DESC &Segment);
    bool IsAttributeTypeTest();
    bool CompileTypeid(SEGMENT_DESC &Segment);
    bool CompileAttributeDefault(SEGMENT_DESC &Segment);
    bool CompileAttributeRoot(SEGMENT_DESC &Segment);
    S_TOKEN_TYPE DetectCallThrough();
    bool CompileCallThrough(SEGMENT_DESC &Segment, S_TOKEN_TYPE call_type);
    bool CompileObjectLiteral(SEGMENT_DESC &Segment);
    bool IsAccessVariable();
    bool IsPlainAttributePath();
    bool IsTextVariable(S_TOKEN_TYPE var_type, uint32_t var_code);
    bool CompileNewArray(SEGMENT_DESC &Segment);
    std::deque<DATA> newArrays_; // arrays made by "new type[n]" (scripts keep references to them)
    bool CompileChainTail(SEGMENT_DESC &Segment);
    bool IsAttributeCall();
    bool IsArefVariable(S_TOKEN_TYPE var_type, uint32_t var_code);
    bool IsIndexedChain();
    static constexpr uint32_t kAccessRead = 0, kAccessQuiet = 1, kAccessCreate = 2; // ADVANCE_AP_EXPR modes
    bool CompileAccessExpression(SEGMENT_DESC &Segment, uint32_t mode);
    void QuietChainElement(SEGMENT_DESC &Segment, uint32_t offset);
    bool CompileAttributeAddress(SEGMENT_DESC &Segment, S_TOKEN_TYPE var_type, uint32_t var_code, uint32_t mode);
    static bool IsAttributeIntrinsic(const char *name);
    bool CompileAttributeIntrinsic(SEGMENT_DESC &Segment);
    bool CompileArrayLiteral(SEGMENT_DESC &Segment);
    bool IsSlice();
    bool CompileSlice(SEGMENT_DESC &Segment, S_TOKEN_TYPE source, uint32_t source_code);
    uint32_t FindMethod(DATA *self, const char *method);
    bool ScanObjectLiteral(ATTRIBUTES *object);
    bool PeekRangeFor(S_TOKEN_TYPE &loop_type);
    S_TOKEN_TYPE PeekNextToken();
    bool CompileRangeFor(SEGMENT_DESC &Segment, bool &bFunctionBlock, uint32_t &inout, S_TOKEN_TYPE loop_type,
                         STRINGS_LIST &BreakUpdateTable);
    static constexpr const char *kRangeContainer = "#rangecontainer"; // hidden locals of a range-for
    static constexpr const char *kRangePosition = "#rangeposition";
    S_TOKEN_TYPE NextScanToken();
    // typeid values
    static constexpr int32_t kTypeidInt = 1, kTypeidString = 2, kTypeidFloat = 3, kTypeidAref = 4, kTypeidObject = 5,
                             kTypeidRef = 6, kTypeidBool = 7, kTypeidFref = 8;
    static constexpr int32_t kTypeTestArray = 0x100; // OP_IS_TYPE: the typeid of the elements of an array
    bool CompileExpression_L0(SEGMENT_DESC &Segment);
    bool CompileExpression_L1(SEGMENT_DESC &Segment);
    bool CompileExpression_L2(SEGMENT_DESC &Segment);
    bool CompileExpression_L3(SEGMENT_DESC &Segment);
    bool CompileExpression_L4(SEGMENT_DESC &Segment);
    bool CompileExpression_L5(SEGMENT_DESC &Segment);
    bool CompileExpression_L6(SEGMENT_DESC &Segment);
    bool CompileExpression_L7(SEGMENT_DESC &Segment);

    DATA *GetOperand(const char *pCodeBase, uint32_t &ip, S_TOKEN_TYPE *pTokenType = nullptr);

    // writes down current script stack (internal functions+script functions+events) to logfile
    // currently used for collecting additional crash info
    // TODO: use it for internal errors also
    void CollectCallStack() const;

    // printout script functions usage

    // Host compile check (src/apps/script_check): every compile diagnostic is also appended to
    // `sink`; source line tracking is on, the script cache is off and a segment that fails to
    // compile keeps its declarations, so the segments compiled after it are checked against them.
    // missing_natives_are_errors: false for a game check (no engine library is linked), true for
    // script tests (as in the game)
    void ConfigureCompileCheck(std::vector<std::string> *sink, bool missing_natives_are_errors);
    // The script tests' endless loops stop after fewer instructions than kScriptCallLimit.
    void SetScriptCallLimit(uint64_t instructions)
    {
        scriptCallLimit_ = instructions;
    }
    // Why no script runs any more ("" while they run): a call ran over its limit and was cut midway, so the
    // program's state is half-made. Nothing runs or is saved after it; the core ends the session.
    const std::string &ScriptsStopped() const
    {
        return scriptsStopped_;
    }
    // script tests: store and load the script cache in this folder (also checks its integrity)
    void UseScriptCache(std::filesystem::path folder);
    // whether the last BC_LoadSegment took the segment from the script cache
    bool LastSegmentFromCache() const
    {
        return lastSegmentFromCache_;
    }
    // the game language for %language% in include paths (CL); by default language.ini's default
    // language, the one the string service starts with
    void SetScriptLanguage(std::string language)
    {
        scriptLanguage_ = std::move(language);
        cacheKeyCalculated_ = false; // segments compiled from now on include that language's files
    }
    // bytecode of a loaded segment, nullptr if it is not loaded
    const char *GetSegmentCode(const char *segment_name, uint32_t &size) const;
    // names a loaded segment defines (functions, globals, defines): script_check retries a segment
    // that uses them with that segment loaded, as the game loads segments together
    std::vector<std::string> GetSegmentSymbols(const char *segment_name);
    // runs a script function that takes no arguments; `value` receives its result as text
    bool RunScriptFunction(const char *func_name, std::string &value);
    // attribute name of an ACCESS_WORD_CODE (codes depend on the order names were first seen)
    const char *GetAccessWord(uint32_t code)
    {
        return SCodec.Convert(code);
    }

private:
    void AttachEngineSegments(); // the Attacher's, after Main or OnLoad
#ifdef STORM_SCENE_SWEEP
    void LoadSceneSweep(); // scene_sweep.cpp
#endif
    [[nodiscard]] std::filesystem::path GetSegmentCachePath(const SEGMENT_DESC &segment) const;

    bool LoadSegmentFromCache(SEGMENT_DESC &segment);
    void LoadDefinesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadVariablesFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadFunctionsFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);
    void LoadScriptLibrariesFromCache(storm::script_cache::BufferReader &reader);
    void LoadEventHandlersFromCache(storm::script_cache::BufferReader &reader);
    void LoadByteCodeFromCache(storm::script_cache::BufferReader &reader, SEGMENT_DESC &segment);

    void SaveSegmentToCache(const SEGMENT_DESC &segment);
    void SaveDefinesToCache(storm::script_cache::BufferWriter &writer);
    void SaveVariablesToCache(storm::script_cache::BufferWriter &writer);
    void SaveFunctionsToCache(storm::script_cache::BufferWriter &writer);
    void SaveScriptLibrariesToCache(storm::script_cache::BufferWriter &writer);
    void SaveEventHandlersToCache(storm::script_cache::BufferWriter &writer);
    void SaveByteCodeToCache(storm::script_cache::BufferWriter &writer, const SEGMENT_DESC &segment);

    COMPILER_STAGE CompilerStage;
    STRINGS_LIST LabelTable;
    STRINGS_LIST LabelUpdateTable;
    TOKEN Token;
    MESSAGE *pEventMessage;
    std::vector<SEGMENT_DESC> SegmentTable;
    uint32_t SegmentsNum;
    uint32_t RunningSegmentID;
    uint32_t InstructionPointer;

    char *ProgramDirectory;
    bool bCompleted;
    bool bEntityUpdate;
    char *pDebExpBuffer;
    uint32_t nDebExpBufferSize;

    FuncFrame *pRun_fi; // running function frame (scalar snapshot + stable table entry)
    FuncTable FuncTab;
    VarTable VarTab;
    S_DEFTAB DefTab;
    S_STACK SStack;
    S_EVENTTAB EventTab;
    POSTEVENTS_LIST EventMsg;
    std::vector<SLIBHOLDER> LibriaryFuncs;

    STRING_CODEC SCodec;


    bool bFirstRun;
    bool bScriptTrace;
    bool bWriteCodeFile;
    bool bDebugInfo;
    char DebugSourceFileName[MAX_PATH];
    char gs[MAX_PATH];
    uint32_t DebugSourceLine;


    bool bDebugExpressionRun;
    bool bTraceMode;

    bool bEventsBreak;

    char DebugTraceFileName[MAX_PATH];
    uint32_t nDebugTraceLineCode;

    uint32_t nIOBufferSize;
    uint32_t nIOFullSize;
    char *pIOBuffer;

    // script registers
    DATA rAX;
    DATA rBX;
    DATA ExpressionResult;
    ATTRIBUTES *rAP;

    // loggers
    storm::logging::logger_ptr logTrace_;
    storm::logging::logger_ptr logError_;
    storm::logging::logger_ptr logStack_;

    // backtrace stack
    // NB: pointers are safe as long as we pop elements before they expire
    static constexpr size_t CALLSTACK_SIZE = 64U;
    // file, line, name, where its arguments start on SStack (kNoArguments for an event)
    static constexpr uint32_t kNoArguments = UINT32_MAX;
    storm::ringbuffer_stack<std::tuple<const char *, size_t, const char *, uint32_t, uint32_t>, CALLSTACK_SIZE> callStack_;

    // attempt to read/write script cache?
    int script_cache_mode_;

    // see ConfigureCompileCheck
    std::vector<std::string> *diagnosticsSink_ = nullptr;
    bool compileCheck_ = false;
    // Endless loop guard (BC_Execute): the instructions one call from the engine into the scripts may execute, its
    // nested calls included. Counted in the VM's own work, so the same call stops at the same place on every
    // device and a slow disk, a debugger or a process paused in the background never trips it. About 110 times the
    // largest call measured: 19 million instructions, the start of a new game.
    static constexpr uint64_t kScriptCallLimit = uint64_t{1} << 31;
    uint64_t scriptCallLimit_ = kScriptCallLimit;
    uint32_t scriptCallDepth_ = 0;
    uint64_t scriptCallInstructions_ = 0;
    std::string scriptsStopped_;
    void StopScripts();
    std::filesystem::path cacheFolder_; // UseScriptCache; empty: the stash folder
    bool lastSegmentFromCache_ = false;
    [[nodiscard]] std::filesystem::path CacheFolder() const;
    std::string ScriptLanguageDirectory(bool report);
    bool cacheKeyCalculated_ = false; // the script cache key (scripts, build, language)
    uint64_t cacheKey_ = 0;
    std::string scriptLanguage_; // see SetScriptLanguage
    uint32_t compileErrors_ = 0; // errors reported while compiling the current segment
    bool missingNativesAreErrors_ = true;
    std::set<std::string> missingNatives_; // reported once per segment
    // pass 2: locals visible at the current point, innermost last
    struct LocalBinding
    {
        std::string name;
        uint32_t slot;
        uint32_t depth;
    };
    std::vector<LocalBinding> localScopes_;
    std::unordered_map<std::string, uint32_t> localDeclarations_; // declarations so far per name
    bool declListActive_ = false; // inside "int a, b;"
    bool declExpectName_ = false; // a ',' of that list was just compiled
    std::vector<uint32_t> *switchFallthrough_ = nullptr; // innermost switch: jumps into the next case
    std::vector<std::string> *undeclaredNatives_ = nullptr; // SetScriptFunction during a library's Init
    std::unordered_map<std::string, IFUNCINFO> pendingNatives_; // registered, not declared yet (lowercase)
    storm::ScriptCache script_cache_;
};
