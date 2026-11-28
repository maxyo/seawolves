// Host checks for the parts of the UI runtime that need no GL context:
// the template expressions, the .tx decoder (synthetic data only) and the interface model's JSON
// codec (every model fixture of tests/models, given as the argument, written and read back).
#include "fakes.h"
#include "../src/action_trace.h"
#include "../src/components.h"
#include "../src/elements.h"
#include "storm_ui/interface_model.h"
#include "../src/template.h"
#include "../src/text_pages.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using storm::ui::Json;

// An expression's value as text, or its error.
static void ExpectValue(const std::string &expression, const Json &scope, const std::string &expected)
{
    std::string actual;
    try
    {
        actual = storm::ui::ToText(storm::ui::EvaluateExpression(expression, scope));
    }
    catch (const std::exception &error)
    {
        actual = std::string("<<") + error.what() + ">>";
    }
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", expression.c_str(), expected.c_str(),
                     actual.c_str());
        ++failures;
    }
}

// The page rules of the dialogue speech:
// intermediate capacity, the final page reserving room for the replies, exact fit, a
// newline at the boundary, and reflow anchored at the page start.
static void Pages()
{
    using storm::ui::ChoosePage;
    const auto check = [](const std::string &text, size_t offset, std::vector<size_t> starts,
                          std::vector<float> bottoms, float total, float middle, float last, size_t next,
                          size_t display, bool more, const char *what) {
        const storm::ui::PageBreak page = ChoosePage(text, offset, starts, bottoms, total, middle, last);
        Expect(page.next == next && page.display == display && page.more == more, what);
    };
    check("1111222233334444", 0, {0, 4, 8, 12, 16}, {10, 20, 30, 40}, 40, 30, 10, 12, 12, true,
          "pages: an intermediate page fills its height");
    check("11112222", 0, {0, 4, 8}, {10, 20}, 20, 30, 10, 4, 4, true,
          "pages: the last line is kept for the final page");
    check("11112222", 4, {0, 4}, {10}, 10, 30, 10, 8, 8, false, "pages: the final page fits beside the replies");
    check("111122223333", 0, {0, 4, 8, 12}, {10, 20, 30}, 30, 30, 30, 12, 12, false, "pages: an exact fit is one page");
    check("11112222", 0, {0, 4, 8}, {10, 20}, 20, 30, 20, 8, 8, false, "pages: all of it fits on the final page");
    check("aaa\nbbb\nccc", 0, {0, 4, 8, 11}, {10, 20, 30}, 30, 20, 10, 8, 7, true,
          "pages: a newline at the boundary shows no empty line");
    check("1111222233334444", 4, {0, 2, 4, 6, 8, 10, 12}, {10, 20, 30, 40, 50, 60}, 60, 30, 10, 10, 10, true,
          "pages: narrow reflow from the page start");
    check("1111222233334444", 4, {0, 6, 12}, {10, 20}, 20, 30, 10, 10, 10, true,
          "pages: wide reflow from the page start");

    const auto width = [](const std::string &line) { return static_cast<float>(line.size()); };
    const auto starts = storm::ui::WrapLines("aaa bb cccc\ndd", 6.0f, width);
    Expect(starts == std::vector<size_t>({0, 7, 12, 14}), "wrap: at spaces up to the width and at newlines");
    Expect(storm::ui::WrapLines("toolongword x", 4.0f, width) == std::vector<size_t>({0, 12, 13}),
           "wrap: a word wider than the line stays whole");
    Expect(storm::ui::WrapLines("a\n\nb", 9.0f, width) == std::vector<size_t>({0, 2, 3, 4}),
           "wrap: an empty line between two newlines");
    Expect(storm::ui::WrapLines("", 9.0f, width) == std::vector<size_t>({0}), "wrap: no text, no lines");
}

