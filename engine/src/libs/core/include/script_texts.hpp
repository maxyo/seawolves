#pragma once

// The localisation natives DLGO, DLG_Convert, DLG_ConvertE and StringFromKey: core runs the call and answers what a
// template reads from the running scripts (ScriptAccess); the string service owns the texts and the template language
// (TextTemplates) and installs itself while it exists.

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace storm::texts
{
// The objects a template reads: the player (`pchar`), the dialogue partner (`CharacterRef`) and the object passed
// to the native ("context").
enum class Who
{
    Player,
    Npc,
    Context,
};

// An argument of a script function a template calls: text, converted to the parameter's type, or a reference.
struct ScriptArg
{
    enum class Kind
    {
        Text,
        Player,
        Npc,
        Context,
    };
    Kind kind = Kind::Text;
    std::string text;
};

class ScriptAccess
{
  public:
    virtual ~ScriptAccess() = default;
    // the value of an attribute (dotted path); nullopt when the object or the attribute does not exist
    virtual std::optional<std::string> Attribute(Who who, std::string_view path) = 0;
    // runs a script function and returns its result as text; nullopt when no script defines it
    virtual std::optional<std::string> Call(std::string_view function, std::span<const ScriptArg> args) = 0;
    // a script #define, or a global variable with an optional attribute path ("var.attr.sub"), as text; nullopt
    // when there is none
    virtual std::optional<std::string> Value(std::string_view name) = 0;
};

class TextTemplates
{
  public:
    virtual ~TextTemplates() = default;
    // DLGO
    virtual std::string Expand(std::string_view text, ScriptAccess &scripts) = 0;
    // DLG_Convert (a missing key is reported and gives the key) and DLG_ConvertE (a missing key gives "")
    virtual std::string Convert(std::string_view file, std::string_view key, bool empty_if_missing,
                                ScriptAccess &scripts) = 0;
    // StringFromKey: the value of a key with its @<...> placeholders replaced by `args` in order
    virtual std::string StringFromKey(std::string_view key, std::span<const std::string> args) = 0;
};

// nullptr while no string service exists: the natives then fail loudly
void InstallTextTemplates(TextTemplates *templates);

// The game language changed: scripts compiled from now on include its `%language%` files.
void SetScriptLanguage(std::string language);
} // namespace storm::texts
