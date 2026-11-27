#pragma once

// The localisation natives (natives_ext_list.h): the call runs here, the texts and templates are the string
// service's (script_texts.hpp).

#include "natives_ext.h"

namespace storm::natives
{
enum class TextNative
{
    Dlgo,
    Convert,
    ConvertE,
    ReplaceAllMatches,
    StringFromKey,
};

// Consumes the call's arguments; returns the pushed string result, nullptr for DLG_ReplaceAllMatches.
DATA *callTextNative(TextNative native, NativeCall &call, DATA *&result);
} // namespace storm::natives
