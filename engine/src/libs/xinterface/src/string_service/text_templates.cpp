#include "text_templates.hpp"

#include "user_string_files.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace storm::strings
{
namespace
{
using texts::ScriptArg;
using texts::Who;

constexpr std::string_view kLowerMarker = "\xE2\x86\xB4"; // U+21B4

bool IsIdentStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c)
{
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

size_t Utf8Length(unsigned char lead)
{
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

std::optional<double> ParseNumber(std::string_view s)
{
    while (!s.empty() && IsBlank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && IsBlank(s.back()))
        s.remove_suffix(1);
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    double value = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || error != std::errc{} || end != s.data() + s.size())
        return std::nullopt;
    return value;
}

// at most two decimals, no trailing zeros: 20 -> "20", 0.376 -> "0.38"
std::string HumanNumber(double value)
{
    std::string text = fmt::format("{:.2f}", value);
    while (text.back() == '0')
        text.pop_back();
    if (text.back() == '.')
        text.pop_back();
    return text == "-0" ? "0" : text;
}

// the Slavic plural rule of the game's declension helpers: 1, 21 -> one; 2-4, 22-24 -> few; the rest -> many
std::string_view DeclensionForm(long long n, std::string_view one, std::string_view few, std::string_view many)
{
    if (many.empty())
        many = few;
    n = n < 0 ? -n : n;
    if (n % 100 >= 11 && n % 100 <= 14)
        return many;
    switch (n % 10)
    {
    case 1:
        return one;
    case 2:
    case 3:
    case 4:
        return few;
    default:
        return many;
    }
}

// The simple lower-case mapping of ASCII, Latin-1, Latin Extended-A and Cyrillic U+0400-U+042F, the letters of the
// games' European languages (the engine links no Unicode library on desktop).
char32_t LowerCode(char32_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x410 && c <= 0x42F))
        return c + 0x20;
    if (c >= 0x400 && c <= 0x40F)
        return c + 0x50;
    if (c == 0x178)
        return 0xFF;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
        return c | 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
        return c + (c & 1);
    return c;
}

enum class BuiltinId
{
    Name,
    LastName,
    FullName,
    ShipName,
    NpcName,
    NpcLastName,
    NpcFullName,
    NpcShipName,
    CtxShipName,
    City,
    Sir,
    NpcSir,
    Swear,
    Hello,
    Bye,
    Br,
    Red,
    Ctx,
    Attr,
    CtxHasAttr,
    Rand,
    ManWoman,
    NpcManWoman,
    CtxManWoman,
    GoodBad,
    NpcGoodBad,
    Key,
    Link,
    Value,
    Percent,
    Number,
    Money,
    Abs,
    Days,
    Decl,
    NationI,
    GoodI,
    ModifierPercent,
};

constexpr std::array<std::pair<std::string_view, BuiltinId>, 38> kBuiltins{{
    {"name", BuiltinId::Name},
    {"lastname", BuiltinId::LastName},
    {"fullname", BuiltinId::FullName},
    {"shipname", BuiltinId::ShipName},
    {"npcname", BuiltinId::NpcName},
    {"npclastname", BuiltinId::NpcLastName},
    {"npcfullname", BuiltinId::NpcFullName},
    {"npcshipname", BuiltinId::NpcShipName},
    {"ctxshipname", BuiltinId::CtxShipName},
    {"city", BuiltinId::City},
    {"sir", BuiltinId::Sir},
    {"npcsir", BuiltinId::NpcSir},
    {"swear", BuiltinId::Swear},
    {"hello", BuiltinId::Hello},
    {"bye", BuiltinId::Bye},
    {"br", BuiltinId::Br},
    {"red", BuiltinId::Red},
    {"ctx", BuiltinId::Ctx},
    {"attr", BuiltinId::Attr},
    {"ctxhasattr", BuiltinId::CtxHasAttr},
    {"rand", BuiltinId::Rand},
    {"manwoman", BuiltinId::ManWoman},
    {"npcmanwoman", BuiltinId::NpcManWoman},
    {"ctxmanwoman", BuiltinId::CtxManWoman},
    {"goodbad", BuiltinId::GoodBad},
    {"npcgoodbad", BuiltinId::NpcGoodBad},
    {"key", BuiltinId::Key},
    {"link", BuiltinId::Link},
    {"value", BuiltinId::Value},
    {"percent", BuiltinId::Percent},
    {"number", BuiltinId::Number},
    {"money", BuiltinId::Money},
    {"abs", BuiltinId::Abs},
    {"days", BuiltinId::Days},
    {"decl", BuiltinId::Decl},
    {"nationi", BuiltinId::NationI},
    {"goodi", BuiltinId::GoodI},
    {"modifierpercent", BuiltinId::ModifierPercent},
}};