// InterfaceModelJson writes what ParseInterfaceModel reads back unchanged: the recordings of
// StormUiModels and the fixtures share one codec with the in-process struct.
static void ModelCodec(const char *models)
{
    int count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(models))
    {
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        storm::ui::InterfaceModel model, again;
        if (!storm::ui::ParseInterfaceModel(text.str(), model))
            continue; // not an interface model (a state fixture)
        ++count;
        const std::string written = storm::ui::InterfaceModelJson(model);
        const bool same = storm::ui::ParseInterfaceModel(written, again) && again == model;
        if (!same)
            std::fprintf(stderr, "model %s\n", entry.path().filename().string().c_str());
        Expect(same, "interface model: written and read back unchanged");
    }
    Expect(count > 20, "interface model: the fixtures were found");
    storm::ui::InterfaceModel model;
    model.screen = "store";
    model.widgets.push_back({});
    model.widgets[0].type = "text";
    model.widgets[0].label = "bad \xff text";
    storm::ui::InterfaceModel back;
    Expect(storm::ui::ParseInterfaceModel(storm::ui::InterfaceModelJson(model), back) &&
               back.widgets.at(0).label == "bad \xEF\xBF\xBD text",
           "interface model: broken text is written with U+FFFD");
    // A field of another type than the format's is a defect of the recording, not a default.
    std::string error;
    Expect(!storm::ui::ParseInterfaceModel(R"({"widgets":[{"node":"a"},{"node":"b","index":"3"}]})", back, &error) &&
               error == "interface model: widgets[1].index: expected a number",
           "interface model: a wrong-typed field is an error naming it");
    Expect(!storm::ui::ParseInterfaceModel(R"({"widgets":[{"node":"a","cells":[{"text":1}]}]})", back, &error) &&
               error == "interface model: widgets[0].cells[0].text: expected a string",
           "interface model: a wrong-typed cell field is an error naming it");
}

// The flicker check across a screen switch: the tap's own frame shows a predicted header tab over the old body, the
// next one the new screen. That first frame is the tap's feedback when the screen predicted it, a transient frame
// when it did not.
static void PredictedFeedback()
{
    using storm::ui::ActionTracer;
    using storm::ui::FrameContent;
    const auto part = [](uint64_t key, uint64_t content) {
        FrameContent::Part p;
        p.key = key;
        p.content = content;
        p.box = Rml::Rectanglef::FromPositionSize({0.0f, 0.0f}, {100.0f, 100.0f});
        p.label = "part" + std::to_string(key);
        return p;
    };
    const auto frame = [](std::vector<FrameContent::Part> parts, double time, uint64_t hash) {
        FrameContent f;
        f.parts = std::move(parts);
        f.time = time;
        f.hash = hash;
        return f;
    };
    // keys 1, 2: the old screen's tab and body; 3, 4: the new screen's.
    const std::vector<FrameContent> frames = {
        frame({part(1, 10), part(2, 20)}, 0.0, 1), frame({part(1, 12), part(2, 20)}, 0.03, 2),
        frame({part(3, 30), part(4, 40)}, 0.06, 3), frame({part(3, 30), part(4, 40)}, 0.1, 3)};
    const auto plain = ActionTracer::Analyse("header section", frames, 100.0f, 100.0f, false);
    Expect(plain.transient == 1 && plain.feedback_frame == 1,
           "flicker check: a tab switched over the old body unasked is a transient frame");
    const auto predicted = ActionTracer::Analyse("header section", frames, 100.0f, 100.0f, true);
    Expect(predicted.transient == 0 && predicted.predicted && predicted.feedback_frame == 1,
           "flicker check: the predicted tab in the tap's own frame is its feedback, not a transient frame");
    const std::vector<FrameContent> later = {frames[0], frames[1], frame({part(1, 12), part(2, 22)}, 0.05, 4),
                                             frames[2], frames[3]};
    const auto late = ActionTracer::Analyse("header section", later, 100.0f, 100.0f, true);
    Expect(late.transient == 1 && late.feedback_frame == 1,
           "flicker check: a frame after the predicted one that shows neither state is still a transient frame");

    // Two taps before the next frame: the first predicted, the second asks nothing. The frame shows the first's
    // prediction, the feedback of the second too.
    ActionTracer tracer;
    tracer.Begin("tap 1", frames[0], 100.0f, 100.0f);
    tracer.Predicted();
    tracer.Presented(frames[0]);            // a frame drawn again before the prediction's
    const auto first = tracer.Finish(true); // the second tap's touch-down
    tracer.Begin("tap 2", frames[0], 100.0f, 100.0f);
    for (size_t f = 1; f < frames.size(); ++f)
        tracer.Presented(frames[f]);
    const auto second = tracer.Finish(false);
    Expect(first && first->interrupted && second && second->predicted && second->transient == 0,
           "flicker check: a tap before the frame of a predicted one takes that prediction as its feedback");
    tracer.Begin("tap 3", frames[0], 100.0f, 100.0f);
    tracer.Predicted();
    tracer.Finish(true);
    tracer.Begin("tap 4", frames[1], 100.0f, 100.0f); // the screen shows the prediction already
    Expect(!tracer.Finish(false)->predicted,
           "flicker check: a tap after the prediction was drawn starts from it and takes nothing over");
}

// The fields the template checks read.
static Json Scope()
{
    return Json::parse(R"({"label":"Купить <1>","n":1250000,"small":1250,"on":true,"off":false,"variant":"primary",
        "items":[{"label":"a","count":2},{"label":"b"}],"empty":[],"size":"md","spec":{"h":{"sm":40,"md":44}}})");
}

