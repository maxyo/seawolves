#pragma once

// The parts of the script natives of natives_ext.cpp that need no VM (tests/natives_ext).

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace storm::natives
{
// hash(text): a deterministic number for a text (the scripts use it as a source of randomness).
int32_t hashText(std::string_view text);

// hrand(n[, tag]): a number in [0, n] that stays the same while the global seed (SetGlobalSeed) does. Each call site
// has its own number unless the tag starts with '&': every call with that tag gives the same number.
int32_t hashRandom(int32_t n, std::string_view global_seed, std::string_view call_site, std::string_view tag);

// Sort keys of SmartSort / SmartSortTable: the value with `offset` leading characters skipped, compared as "int",
// "float", "string" (case-insensitive) or "date".
enum class SortType
{
    Int,
    Float,
    String,
    Date,
};
bool parseSortType(std::string_view name, SortType &type);

struct SortKey
{
    double number = 0.0;       // Int, Float
    std::string text;          // String: lower-cased
    std::vector<int32_t> date; // Date: year, month, day, then the remaining numbers (time)
};
SortKey makeSortKey(std::string_view value, int32_t offset, SortType type);
bool sortKeyLess(const SortKey &lhs, const SortKey &rhs, SortType type);

// Sprintf(format, ...): printf conversions. A format the arguments do not fit is returned unchanged and `error` says
// why.
using FormatArg = std::variant<int32_t, float, std::string>;
std::string formatScript(std::string_view format, const std::vector<FormatArg> &args, std::string &error);
} // namespace storm::natives