std::optional<BuiltinId> FindBuiltin(std::string_view name)
{
    const std::string folded = AsciiLower(std::string(name));
    for (const auto &[builtin, id] : kBuiltins)
        if (builtin == folded)
            return id;
    return std::nullopt;
}

ScriptArg TextArg(std::string value)
{
    return ScriptArg{ScriptArg::Kind::Text, std::move(value)};
}

ScriptArg RefArg(ScriptArg::Kind kind)
{
    return ScriptArg{kind, {}};
}
} // namespace

void LowerFirstLetter(std::string &text, size_t from)
{
    for (size_t pos = from; pos < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[pos]);
        if (IsBlank(static_cast<char>(lead)))
        {
            ++pos;
            continue;
        }
        if (lead == '[')
            if (const size_t close = text.find(']', pos); close != std::string::npos)
            {
                pos = close + 1;
                continue;
            }
        if (lead >= '0' && lead <= '9')
            return;
        if (lead < 0x80)
        {
            if (IsIdentStart(static_cast<char>(lead)))
            {
                text[pos] = static_cast<char>(LowerCode(lead));
                return;
            }
            ++pos;
            continue;
        }
        const size_t length = std::min(Utf8Length(lead), text.size() - pos);
        if (length == 2)
        {
            const char32_t code = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[pos + 1]) & 0x3Fu);
            if (code >= 0xC0) // a letter (Latin-1 and above); lower-case it if it has a mapping
            {
                const char32_t lower = LowerCode(code);
                text[pos] = static_cast<char>(0xC0 | (lower >> 6));
                text[pos + 1] = static_cast<char>(0x80 | (lower & 0x3F));
                return;
            }
        }
        else if (length >= 3 && text.compare(pos, kLowerMarker.size(), kLowerMarker) != 0)
            return; // CJK and other scripts without case
        pos += length;
    }
}

// One native call: the objects it reads and the file whose value is being expanded ($key(k) looks there after the
// common strings).
struct TextTemplateLanguage::Run
{
    texts::ScriptAccess &scripts;
    std::string file;
};

struct TextTemplateLanguage::Construct
{
    std::string_view name;
    std::string_view raw;               // as written, kept in the output when it cannot be expanded
    std::vector<std::string_view> args; // unexpanded
};

void TextTemplateLanguage::Report(std::string problem)
{
    if (reported_.insert(problem).second)
        texts_.Report(problem);
}

std::string TextTemplateLanguage::Expand(std::string_view text, texts::ScriptAccess &scripts)
{
    Run run{scripts, {}};
    std::string out;
    ExpandInto(run, out, text, 0);
    return out;
}

std::string TextTemplateLanguage::Convert(std::string_view file, std::string_view key, bool empty_if_missing,
                                          texts::ScriptAccess &scripts)
{
    const auto value = texts_.Value(file, key);
    if (!value)
    {
        if (empty_if_missing)
            return {};
        Report(fmt::format("DLG_Convert: no key '{}' in '{}'", key, file));
        return std::string(key);
    }
    Run run{scripts, {}};
    return ExpandInFile(run, *value, file, 0);
}

