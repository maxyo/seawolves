#include "text_templates.hpp"

#include <map>
#include <vector>

#include <catch2/catch.hpp>

using storm::strings::TextTemplateLanguage;
using storm::texts::ScriptArg;
using storm::texts::Who;

namespace
{
struct Texts final : storm::strings::TemplateTexts
{
    std::map<std::pair<std::string, std::string>, std::string> values; // (file, key); file "" = common strings
    std::map<std::string, std::string> keys;
    std::string language = "english";
    int choice = 0;
    std::vector<std::string> reports;

    std::optional<std::string> Value(std::string_view file, std::string_view key) override
    {
        const auto found = values.find({std::string(file), std::string(key)});
        return found == values.end() ? std::nullopt : std::optional(found->second);
    }
    std::optional<std::string> KeyValue(std::string_view key) override
    {
        const auto found = keys.find(std::string(key));
        return found == keys.end() ? std::nullopt : std::optional(found->second);
    }
    std::string_view Language() override
    {
        return language;
    }
    int Random(int bound) override
    {
        return choice % bound;
    }
    void Report(std::string_view problem) override
    {
        reports.emplace_back(problem);
    }
};

struct Scripts final : storm::texts::ScriptAccess
{
    std::map<std::pair<Who, std::string>, std::string> attributes;
    std::vector<std::string> calls; // "name(arg,&player,...)"

    std::optional<std::string> Attribute(Who who, std::string_view path) override
    {
        const auto found = attributes.find({who, std::string(path)});
        return found == attributes.end() ? std::nullopt : std::optional(found->second);
    }
    std::optional<std::string> Call(std::string_view function, std::span<const ScriptArg> args) override
    {
        if (function != "RT_Character" && function != "GetAddress_FormToNPC" && function != "SF_Indent" &&
            function != "echo")
            return std::nullopt;
        std::string call = std::string(function) + "(";
        for (const auto &arg : args)
            call += (arg.kind == ScriptArg::Kind::Text     ? arg.text
                     : arg.kind == ScriptArg::Kind::Player ? std::string("&player")
                     : arg.kind == ScriptArg::Kind::Npc    ? std::string("&npc")
                                                           : std::string("&context")) +
                    ",";
        calls.push_back(call + ")");
        return function == "SF_Indent" ? "    " : "<" + calls.back() + ">";
    }
    std::optional<std::string> Constant(std::string_view name) override
    {
        return name == "HEAL" ? std::optional<std::string>("0.15") : std::nullopt;
    }
};
} // namespace

TEST_CASE("text_templates.constructs")
{
    Texts texts;
    Scripts scripts;
    TextTemplateLanguage language(texts);
    scripts.attributes[{Who::Player, "name"}] = "Charles";
    scripts.attributes[{Who::Player, "sex"}] = "man";
    scripts.attributes[{Who::Npc, "sex"}] = "woman";
    scripts.attributes[{Who::Context, "price"}] = "12.5";
    scripts.attributes[{Who::Context, "label"}] = "$name's";

    CHECK(language.Expand("Hello, $name!", scripts) == "Hello, Charles!");
    CHECK(language.Expand("$name() and $Name", scripts) == "Charles and Charles"); // $x() is $x, names fold case
    CHECK(language.Expand("$manWoman(he|she)/$npcManWoman(he|she)", scripts) == "he/she");
    CHECK(language.Expand("cost: $ctx(price)", scripts) == "cost: 12.5");
    CHECK(language.Expand("$ctx(label) ship", scripts) == "Charles's ship"); // a $ctx value is a template too
    CHECK(language.Expand("$attr($context.label)", scripts) == "$name's");   // $attr is not
    CHECK(language.Expand("#sprice# #dprice# #f.2price#", scripts) == "12.5 12 12.50");
    CHECK(language.Expand("#1 and #2", scripts) == "#1 and #2");
    CHECK(language.Expand("\\(x\\) \\| \\$ \\n", scripts) == "(x) | $ \\n");
    CHECK(language.Expand("a$br()b", scripts) == "a\nb");
    CHECK(language.Expand("$red text", scripts) == "     text");
    CHECK(language.Expand("$value(HEAL)", scripts) == "0.15");
    CHECK(language.Expand("$percent(0.15) $number(2.50) $abs(-3)", scripts) == "15% 2.5 3");
    texts.language = "russian";
    CHECK(language.Expand("$percent(0.05)", scripts) == "5 %");
    CHECK(language.Expand("$decl(21|day|days|many) $decl(3|d|ds|m) $decl(12|d|ds|m)", scripts) == "21 day 3 ds 12 m");
    const size_t reports = texts.reports.size();
    CHECK(language.Expand("one $dialogBreak() two", scripts) == "one $dialogBreak() two"); // for the dialogue window
    CHECK(texts.reports.size() == reports);
}

