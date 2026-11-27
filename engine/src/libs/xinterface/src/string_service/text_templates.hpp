#pragma once

// The template language of Caribbean Legend's texts: `$name`, `$name(a|b)`, `#path#`, `\x` and the lower-case
// marker U+21B4 in the values of resource/ini/texts/<language>/**.txt. README.md beside this file describes it.

#include "script_texts.hpp"

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace storm::strings
{
// Where the templates find their texts (the string service; a test's table).
class TemplateTexts
{
  public:
    virtual ~TemplateTexts() = default;
    // the raw value of `key` in a text file of the current language ("Dialogs\Roger.txt"); an empty `file` is the
    // common strings. nullopt when the file or the key is missing
    virtual std::optional<std::string> Value(std::string_view file, std::string_view key) = 0;
    // the raw value of a StringFromKey key (the files under Localization_Assets)
    virtual std::optional<std::string> KeyValue(std::string_view key) = 0;
    // the directory of the current language, as language.ini names it
    virtual std::string_view Language() = 0;
    // uniform in [0, bound)
    virtual int Random(int bound) = 0;
    // a construct the templates cannot expand; called once per distinct problem
    virtual void Report(std::string_view problem) = 0;
};

class TextTemplateLanguage final : public texts::TextTemplates
{
  public:
    static constexpr int kMaxDepth = 16; // $key and $ctx values are templates too: a value naming itself stops here

    explicit TextTemplateLanguage(TemplateTexts &texts) : texts_(texts)
    {
    }

    std::string Expand(std::string_view text, texts::ScriptAccess &scripts) override;
    std::string Convert(std::string_view file, std::string_view key, bool empty_if_missing,
                        texts::ScriptAccess &scripts) override;
    std::string StringFromKey(std::string_view key, std::span<const std::string> args) override;

  private:
    struct Run;
    struct Construct;

    void ExpandInto(Run &run, std::string &out, std::string_view text, int depth);
    size_t ExpandConstruct(Run &run, std::string &out, std::string_view text, size_t start, int depth);
    size_t ExpandAttribute(Run &run, std::string &out, std::string_view text, size_t start);
    bool Builtin(Run &run, std::string &out, const Construct &c, int depth);
    bool ScriptFunction(Run &run, std::string &out, const Construct &c, int depth);
    std::string ExpandInFile(Run &run, std::string_view value, std::string_view file, int depth);
    void Report(std::string problem);

    TemplateTexts &texts_;
    std::set<std::string> reported_;
};

// Lower-cases the first letter at or after `from`, past blanks and [tag] markup; stops at a digit.
void LowerFirstLetter(std::string &text, size_t from);
} // namespace storm::strings