// Expressions, number formats; in strict mode a path that names nothing is an error.
static void Expressions(const Json &scope)
{
    ExpectValue("label", scope, "Купить <1>");
    ExpectValue("num(n) + ' / ' + num(small)", scope,
                "1\xE2\x80\xAF"
                "250\xE2\x80\xAF"
                "000 / 1250");
    ExpectValue("on && !off ? 'y' : 'n'", scope, "y");
    // Amounts as the game and num() group them compare as numbers (the header's preview tone).
    ExpectValue("'3000192' * 1 < '3 000 495' * 1 && '999\xC2\xA0"
                "800' * 1 < '1\xE2\x80\xAF"
                "000\xE2\x80\xAF"
                "500' * 1 ? 'y' : 'n'",
                scope, "y");
    ExpectValue("'1 25' * 1", scope, "1");
    ExpectValue("spec.h[size] + 4", scope, "48");
    ExpectValue("str(opt(missing)) + '|' + or(missing, 'x') + '|' + len(items)", scope, "|x|2");
    // Literals of structures, for a nested component's json props.
    ExpectValue("[1, 'a', {k: on, 'q': [ ]}]", scope, R"([1.0,"a",{"k":true,"q":[]}])");
    // A path that names nothing is an error unless marked optional; a branch not taken is not evaluated.
    ExpectValue(
        "missing", scope,
        "<<template expression: 'missing' is not defined in 'missing' (opt() marks a path that may be missing)>>");
    ExpectValue("items[1].count", scope,
                "<<template expression: 'items[1].count' is not defined in 'items[1].count' (opt() marks a path that "
                "may be missing)>>");
    ExpectValue("spec.h[variant]", scope,
                "<<template expression: 'spec.h[variant]' is not defined in 'spec.h[variant]' (opt() marks a path "
                "that may be missing)>>");
    ExpectValue("str(opt(missing)) + '|' + or(items[1].count, 0) + '|' + str(opt(items[5].label)) + '|' + "
                "items[0].count",
                scope, "|0||2");
    ExpectValue("off ? missing.x : 'ok'", scope, "ok");
    ExpectValue("variant == 'primary' ? 'ink' : 'duo'", scope, "ink");
}

static void ShortWords()
{
    const std::string bound = "Я\xC2\xA0в\xC2\xA0порту и\xC2\xA0с\xC2\xA0ним «к\xC2\xA0нам» (а\xC2\xA0он) вы";
    Expect(storm::ui::BindShortWords("Я в порту и с ним «к нам» (а он) вы") == bound,
           "short words: a one-letter Russian word holds on to the next word");
    Expect(storm::ui::BindShortWords("слово в") == "слово в" && storm::ui::BindShortWords("ив яд") == "ив яд",
           "short words: a word at the end or inside a longer word stays");
}

static void LazyCalls(const Json &scope)
{
    // A call in a branch not taken is never made (tr of an empty key would be an error).
    std::vector<std::string> asked;
    storm::ui::SetTemplateStrings([&asked](const std::string &key) {
        asked.push_back(key);
        return "[" + key + "]";
    });
    ExpectValue("(opt(missing) ? tr(missing) : 'plain') + '|' + (variant ? tr('a.b') : tr(missing))", scope,
                "plain|[a.b]");
    Expect(asked == std::vector<std::string>({"a.b"}), "template: calls only in the branch taken");
    storm::ui::SetTemplateStrings(nullptr);
}

// The template functions.
static void Helpers(const Json &scope)
{
    ExpectValue("pick(variant, 'danger', 1, 'primary', 2, 3) + str(in(size, 'sm', 'md'))", scope, "2true");
    ExpectValue("len(items) > 1 && items[1].label == 'b'", scope, "true");
    ExpectValue("str(7 / 2) + ' ' + 10 % 4 + ' ' + (-n < 0)", scope, "3.5 2 true");
    ExpectValue("replace('Удалить {s}?', '{s}', label) + '|' + contains(variant, 'rim') + '|' + ends('Цель?', '?')",
                scope, "Удалить Купить <1>?|true|true");
    ExpectValue("len(range(int(split('2/3', '/')[1])))", scope, "3");
    const Json images =
        Json::parse(R"({"tok":{"icon":"ship","enamel":"red"},"pic":{"texture":"INTERFACES/items6","cell":3},
        "face":{"kind":"portrait","texture":"x"},"bare":{"icon":"anchor"}})");
    ExpectValue("image_kind(tok, '') + '|' + image_kind(pic, '') + '|' + image_kind(face, 'a') + '|' + "
                "image_kind(bare, '') + '|' + image_kind(opt(none), 'anchor') + '|' + image_kind(opt(none), '')",
                images, "token|item|portrait|icon|icon|");
    ExpectValue("digits('1\xE2\x80\xAF"
                "500') + 1",
                scope, "1501");
}

