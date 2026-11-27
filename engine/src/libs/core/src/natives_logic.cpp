#include "natives_logic.hpp"

#include "fnv_hash.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

#include <fmt/args.h>
#include <fmt/printf.h>

namespace storm::natives
{
int32_t hashText(std::string_view text)
{
    const uint64_t hash = storm::hash::fnv1a(text);
    return static_cast<int32_t>(static_cast<uint32_t>(hash ^ (hash >> 32)));
}

int32_t hashRandom(int32_t n, std::string_view global_seed, std::string_view call_site, std::string_view tag)
{
    if (n <= 0)
        return 0;
    uint64_t h = storm::hash::fnv1a(global_seed);
    h = storm::hash::fnvRound(h, 0x1f);
    if (tag.empty() || tag.front() != '&')
        h = storm::hash::fnvRound(storm::hash::fnv1a(call_site, h), 0x1f);
    h = storm::hash::fnv1a(tag, h);
    // FNV's low bits follow its last bytes: mix, so that neighbouring call sites do not give neighbouring numbers
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return static_cast<int32_t>(h % (static_cast<uint64_t>(n) + 1));
}

bool parseSortType(std::string_view name, SortType &type)
{
    if (name == "int")
        type = SortType::Int;
    else if (name == "float")
        type = SortType::Float;
    else if (name == "string")
        type = SortType::String;
    else if (name == "date")
        type = SortType::Date;
    else
        return false;
    return true;
}

SortKey makeSortKey(std::string_view value, int32_t offset, SortType type)
{
    SortKey key;
    const std::string text(value.substr(std::min(value.size(), static_cast<size_t>(std::max(offset, 0)))));
    switch (type)
    {
    case SortType::Int:
        key.number = static_cast<double>(std::strtoll(text.c_str(), nullptr, 10));
        break;
    case SortType::Float:
        key.number = std::strtod(text.c_str(), nullptr);
        break;
    case SortType::String:
        key.text = text;
        std::ranges::transform(key.text, key.text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        break;
    case SortType::Date: {
        // groups of digits; the year is the group of four digits (first or third), the rest keep their order
        std::vector<size_t> widths;
        for (size_t i = 0; i < text.size();)
        {
            if (!std::isdigit(static_cast<unsigned char>(text[i])))
            {
                ++i;
                continue;
            }
            size_t end = i;
            while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
                ++end;
            int32_t number = 0;
            std::from_chars(text.data() + i, text.data() + end, number);
            key.date.push_back(number);
            widths.push_back(end - i);
            i = end;
        }
        if (key.date.size() >= 3 && widths[0] < 4 && widths[2] >= 4)
            std::swap(key.date[0], key.date[2]); // day.month.year
        break;
    }
    }
    return key;
}

bool sortKeyLess(const SortKey &lhs, const SortKey &rhs, SortType type)
{
    switch (type)
    {
    case SortType::Int:
    case SortType::Float:
        return lhs.number < rhs.number;
    case SortType::String:
        return lhs.text < rhs.text;
    case SortType::Date:
        return lhs.date < rhs.date;
    }
    return false;
}

std::string formatScript(std::string_view format, const std::vector<FormatArg> &args, std::string &error)
{
    fmt::dynamic_format_arg_store<fmt::printf_context> store;
    for (const auto &arg : args)
        std::visit([&store](const auto &value) { store.push_back(value); }, arg);
    try
    {
        return fmt::vsprintf(fmt::string_view(format.data(), format.size()), store);
    }
    catch (const fmt::format_error &failure)
    {
        error = failure.what();
        return std::string(format);
    }
}
} // namespace storm::natives