std::string TextTemplateLanguage::StringFromKey(std::string_view key, std::span<const std::string> args)
{
    const auto value = texts_.KeyValue(key);
    if (!value)
    {
        Report(fmt::format("StringFromKey: no key '{}'", key));
        return std::string(key);
    }
    // each @<...> takes the next argument
    std::string out;
    size_t used = 0;
    for (size_t pos = 0; pos < value->size();)
    {
        const size_t open = value->find("@<", pos);
        const size_t close = open == std::string::npos ? std::string::npos : value->find('>', open + 2);
        if (close == std::string::npos)
        {
            out.append(*value, pos);
            break;
        }
        out.append(*value, pos, open - pos);
        if (used < args.size())
            out += args[used];
        else
        {
            Report(fmt::format("StringFromKey '{}': no argument for {}", key, value->substr(open, close + 1 - open)));
            out.append(*value, open, close + 1 - open);
        }
        ++used;
        pos = close + 1;
    }
    if (used < args.size())
        Report(fmt::format("StringFromKey '{}': {} arguments unused", key, args.size() - used));
    return out;
}

std::string TextTemplateLanguage::ExpandInFile(Run &run, std::string_view value, std::string_view file, int depth)
{
    std::string saved = std::exchange(run.file, std::string(file));
    std::string out;
    ExpandInto(run, out, value, depth);
    run.file = std::move(saved);
    return out;
}

void TextTemplateLanguage::ExpandInto(Run &run, std::string &out, std::string_view text, int depth)
{
    std::optional<size_t> lower;
    for (size_t pos = 0; pos < text.size();)
    {
        const char c = text[pos];
        if (c == '$')
            pos = ExpandConstruct(run, out, text, pos, depth);
        else if (c == '#')
            pos = ExpandAttribute(run, out, text, pos);
        else if (c == '\\' && pos + 1 < text.size())
        {
            if (text[pos + 1] == 'n') // a line break for the text renderer
                out += "\\n";
            else
                out.append(text.substr(pos + 1, Utf8Length(static_cast<unsigned char>(text[pos + 1]))));
            pos += 1 + Utf8Length(static_cast<unsigned char>(text[pos + 1]));
        }
        else if (text.compare(pos, kLowerMarker.size(), kLowerMarker) == 0)
        {
            lower = out.size();
            pos += kLowerMarker.size();
            continue;
        }
        else
        {
            const size_t end = std::min(text.find_first_of("$#\\\xE2", pos + 1), text.size());
            out.append(text.substr(pos, end - pos));
            pos = end;
        }
        if (lower && out.size() > *lower)
        {
            LowerFirstLetter(out, *lower);
            lower.reset();
        }
    }
}

// `#path#`: an attribute of the context object; `#s...#`, `#d...#`, `#f...#` and `#f.N...#` as text, an integer, a
// float and a float with N decimals (the formats of GetAssembledString).
size_t TextTemplateLanguage::ExpandAttribute(Run &run, std::string &out, std::string_view text, size_t start)
{
    const size_t close = text.find('#', start + 1);
    const std::string_view token =
        close == std::string_view::npos ? std::string_view{} : text.substr(start + 1, close - start - 1);
    if (token.size() < 2 || (token[0] != 's' && token[0] != 'd' && token[0] != 'f') ||
        token.find_first_of(" \t\r\n") != std::string_view::npos)
    {
        out += '#'; // a plain '#' of the text
        return start + 1;
    }
    const bool decimals = token[0] == 'f' && token.size() > 3 && token[1] == '.';
    const std::string_view path = token.substr(decimals ? 3 : 1);
    const auto value = run.scripts.Attribute(Who::Context, path);
    if (!value)
    {
        Report(fmt::format("template: no context attribute '{}'", path));
        out += "<nullptr>";
        return close + 1;
    }
    switch (token[0])
    {
    case 'd':
        out += std::to_string(static_cast<long long>(ParseNumber(*value).value_or(0)));
        break;
    case 'f':
        out += decimals ? fmt::format("{:.{}f}", ParseNumber(*value).value_or(0), token[2] - '0')
                        : fmt::format("{:f}", ParseNumber(*value).value_or(0));
        break;
    default:
        out += *value;
        break;
    }
    return close + 1;
}

