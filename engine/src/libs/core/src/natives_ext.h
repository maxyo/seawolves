#pragma once

// Built-in script functions of Caribbean Legend's scripts (natives_ext_list.h). COMPILER::BC_CallIntFunction hands
// every id from FUNC_NATIVE_EXT_BASE on to callNativeExt with the VM state they need.

#include <cstdint>
#include <string_view>

class COMPILER;
class DATA;
class S_STACK;
class POSTEVENTS_LIST;

namespace storm::natives
{
struct NativeCall
{
    COMPILER &compiler;
    S_STACK &stack;                  // the arguments are its last `arguments` entries
    POSTEVENTS_LIST &posts;          // pending PostEvent messages
    std::string_view caller;         // the script function that called the native
    std::string_view callers_caller; // the function that called `caller`, empty at the top
    uint32_t instruction;            // code offset of the call: the call site within `caller`
    uint32_t arguments;
};

// `index` = function code - FUNC_NATIVE_EXT_BASE. Consumes the arguments; returns the pushed result (also stored in
// `result`) or nullptr for a void function.
DATA *callNativeExt(uint32_t index, NativeCall &call, DATA *&result);
bool isNativeExtVarArgs(uint32_t index);
} // namespace storm::natives
