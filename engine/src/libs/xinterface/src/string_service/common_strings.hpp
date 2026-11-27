#pragma once

#include "user_string_files.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::strings
{
// The common strings of the language (texts/<language>/common.ini) by number and by name, the name compared as
// FoldedName. A line the file cannot describe keeps its number with no name, so numbers match between languages.
// Strings merged from another file (a mod's common.ini) replace the text of a name the table has and add the others.
class CommonStrings
{
  public:
    void Clear()
    {
        names_.clear();
        texts_.clear();
        index_.clear();
    }

    void Append(std::string name, std::string text)
    {
        if (!name.empty())
            index_.emplace(FoldedName(name), static_cast<int32_t>(names_.size()));
        names_.push_back(std::move(name));
        texts_.push_back(std::move(text));
    }

    void Merge(std::string name, std::string text)
    {
        if (const auto found = index_.find(FoldedName(name)); found != index_.end())
            texts_[found->second] = std::move(text);
        else
            Append(std::move(name), std::move(text));
    }

    int32_t Find(std::string_view name) const
    {
        const auto found = index_.find(FoldedName(name));
        return found == index_.end() ? -1 : found->second;
    }

    int32_t Size() const
    {
        return static_cast<int32_t>(names_.size());
    }

    // nullptr past the end and for a line without a name
    char *Text(int32_t number)
    {
        return Named(number) ? texts_[number].data() : nullptr;
    }
    char *Name(int32_t number)
    {
        return Named(number) ? names_[number].data() : nullptr;
    }

  private:
    bool Named(int32_t number) const
    {
        return number >= 0 && number < Size() && !names_[number].empty();
    }

    std::vector<std::string> names_, texts_;
    std::unordered_map<std::string, int32_t> index_; // the first string of each name
};
} // namespace storm::strings