size_t TextTemplateLanguage::ExpandConstruct(Run &run, std::string &out, std::string_view text, size_t start, int depth)
{
    size_t pos = start + 1;
    if (pos >= text.size() || !IsIdentStart(text[pos]))
    {
        out += '$';
        return pos;
    }
    while (pos < text.size() && IsIdentChar(text[pos]))
        ++pos;
    Construct c;
    c.name = text.substr(start + 1, pos - start - 1);
    if (pos < text.size() && text[pos] == '(')
    {
        // arguments split at `|` of this level; escapes and nested calls skipped
        size_t arg = pos + 1;
        int nested = 0;
        size_t i = arg;
        for (; i < text.size(); ++i)
        {
            const char ch = text[i];
            if (ch == '\\')
                ++i;
            else if (ch == '$' && i + 1 < text.size() && IsIdentStart(text[i + 1]))
            {
                size_t j = i + 1;
                while (j < text.size() && IsIdentChar(text[j]))
                    ++j;
                if (j < text.size() && text[j] == '(')
                    ++nested;
                i = j;
            }
            else if (ch == '|' && nested == 0)
            {
                c.args.push_back(text.substr(arg, i - arg));
                arg = i + 1;
            }
            else if (ch == ')')
            {
                if (nested == 0)
                    break;
                --nested;
            }
        }
        if (i >= text.size())
        {
            Report(fmt::format("template: no ')' after ${}(", c.name));
            out.append(text.substr(start));
            return text.size();
        }
        if (i > pos + 1 || !c.args.empty())
            c.args.push_back(text.substr(arg, i - arg));
        pos = i + 1;
    }
    c.raw = text.substr(start, pos - start);
    if (c.name == "dialogBreak") // the dialogue window splits the line there
        out.append(c.raw);
    else if (!Builtin(run, out, c, depth) && !ScriptFunction(run, out, c, depth))
    {
        Report(fmt::format("template: unknown function '{}'", c.name));
        out.append(c.raw);
    }
    return pos;
}

bool TextTemplateLanguage::ScriptFunction(Run &run, std::string &out, const Construct &c, int depth)
{
    // `$colonyVoc(x)` is DLG_ColonyVoc, `$character(x)` RT_Character, `$perkBaseValue(x)` PerkBaseValue
    std::vector<ScriptArg> args;
    for (const auto arg : c.args)
    {
        std::string expanded;
        ExpandInto(run, expanded, arg, depth);
        args.push_back(TextArg(std::move(expanded)));
    }
    std::string capitalised(c.name);
    capitalised[0] =
        static_cast<char>(capitalised[0] >= 'a' && capitalised[0] <= 'z' ? capitalised[0] - 32 : capitalised[0]);
    for (const std::string &name : {std::string(c.name), "DLG_" + capitalised, "RT_" + capitalised, capitalised})
        if (auto result = run.scripts.Call(name, args))
        {
            out += *result;
            return true;
        }
    return false;
}