// No silent fallbacks: wrong specs, props, enum values, icons and expressions are errors.
static void Specs(const Json &scope)
{
    const Json entry = Json::parse(R"({"tag":"c-x","props":{"variant":{"type":"enum","options":["a","b"],"default":"a"},
        "size":{"type":"enum","options":[44,48]},"label":{"type":"text"}}})");
    const auto spec = storm::ui::ParseComponentSpec(entry);
    Expect(spec->props.size() == 3, "spec props parsed");
    const auto &variant = *spec->FindAttribute("variant");
    Expect(storm::ui::CheckPropValue(variant, "b").empty(), "enum value accepted");
    Expect(!storm::ui::CheckPropValue(variant, "purple").empty(), "enum value outside options refused");
    Expect(storm::ui::CheckPropValue(*spec->FindAttribute("size"), "48").empty(),
           "numeric enum from attribute text accepted");
    Expect(storm::ui::CheckPropValue(*spec->FindAttribute("label"), "anything").empty(), "text prop takes anything");
    bool threw = false;
    try
    {
        storm::ui::ParseComponentSpec(Json::parse(R"({"tag":"c-y","props":{"p":{"type":"colour"}}})"));
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    Expect(threw, "unknown prop type refused");
    // A template path is checked against its prop's shape at load, through a loop and a nested component's prop.
    const auto shaped = [](const std::string &body) {
        const auto spec = storm::ui::ParseComponentSpec(
            Json{{"tag", "c-s"}, {"template", body}, {"props", Json::parse(R"({"title":{"type":"text"},
                "list":{"type":"json","shape":{"$null":{"label":"text","rows":[{"name":"text"}]}}}})")}});
        try
        {
            storm::ui::CompileComponent(*spec,
                                        [&](const std::string &tag) { return tag == "c-s" ? spec.get() : nullptr; });
            return std::string();
        }
        catch (const std::exception &error)
        {
            return std::string(error.what());
        }
    };
    Expect(shaped("<t-for each=\"list.rows\" as=\"r\"><c-s title=\"{{ r.name }}\"/></t-for>{{ list.label }}").empty(),
           "paths the shape has compile");
    Expect(!shaped("<t-for each=\"list.rows\" as=\"r\"><c-s title=\"{{ r.nam }}\"/></t-for>").empty(),
           "a misspelt field of a loop's row fails at load");
    Expect(!shaped("{{ opt(list.labl) }}").empty(), "a misspelt field fails at load also where it may be absent");
    Expect(!shaped("{{ title.text }}").empty(), "a field of a text prop fails at load");
    Expect(!shaped("{{ titel }}").empty(), "a name that is no prop fails at load");
    Expect(storm::ui::IsGenericAttribute("id") && storm::ui::IsGenericAttribute("data-action") &&
               !storm::ui::IsGenericAttribute("colour"),
           "generic attributes");
    storm::ui::LoadIconPack(
        Json::parse(R"({"levels":[24],"icons":{"i-ship":{"glyph":[[1,1,0]]},"c-rum":{"colour":[[1,1,0]]}}})"), "");
    Expect(storm::ui::HasIcon("ship") && storm::ui::HasIcon("c-rum") && !storm::ui::HasIcon("anchor"), "icon lookup");
    float uv[4];
    Expect(storm::ui::AtlasCell(0, 4, uv) && uv[2] == 1.0f, "cell 0 is the whole texture");
    Expect(storm::ui::AtlasCell(6, 4, uv) && uv[0] == 0.25f && uv[1] == 0.25f && uv[2] == 0.5f, "atlas cell 6 of 4x4");
    Expect(!storm::ui::AtlasCell(17, 4, nullptr) && !storm::ui::AtlasCell(-1, 4, nullptr),
           "cell outside the atlas refused");
    ExpectValue("nosuchfunction(1)", scope,
                "<<template expression: unknown function nosuchfunction in 'nosuchfunction(1)'>>");
}

int main(int argc, char **argv)
{
    Pages();
    PredictedFeedback();
    if (argc > 1)
        ModelCodec(argv[1]);
    else
        Expect(false, "interface model: the fixture directory argument is missing");
    const Json scope = Scope();
    Expressions(scope);
    ShortWords();
    LazyCalls(scope);
    Helpers(scope);
    Specs(scope);

    if (failures)
        return 1;
    std::printf("ui unit checks passed\n");
    return 0;
}