TEST_CASE("text_templates.choices")
{
    Texts texts;
    Scripts scripts;
    TextTemplateLanguage language(texts);
    texts.choice = 2;
    CHECK(language.Expand("$rand(a|b|$rand(c|d)|e)", scripts) == "c"); // `|` of a nested call is its own

    const auto goodBad = [&](const char *nobility) {
        scripts.attributes[{Who::Player, "reputation.nobility"}] = nobility;
        return language.Expand("$goodBad(good|plain|rude)", scripts);
    };
    CHECK(goodBad("71") == "good");
    CHECK(goodBad("70") == "plain");
    CHECK(goodBad("31") == "plain");
    CHECK(goodBad("30") == "rude");
    scripts.attributes.erase({Who::Player, "reputation.nobility"});
    CHECK(language.Expand("$goodBad(good|plain|rude)", scripts) == "plain");
}

TEST_CASE("text_templates.keys_and_files")
{
    Texts texts;
    Scripts scripts;
    TextTemplateLanguage language(texts);
    texts.values[{"", "Boatswain"}] = "boatswain";
    texts.values[{"Dialogs\\Roger.txt", "greet"}] = "$key(Boatswain), $key(own)!";
    texts.values[{"Dialogs\\Roger.txt", "own"}] = "mate";
    texts.values[{"Dialogs\\Roger.txt", "loop"}] = "$key(loop)";
    texts.values[{"Other.txt", "far"}] = "$key(own)";
    texts.values[{"Other.txt", "near"}] = "near";

    // $key(k): the common strings, then the file being expanded
    CHECK(language.Convert("Dialogs\\Roger.txt", "greet", false, scripts) == "boatswain, mate!");
    CHECK(language.Convert("Dialogs\\Roger.txt", "nothing", false, scripts) == "nothing");
    CHECK(texts.reports.size() == 1);
    CHECK(language.Convert("Dialogs\\Roger.txt", "nothing", true, scripts).empty());
    CHECK(language.Convert("Other.txt", "far", false, scripts) == "$key(own)"); // not in Other.txt
    CHECK(language.Expand("$key(near|Other.txt)", scripts) == "near");
    CHECK(language.Convert("Dialogs\\Roger.txt", "loop", false, scripts).find("$key(loop)") != std::string::npos);

    texts.keys["Ship_3"] = "Sold @<GetShipName(i)> for @<price> gold";
    const std::string both[] = {"Fortune", "300"};
    CHECK(language.StringFromKey("Ship_3", both) == "Sold Fortune for 300 gold");
    CHECK(language.StringFromKey("Ship_3", std::span(both, 1)) == "Sold Fortune for @<price> gold");
    CHECK(language.StringFromKey("absent", {}) == "absent");
}

TEST_CASE("text_templates.script_functions")
{
    Texts texts;
    Scripts scripts;
    TextTemplateLanguage language(texts);
    // a name that is no built-in is a script function: name, DLG_Name, RT_Name, Name
    CHECK(language.Expand("$character($ctx(id)|x)", scripts) == "<RT_Character($ctx(id),x,)>"); // kept, reported
    scripts.attributes[{Who::Context, "id"}] = "Mary";
    CHECK(language.Expand("$character($ctx(id)|x)", scripts) == "<RT_Character(Mary,x,)>");
    CHECK(language.Expand("$echo()", scripts) == "<echo()>");
    CHECK(language.Expand("$sir $npcSir", scripts) == "<GetAddress_FormToNPC(&player,)> <GetAddress_FormToNPC(&npc,)>");
    CHECK(language.Expand("$unknownThing(1) left", scripts) == "$unknownThing(1) left");
    CHECK(language.Expand("$broken(a", scripts) == "$broken(a");
    CHECK(language.Expand("price $ and $5", scripts) == "price $ and $5");
}

TEST_CASE("text_templates.lower_marker")
{
    Texts texts;
    Scripts scripts;
    TextTemplateLanguage language(texts);
    scripts.attributes[{Who::Context, "a"}] = "England";
    scripts.attributes[{Who::Context, "b"}] = "[color=1,2,3,4]Англия";
    scripts.attributes[{Who::Context, "c"}] = "Éire";
    CHECK(language.Expand("of \xE2\x86\xB4$ctx(a)", scripts) == "of england");
    CHECK(language.Expand("\xE2\x86\xB4$ctx(b)", scripts) == "[color=1,2,3,4]англия"); // past markup
    CHECK(language.Expand("\xE2\x86\xB4$ctx(c)", scripts) == "éire");
    CHECK(language.Expand("\xE2\x86\xB4 Big", scripts) == " big");
}