bool TextTemplateLanguage::Builtin(Run &run, std::string &out, const Construct &c, int depth)
{
    const auto id = FindBuiltin(c.name);
    if (!id)
        return false;
    const size_t argc = c.args.size();
    const auto fail = [&](std::string problem) {
        Report(fmt::format("template: {}: {}", c.raw, problem));
        out.append(c.raw);
        return true;
    };
    const auto takes = [&](size_t low, size_t high) {
        if (argc >= low && argc <= high)
            return true;
        fail(fmt::format("{} arguments", argc));
        return false;
    };
    const auto arg = [&](size_t i) {
        std::string expanded;
        ExpandInto(run, expanded, c.args[i], depth);
        return expanded;
    };
    const auto choose = [&](size_t i) { ExpandInto(run, out, c.args[i], depth); };
    const auto attribute = [&](Who who, std::string_view path) {
        if (!takes(0, 0))
            return true;
        if (auto value = run.scripts.Attribute(who, path))
        {
            out += *value;
            return true;
        }
        return fail(fmt::format("no attribute {}", path));
    };
    const auto script = [&](std::string_view function, std::vector<ScriptArg> args) {
        if (auto value = run.scripts.Call(function, args))
        {
            out += *value;
            return true;
        }
        return fail(fmt::format("no script function {}", function));
    };
    const auto bySex = [&](Who who) {
        if (!takes(2, 2))
            return true;
        const auto sex = run.scripts.Attribute(who, "sex");
        if (!sex)
            return fail("no character");
        choose(*sex == "woman" ? 1 : 0);
        return true;
    };
    // the game's thresholds: nobility above 70 good, above 30 or unknown neutral, else bad
    const auto byNobility = [&](Who who) {
        if (!takes(3, 3))
            return true;
        const auto nobility = run.scripts.Attribute(who, "reputation.nobility");
        const double value = nobility ? ParseNumber(*nobility).value_or(50) : 50;
        choose(value > 70 ? 0 : value > 30 ? 1 : 2);
        return true;
    };
    const auto number = [&](size_t i) {
        const std::string text = arg(i);
        const auto value = ParseNumber(text);
        if (!value)
            fail(fmt::format("'{}' is not a number", text));
        return value;
    };
    const auto value = [&](std::string_view what, std::optional<std::string> raw) {
        if (!raw)
            return fail(fmt::format("no {}", what));
        if (depth + 1 >= kMaxDepth)
            return fail("nested too deep");
        ExpandInto(run, out, *raw, depth + 1);
        return true;
    };

    switch (*id)
    {
    case BuiltinId::Name:
        return attribute(Who::Player, "name");
    case BuiltinId::LastName:
        return attribute(Who::Player, "lastname");
    case BuiltinId::ShipName:
        return attribute(Who::Player, "Ship.Name");
    case BuiltinId::NpcName:
        return attribute(Who::Npc, "name");
    case BuiltinId::NpcLastName:
        return attribute(Who::Npc, "lastname");
    case BuiltinId::NpcShipName:
        return attribute(Who::Npc, "Ship.Name");
    case BuiltinId::CtxShipName:
        return attribute(Who::Context, "Ship.Name");
    case BuiltinId::City:
        return attribute(Who::Npc, "City");
    case BuiltinId::FullName:
        return !takes(0, 0) || script("GetFullName", {RefArg(ScriptArg::Kind::Player)});
    case BuiltinId::NpcFullName:
        return !takes(0, 0) || script("GetFullName", {RefArg(ScriptArg::Kind::Npc)});
    case BuiltinId::Sir:
        return !takes(0, 0) || script("GetAddress_FormToNPC", {RefArg(ScriptArg::Kind::Player)});
    case BuiltinId::NpcSir:
        return !takes(0, 0) || script("GetAddress_FormToNPC", {RefArg(ScriptArg::Kind::Npc)});
    case BuiltinId::Swear:
        return !takes(0, 0) || script("RandSwear", {});
    case BuiltinId::Hello:
        return !takes(0, 0) || script("TimeGreeting", {});
    case BuiltinId::Bye:
        return !takes(0, 0) || script("Goodbye", {TextArg("1")});
    case BuiltinId::Red:
        return !takes(0, 0) || script("SF_Indent", {});
    case BuiltinId::Br:
        if (takes(0, 0))
            out += '\n';
        return true;
    case BuiltinId::Ctx:
        return !takes(1, 1) ||
               value(fmt::format("context attribute {}", c.args[0]), run.scripts.Attribute(Who::Context, arg(0)));
    case BuiltinId::Attr: {
        constexpr std::string_view prefix = "$context.";
        if (!takes(1, 1))
            return true;
        if (!c.args[0].starts_with(prefix))
            return fail("expects $context.<path>");
        const auto raw = run.scripts.Attribute(Who::Context, c.args[0].substr(prefix.size()));
        if (!raw)
            return fail("no context attribute");
        out += *raw;
        return true;
    }
    case BuiltinId::CtxHasAttr:
        if (takes(2, 3))
        {
            if (run.scripts.Attribute(Who::Context, arg(0)))
                choose(1);
            else if (argc == 3)
                choose(2);
        }
        return true;
    case BuiltinId::Rand:
        if (takes(1, SIZE_MAX))
            choose(static_cast<size_t>(texts_.Random(static_cast<int>(argc))));
        return true;
    case BuiltinId::ManWoman:
        return bySex(Who::Player);
    case BuiltinId::NpcManWoman:
        return bySex(Who::Npc);
    case BuiltinId::CtxManWoman:
        return bySex(Who::Context);
    case BuiltinId::GoodBad:
        return byNobility(Who::Player);
    case BuiltinId::NpcGoodBad:
        return byNobility(Who::Npc);
    case BuiltinId::Key: {
        // $key(k): the common strings, then the file being expanded; $key(k|file): that file
        if (!takes(1, 2))
            return true;
        const std::string key = arg(0);
        std::string file = argc == 2 ? arg(1) : std::string();
        auto raw = texts_.Value(file, key);
        if (!raw && argc == 1 && !run.file.empty())
            raw = texts_.Value(file = run.file, key);
        if (!raw)
            return fail(fmt::format("no key {}", key));
        if (depth + 1 >= kMaxDepth)
            return fail("nested too deep");
        if (file.empty())
            ExpandInto(run, out, *raw, depth + 1);
        else
            out += ExpandInFile(run, *raw, file, depth + 1);
        return true;
    }
    case BuiltinId::Link:
        return !takes(1, 1) || value("key", texts_.KeyValue(arg(0)));
    case BuiltinId::Value: {
        if (!takes(1, 1))
            return true;
        const auto constant = run.scripts.Constant(arg(0));
        if (!constant)
            return fail("no such #define");
        out += *constant;
        return true;
    }
    case BuiltinId::Percent:
        if (takes(1, 1))
            if (const auto n = number(0))
            {
                const std::string digits = HumanNumber(*n * 100);
                const std::string language = AsciiLower(std::string(texts_.Language()));
                out += language == "russian" ? digits + " %" : language == "turkish" ? "%" + digits : digits + "%";
            }
        return true;
    case BuiltinId::Number:
        if (takes(1, 1))
            if (const auto n = number(0))
                out += HumanNumber(*n);
        return true;
    case BuiltinId::Money:
        return !takes(1, 1) || script("FormatMoney", {TextArg(arg(0))});
    case BuiltinId::Abs:
        if (takes(1, 1))
        {
            const std::string text = arg(0);
            out.append(text, !text.empty() && text[0] == '-' ? 1 : 0);
        }
        return true;
    case BuiltinId::Days: {
        if (!takes(1, 1))
            return true;
        const auto n = number(0);
        if (!n)
            return true;
        std::array<std::string, 3> forms;
        constexpr std::array<std::string_view, 3> keys{"day2", "rdays2", "rdays"};
        for (size_t i = 0; i < keys.size(); ++i)
        {
            auto form = texts_.Value("", keys[i]);
            if (!form)
                return fail(fmt::format("no common string {}", keys[i]));
            forms[i] = std::move(*form);
        }
        const auto days = static_cast<long long>(*n);
        out += fmt::format("{} {}", days, DeclensionForm(days, forms[0], forms[1], forms[2]));
        return true;
    }
    case BuiltinId::Decl: {
        if (!takes(3, 4))
            return true;
        const auto n = number(0);
        if (!n)
            return true;
        const auto count = static_cast<long long>(*n);
        out += fmt::format("{} {}", count, DeclensionForm(count, arg(1), arg(2), argc == 4 ? arg(3) : std::string()));
        return true;
    }
    case BuiltinId::NationI:
        return !takes(1, 1) || script("GetNationNameByType", {TextArg(arg(0))});
    case BuiltinId::GoodI:
        return !takes(1, 1) || script("DLG_GoodName", {TextArg(arg(0))});
    case BuiltinId::ModifierPercent:
        return !takes(1, 1) ||
               script("DLG_HumanModifierValuePercent", {TextArg(arg(0)), RefArg(ScriptArg::Kind::Context)});
    }
    return false;
}
} // namespace storm::strings
