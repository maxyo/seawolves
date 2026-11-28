#include "script_port.hpp"

#include "core.h"
#include "platform/android_script_errors.hpp"
#include "istring.hpp"
#include "s_import_func.h"
#include "script_libriary.h"
#include "storm_ui/interface_model.h"
#include "storm_ui/tree_layout.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <numeric>
#include <set>
#include <utility>

#include <stdexcept>

namespace storm::ui_scripts
{
namespace
{
// The screen call the natives act on while its script runs; the game thread's only.
struct Running
{
    storm::ui::ScriptCall &call;
    std::string failure; // the first native that failed
};
Running *running = nullptr;

struct Scope
{
    Running state;
    Running *outer;
    explicit Scope(storm::ui::ScriptCall &call) : state{call, {}}, outer(running)
    {
        running = &state;
    }
    ~Scope()
    {
        running = outer;
    }
};

// An argument the VM did not give as its type: the native fails naming it (the compiler checks the count only).
std::string bad_argument;

std::string PopText(VS_STACK *stack)
{
    auto *value = static_cast<VDATA *>(stack->Pop());
    const char *text = value ? value->GetString() : nullptr;
    if (!text && bad_argument.empty())
        bad_argument = "an argument is not a text";
    return text ? text : "";
}

float PopFloat(VS_STACK *stack)
{
    auto *value = static_cast<VDATA *>(stack->Pop());
    if (!value && bad_argument.empty())
        bad_argument = "an argument is not a number";
    return value ? value->GetFloat() : 0.0f;
}

ATTRIBUTES *PopObject(VS_STACK *stack)
{
    auto *value = static_cast<VDATA *>(stack->Pop());
    ATTRIBUTES *root = value ? value->GetAClass() : nullptr;
    if (!root && bad_argument.empty())
        bad_argument = "an argument is not an object";
    return root;
}

storm::ui::ViewTree Tree(const ATTRIBUTES &attributes)
{
    storm::ui::ViewTree tree{attributes.GetThisName(), attributes.HasValue() ? attributes.GetValue() : "", {}};
    for (size_t n = 0; n < attributes.GetAttributesNum(); n++)
        tree.children.push_back(Tree(*attributes.GetAttributeClass(static_cast<uint32_t>(n))));
    return tree;
}

// The named fields of an object argument: a required one absent, a number that is none, or a field no one reads (a
// misspelt name) fails the native. Names compare as the VM's do, without case.
class Fields
{
  public:
    explicit Fields(const ATTRIBUTES &at) : at_(at)
    {
    }
    std::string Text(const char *name)
    {
        if (std::optional<std::string> value = Maybe(name))
            return *value;
        throw std::runtime_error(std::string("no field ") + name);
    }
    std::optional<std::string> Maybe(const char *name)
    {
        read_.insert(AsciiLower(name));
        const ATTRIBUTES *field = at_.GetAttributeClass(name);
        if (!field)
            return std::nullopt;
        return std::string(field->HasValue() ? field->GetValue() : "");
    }
    int Int(const char *name)
    {
        const std::string text = Text(name);
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size() || text.empty())
            throw std::runtime_error(std::string("field ") + name + " is '" + text + "', not a whole number");
        return value;
    }
    bool Bool(const char *name)
    {
        return Int(name) != 0;
    }
    void Done() const
    {
        for (uint32_t n = 0; n < at_.GetAttributesNum(); n++)
            if (const std::string name(at_.GetAttributeClass(n)->GetThisName()); !read_.contains(AsciiLower(name)))
                throw std::runtime_error("no field " + name + " is known");
    }

  private:
    const ATTRIBUTES &at_;
    std::set<std::string> read_;
};

// Runs `act` on the running screen call. Outside one, with an argument of the wrong type, or when it throws, the
// native fails: an error line, no result, and the build or event that ran the script fails with it.
template <class Act> uint32_t Native(const char *native, Act &&act)
{
    if (!running)
    {
        spdlog::error("{}: called outside a UI script screen's build or event", native);
        bad_argument.clear();
        return IFUNCRESULT_FAILED;
    }
    try
    {
        if (!bad_argument.empty())
            throw std::runtime_error(std::exchange(bad_argument, {}));
        act(running->call);
        return IFUNCRESULT_OK;
    }
    catch (const std::exception &error)
    {
        spdlog::error("{}: {}", native, error.what());
        if (running->failure.empty())
            running->failure = std::string(native) + ": " + error.what();
        return IFUNCRESULT_FAILED;
    }
}

template <class Get> uint32_t ReturnInt(VS_STACK *stack, const char *native, Get &&get)
{
    return Native(native, [&](storm::ui::ScriptCall &call) {
        const auto value = static_cast<int32_t>(get(call));
        static_cast<VDATA *>(stack->Push())->Set(value);
    });
}

template <class Get> uint32_t ReturnFloat(VS_STACK *stack, const char *native, Get &&get)
{
    return Native(native, [&](storm::ui::ScriptCall &call) {
        const auto value = static_cast<float>(get(call));
        static_cast<VDATA *>(stack->Push())->Set(value);
    });
}

template <class Get> uint32_t ReturnText(VS_STACK *stack, const char *native, Get &&get)
{
    return Native(native, [&](storm::ui::ScriptCall &call) {
        const std::string value = get(call);
        static_cast<VDATA *>(stack->Push())->Set(value);
    });
}

uint32_t UiShow(VS_STACK *stack)
{
    ATTRIBUTES *view = PopObject(stack);
    return Native("UiShow", [&](storm::ui::ScriptCall &call) { call.Show(Tree(*view)); });
}

uint32_t UiPredict(VS_STACK *stack)
{
    ATTRIBUTES *view = PopObject(stack);
    return Native("UiPredict", [&](storm::ui::ScriptCall &call) { call.Predict(Tree(*view)); });
}

uint32_t UiOn(VS_STACK *stack)
{
    const std::string verb = PopText(stack), id = PopText(stack);
    return Native("UiOn", [&](storm::ui::ScriptCall &call) { call.On(id, verb); });
}

uint32_t UiSend(VS_STACK *stack)
{
    const std::string value = PopText(stack), verb = PopText(stack), node = PopText(stack);
    return Native("UiSend", [&](storm::ui::ScriptCall &call) { call.Send(node, -1, verb, value, "", ""); });
}

// UiSendTo(&action): {node, verb, value, row (absent: the node's own widget), guard, lead (absent: none)}.
uint32_t UiSendTo(VS_STACK *stack)
{
    ATTRIBUTES *action = PopObject(stack);
    return Native("UiSendTo", [&](storm::ui::ScriptCall &call) {
        Fields fields(*action);
        const std::string node = fields.Text("node"), verb = fields.Text("verb"), value = fields.Text("value");
        const int row = fields.Maybe("row") ? fields.Int("row") : -1;
        const std::string guard = fields.Maybe("guard").value_or(""), lead = fields.Maybe("lead").value_or("");
        fields.Done();
        call.Send(node, row, verb, value, guard, lead);
    });
}

uint32_t UiHold(VS_STACK *stack)
{
    const float seconds = PopFloat(stack);
    return Native("UiHold", [&](storm::ui::ScriptCall &call) { call.Hold(seconds); });
}

uint32_t UiAnswer(VS_STACK *stack)
{
    return ReturnText(stack, "UiAnswer", [](storm::ui::ScriptCall &call) { return call.Answer(); });
}

uint32_t UiAwait(VS_STACK *)
{
    return Native("UiAwait", [](storm::ui::ScriptCall &call) { call.Await(); });
}

uint32_t UiRebuild(VS_STACK *)
{
    return Native("UiRebuild", [](storm::ui::ScriptCall &call) { call.Rebuild(); });
}

uint32_t UiFirst(VS_STACK *stack)
{
    return ReturnInt(stack, "UiFirst", [](storm::ui::ScriptCall &call) { return call.First(); });
}

// `out`'s attributes become `tree`'s children, emptied first.
void Write(ATTRIBUTES &out, const storm::ui::ViewTree &tree)
{
    while (out.GetAttributesNum() != 0)
        out.DeleteAttributeClassX(out.GetAttributeClass(0u));
    for (const storm::ui::ViewTree &child : tree.children)
    {
        ATTRIBUTES &at = out.CreateAttribute(child.name);
        at.SetValue(child.value);
        Write(at, child);
    }
}

// UiCommand(kind, &fields): one struct per kind, each field named; an unknown kind or field fails.
storm::ui::ScriptCommand Command(const std::string &kind, Fields &fields)
{
    using namespace storm::ui;
    static const std::pair<const char *, ScriptCommand (*)(Fields &)> kinds[] = {
        {"host", [](Fields &f) -> ScriptCommand { return HostCommand{f.Text("action")}; }},
        {"quick", [](Fields &f) -> ScriptCommand { return QuickCommand{f.Text("which")}; }},
        {"title", [](Fields &f) -> ScriptCommand { return TitleCommand{f.Text("command")}; }},
        {"settings", [](Fields &f) -> ScriptCommand { return SettingsCommand{f.Text("verb")}; }},
        {"setting", [](Fields &f) -> ScriptCommand { return SettingCommand{f.Text("key"), f.Int("value")}; }},
    };
    for (const auto &[name, make] : kinds)
        if (kind == name)
        {
            ScriptCommand command = make(fields);
            fields.Done();
            return command;
        }
    throw std::runtime_error("no command kind '" + kind + "'");
}

uint32_t UiCommand(VS_STACK *stack)
{
    ATTRIBUTES *fields = PopObject(stack);
    const std::string kind = PopText(stack);
    return Native("UiCommand", [&](storm::ui::ScriptCall &call) {
        Fields read(*fields);
        call.Command(Command(kind, read));
    });
}

uint32_t UiPass(VS_STACK *stack)
{
    ATTRIBUTES *note = PopObject(stack);
    const std::string screen = PopText(stack);
    return Native("UiPass", [&](storm::ui::ScriptCall &call) { call.Pass(screen, Tree(*note)); });
}

// UiTake(screen, &out) and UiState(channel, &out): out becomes the note or the state (1), or is emptied (0).
uint32_t UiTake(VS_STACK *stack)
{
    ATTRIBUTES *out = PopObject(stack);
    const std::string screen = PopText(stack);
    return ReturnInt(stack, "UiTake", [&](storm::ui::ScriptCall &call) {
        const std::optional<storm::ui::ViewTree> note = call.Take(screen);
        Write(*out, note.value_or(storm::ui::ViewTree{}));
        return note.has_value();
    });
}

uint32_t UiState(VS_STACK *stack)
{
    ATTRIBUTES *out = PopObject(stack);
    const std::string channel = PopText(stack);
    return ReturnInt(stack, "UiState", [&](storm::ui::ScriptCall &call) {
        const std::optional<storm::ui::ViewTree> state = call.State(channel);
        Write(*out, state.value_or(storm::ui::ViewTree{}));
        return state.has_value();
    });
}

uint32_t UiToast(VS_STACK *stack)
{
    const std::string text = PopText(stack);
    return Native("UiToast", [&](storm::ui::ScriptCall &call) { call.Toast(text); });
}

uint32_t UiRead(VS_STACK *stack)
{
    const std::string text = PopText(stack), title = PopText(stack);
    return Native("UiRead", [&](storm::ui::ScriptCall &call) { call.Read(title, text); });
}

uint32_t UiEditText(VS_STACK *stack)
{
    const std::string title = PopText(stack), node = PopText(stack);
    return Native("UiEditText", [&](storm::ui::ScriptCall &call) { call.EditText(node, title); });
}

uint32_t UiError(VS_STACK *stack)
{
    const std::string text = PopText(stack);
    return Native("UiError", [&](storm::ui::ScriptCall &call) { call.Error(text); });
}

uint32_t UiAmount(VS_STACK *stack)
{
    const std::string what = PopText(stack), text = PopText(stack);
    return ReturnInt(stack, "UiAmount", [&](storm::ui::ScriptCall &call) { return call.Amount(text, what); });
}

uint32_t UiHasAmount(VS_STACK *stack)
{
    const std::string text = PopText(stack);
    return ReturnInt(stack, "UiHasAmount", [&](storm::ui::ScriptCall &call) { return call.HasAmount(text); });
}

uint32_t UiNumber(VS_STACK *stack)
{
    const std::string what = PopText(stack), text = PopText(stack);
    return ReturnFloat(stack, "UiNumber", [&](storm::ui::ScriptCall &call) { return call.Number(text, what); });
}

uint32_t UiDecimal(VS_STACK *stack)
{
    const std::string text = PopText(stack);
    return ReturnText(stack, "UiDecimal", [&](storm::ui::ScriptCall &call) { return call.Decimal(text); });
}

uint32_t UiOneLine(VS_STACK *stack)
{
    const std::string text = PopText(stack);
    return ReturnText(stack, "UiOneLine", [&](storm::ui::ScriptCall &call) { return call.OneLine(text); });
}

// A value the game's scripts hold, by `path`: "global" or "global.attribute.…" (of an object or a reference to one).
// It reaches a global of the game's open interface, which a segment attached before that interface was compiled
// cannot name. The global's own value is checked by the reader; an attribute must exist and hold a value (an empty
// one is a value: it is how the game writes "nothing yet").
struct GameValue
{
    VDATA *global = nullptr;               // the global itself (no attribute in the path)
    const ATTRIBUTES *attribute = nullptr; // or the attribute it names
};

GameValue FindGameValue(const std::string &path)
{
    const size_t dot = path.find('.');
    const std::string name = path.substr(0, dot);
    auto *variable = static_cast<VDATA *>(core.GetScriptVariable(name.c_str()));
    if (!variable)
        throw std::runtime_error("the game has no global " + name);
    if (variable->IsArray())
        throw std::runtime_error("the game's " + name + " is an array");
    if (dot == std::string::npos)
        return {.global = variable};
    int32_t number = 0;
    float real = 0.0f;
    const char *text = nullptr;
    if (variable->Get(number) || variable->Get(real) || variable->Get(text))
        throw std::runtime_error("the game's " + name + " is not an object");
    ATTRIBUTES *root = variable->GetAClass();
    const ATTRIBUTES *at = root ? root->FindAClass(root, path.c_str() + dot + 1) : nullptr;
    if (!at || !at->HasValue())
        throw std::runtime_error("the game has no " + path);
    return {.attribute = at};
}

// A whole number: an int global or an attribute whose whole text is one.
int GameInt(const std::string &path)
{
    const GameValue found = FindGameValue(path);
    int32_t number = 0;
    if (found.global)
    {
        if (!found.global->Get(number))
            throw std::runtime_error("the game's " + path + " is not an int");
        return number;
    }
    const std::string &value = found.attribute->GetValue();
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc() || end != value.data() + value.size() || value.empty())
        throw std::runtime_error("the game's " + path + " is '" + value + "', not a whole number");
    return number;
}

// A text: a string global or any attribute's value, "" when the game holds an empty one.
std::string GameText(const std::string &path)
{
    const GameValue found = FindGameValue(path);
    if (found.attribute)
        return found.attribute->GetValue();
    const char *text = nullptr;
    if (!found.global->Get(text))
        throw std::runtime_error("the game's " + path + " is not a string");
    return text;
}

uint32_t UiGameInt(VS_STACK *stack)
{
    const std::string path = PopText(stack);
    return ReturnInt(stack, "UiGameInt", [&](storm::ui::ScriptCall &) { return GameInt(path); });
}

uint32_t UiGameText(VS_STACK *stack)
{
    const std::string path = PopText(stack);
    return ReturnText(stack, "UiGameText", [&](storm::ui::ScriptCall &) { return GameText(path); });
}

uint32_t UiFill(VS_STACK *stack)
{
    const std::string argument = PopText(stack), text = PopText(stack);
    return ReturnText(stack, "UiFill", [&](storm::ui::ScriptCall &call) { return call.Fill(text, argument); });
}

// UiDeal(&spec, &out): spec {key, element, origin, other, low, high, answer, cutReason, more, less, disabled, reason,
// commit, offer} (storm_ui::DealSpec); out becomes {value, change, button.{label, sub, disabled, reason, busy}}, the
// button absent while there is none.
uint32_t UiDeal(VS_STACK *stack)
{
    ATTRIBUTES *out = PopObject(stack);
    ATTRIBUTES *spec = PopObject(stack);
    return Native("UiDeal", [&](storm::ui::ScriptCall &call) {
        Fields fields(*spec);
        storm::ui::DealSpec deal;
        deal.key = fields.Text("key");
        deal.element = fields.Text("element");
        deal.origin = fields.Int("origin");
        deal.other = fields.Int("other");
        deal.low = fields.Int("low");
        deal.high = fields.Int("high");
        deal.answer = fields.Int("answer");
        deal.cut_reason = fields.Text("cutReason");
        deal.more = fields.Text("more");
        deal.less = fields.Text("less");
        deal.disabled = fields.Bool("disabled");
        deal.reason = fields.Text("reason");
        deal.commit = fields.Text("commit");
        deal.offer = fields.Text("offer");
        fields.Done();
        const storm::ui::DealView view = call.Deal(deal);
        while (out->GetAttributesNum() != 0)
            out->DeleteAttributeClassX(out->GetAttributeClass(0u));
        out->SetAttribute("value", std::to_string(view.value));
        out->SetAttribute("change", std::to_string(view.change));
        if (!view.button.children.empty())
        {
            ATTRIBUTES &button = out->CreateAttribute("button");
            for (const storm::ui::ViewTree &field : view.button.children)
                button.SetAttribute(field.name, field.value);
        }
    });
}

// UiOrder(&list, field, descending): the entries of `list` in the order of their whole number `field`, stable.
uint32_t UiOrder(VS_STACK *stack)
{
    const auto descending = static_cast<VDATA *>(stack->Pop());
    const std::string field = PopText(stack);
    ATTRIBUTES *list = PopObject(stack);
    return Native("UiOrder", [&](storm::ui::ScriptCall &) {
        std::vector<int> keys;
        for (uint32_t n = 0; n < list->GetAttributesNum(); n++)
            keys.push_back(Fields(*list->GetAttributeClass(n)).Int(field.c_str()));
        std::vector<size_t> order(keys.size());
        std::iota(order.begin(), order.end(), size_t{0});
        const bool down = descending && descending->GetInt() != 0;
        std::ranges::stable_sort(order,
                                 [&](size_t a, size_t b) { return down ? keys[b] < keys[a] : keys[a] < keys[b]; });
        list->ReorderAttributes(order);
    });
}

// UiTreeLayout(&nodes, &edges, &out): nodes n<k>.id in their order, edges e<k>.{from, to} (ids, without case; other
// fields of theirs are the caller's); out
// becomes {valid, width, height, places.p<k>.{left, top} (k of nodes), links.l<j>.{from, to, points "x,y x,y …"}}
// in dp, the layout of storm_ui/tree_layout.h. Not valid (and nothing more) for a cycle or a link to an unknown id.
uint32_t UiTreeLayout(VS_STACK *stack)
{
    ATTRIBUTES *out = PopObject(stack);
    ATTRIBUTES *edges = PopObject(stack);
    ATTRIBUTES *nodes = PopObject(stack);
    return Native("UiTreeLayout", [&](storm::ui::ScriptCall &) {
        namespace tree = storm::ui::tree_layout;
        std::vector<std::string> ids;
        for (uint32_t n = 0; n < nodes->GetAttributesNum(); n++)
        {
            Fields node(*nodes->GetAttributeClass(n));
            ids.push_back(node.Text("id"));
        }
        std::vector<tree::Edge> links;
        for (uint32_t n = 0; n < edges->GetAttributesNum(); n++)
        {
            Fields edge(*edges->GetAttributeClass(n));
            links.push_back({edge.Text("from"), edge.Text("to")});
        }
        const tree::Layout layout = tree::Lay(ids, links);
        while (out->GetAttributesNum() != 0)
            out->DeleteAttributeClassX(out->GetAttributeClass(0u));
        out->SetAttribute("valid", layout.valid ? "1" : "0");
        if (!layout.valid)
            return;
        out->SetAttribute("width", std::to_string(tree::Layout::width));
        out->SetAttribute("height", std::to_string(layout.height));
        ATTRIBUTES &places = out->CreateAttribute("places");
        for (size_t k = 0; k < ids.size(); k++)
        {
            const tree::Place &place = layout.nodes.at(tree::Id(ids[k]));
            ATTRIBUTES &at = places.CreateAttribute(fmt::format("p{}", k));
            at.SetAttribute("left", std::to_string(place.left));
            at.SetAttribute("top", std::to_string(place.top));
        }
        ATTRIBUTES &ways = out->CreateAttribute("links");
        for (size_t j = 0; j < layout.links.size(); j++)
        {
            const tree::Edge &edge = layout.edges[layout.links[j].edge];
            ATTRIBUTES &at = ways.CreateAttribute(fmt::format("l{}", j));
            at.SetAttribute("from", edge.from);
            at.SetAttribute("to", edge.to);
            std::string points;
            for (const tree::Point &point : layout.links[j].points)
                points += fmt::format("{}{},{}", points.empty() ? "" : " ", point.x, point.y);
            at.SetAttribute("points", points);
        }
    });
}

uint32_t UiBack(VS_STACK *stack)
{
    const std::string node = PopText(stack);
    return Native("UiBack", [&](storm::ui::ScriptCall &call) { call.Back(node); });
}

uint32_t UiModal(VS_STACK *stack)
{
    const std::string cancel = PopText(stack), id = PopText(stack);
    return Native("UiModal", [&](storm::ui::ScriptCall &call) { call.Modal(id, cancel); });
}

uint32_t UiBackTo(VS_STACK *stack)
{
    const std::string id = PopText(stack);
    return Native("UiBackTo", [&](storm::ui::ScriptCall &call) { call.BackTo(id); });
}

uint32_t UiPending(VS_STACK *stack)
{
    const std::string id = PopText(stack);
    return ReturnInt(stack, "UiPending", [&](storm::ui::ScriptCall &call) { return call.Pending(id); });
}

uint32_t UiOffers(VS_STACK *stack)
{
    const std::string verb = PopText(stack), node = PopText(stack);
    return ReturnInt(stack, "UiOffers", [&](storm::ui::ScriptCall &call) { return call.Offers(node, verb); });
}

uint32_t UiText(VS_STACK *stack)
{
    const std::string key = PopText(stack);
    return ReturnText(stack, "UiText", [&](storm::ui::ScriptCall &call) { return call.Text(key); });
}

// The child `name` of `at`, made when it has none.
ATTRIBUTES &Child(ATTRIBUTES &at, const std::string &name)
{
    ATTRIBUTES *child = at.GetAttributeClass(name);
    return child ? *child : at.CreateAttribute(name);
}

// An image of the game as the view's "picture" and "texture" shapes read it back (screens/script_props.cpp); uv is
// the part of a texture shown, "left,top,right,bottom".
void FillImage(ATTRIBUTES &at, const storm::ui::ImageRef &image)
{
    at.SetAttribute("kind", image.kind);
    at.SetAttribute("group", image.group);
    at.SetAttribute("image", image.image);
    at.SetAttribute("uv", fmt::format("{:g},{:g},{:g},{:g}", image.uv[0], image.uv[1], image.uv[2], image.uv[3]));
}

// The verbs the engine accepts for the widget now: of its list, or its type's (Widget::Offers).
void FillOffers(ATTRIBUTES &at, const storm::ui::Widget &widget)
{
    ATTRIBUTES &offers = at.CreateAttribute("offers");
    const std::vector<std::string> defaults{"press", "select", "toggle", "edit", "left", "right"};
    for (const std::string &verb : widget.actions.empty() ? defaults : widget.actions)
        if (widget.Offers(verb))
            offers.SetAttribute(verb, "1");
}

// A table row: <model>.<node>.rows.r<index>.{index, id, label, value, enabled, reason, offers.<verb>,
// cells.c<column>.{text, color, images}, images, context.<key>}; a cell's color is the game's RGB as six hex digits.
void FillRow(ATTRIBUTES &rows, const storm::ui::Widget &widget)
{
    ATTRIBUTES &row = rows.CreateAttribute("r" + std::to_string(widget.index));
    row.SetAttribute("index", std::to_string(widget.index));
    row.SetAttribute("id", widget.row_id);
    row.SetAttribute("label", widget.label);
    row.SetAttribute("value", widget.value);
    row.SetAttribute("enabled", widget.enabled ? "1" : "0");
    row.SetAttribute("reason", widget.unavailable_reason);
    FillOffers(row, widget);
    size_t columns = widget.column_headers.size();
    for (const storm::ui::Cell &cell : widget.cells)
        columns = std::max(columns, static_cast<size_t>(cell.column) + 1);
    columns = std::max(columns, static_cast<size_t>(std::ranges::count(widget.label, '\t')) + 1);
    ATTRIBUTES &cells = row.CreateAttribute("cells");
    for (size_t column = 0; column < columns; column++)
    {
        ATTRIBUTES &at = cells.CreateAttribute("c" + std::to_string(column));
        at.SetAttribute("text", widget.CellText(static_cast<int>(column)));
        for (const storm::ui::Cell &cell : widget.cells)
            if (cell.column == static_cast<int>(column))
            {
                at.SetAttribute("color", fmt::format("{:06x}", cell.color_argb & 0xffffffu));
                for (size_t i = 0; i < cell.images.size(); i++)
                    FillImage(Child(at, "images").CreateAttribute("i" + std::to_string(i)), cell.images[i]);
            }
    }
    for (size_t i = 0; i < widget.images.size(); i++)
        FillImage(Child(row, "images").CreateAttribute("i" + std::to_string(i)), widget.images[i]);
    for (const auto &[key, value] : widget.context)
        Child(row, "context").SetAttribute(key, value);
}

// The admitted widgets as <model>.<node>.{type, label, value, min, max, step, reason, offers.<verb>, context.<key>,
// headers.c<column>, images (a node's own), rows}: a node's first widget stands for it, its widgets with an index are
// its rows (FillRow).
void FillModel(ATTRIBUTES &model, const std::vector<const storm::ui::Widget *> &widgets)
{
    for (const storm::ui::Widget *widget : widgets)
    {
        ATTRIBUTES *node = model.GetAttributeClass(widget->node);
        if (!node)
        {
            node = &model.CreateAttribute(widget->node);
            node->SetAttribute("type", widget->type);
            node->SetAttribute("label", widget->label);
            node->SetAttribute("value", widget->value);
            node->SetAttribute("reason", widget->unavailable_reason);
            if (widget->type == "slider")
            {
                node->SetAttribute("min", std::to_string(widget->min));
                node->SetAttribute("max", std::to_string(widget->max));
                node->SetAttribute("step", std::to_string(widget->step));
            }
            FillOffers(*node, *widget);
            for (const auto &[key, value] : widget->context)
                Child(*node, "context").SetAttribute(key, value);
            for (size_t column = 0; column < widget->column_headers.size(); column++)
                Child(*node, "headers").SetAttribute("c" + std::to_string(column), widget->column_headers[column]);
            if (widget->index < 0)
                for (size_t i = 0; i < widget->images.size(); i++)
                    FillImage(Child(*node, "images").CreateAttribute("i" + std::to_string(i)), widget->images[i]);
        }
        if (widget->index >= 0)
            FillRow(Child(*node, "rows"), *widget);
    }
}

void FillTree(ATTRIBUTES &at, const storm::ui::ViewTree &tree)
{
    ATTRIBUTES &child = at.CreateAttribute(tree.name);
    child.SetValue(tree.value);
    for (const storm::ui::ViewTree &leaf : tree.children)
        FillTree(child, leaf);
}

// The object of one capability's screen, UiScreens.<capability>.{capability, screen, look, data, model}, which its
// events hand to the script. Only that capability's is refilled, so a tap acts on the model of the build that bound it.
ATTRIBUTES &Ui(const storm::ui::ScriptScreen &screen)
{
    auto *screens = static_cast<VDATA *>(core.GetScriptVariable("UiScreens"));
    ATTRIBUTES *root = screens ? screens->GetAClass() : nullptr;
    if (!root)
        throw std::runtime_error("the touch interface's core script declares no object UiScreens");
    return Child(*root, screen.capability);
}

ATTRIBUTES &FillUi(const storm::ui::ScriptScreen &screen, const std::vector<const storm::ui::Widget *> &widgets)
{
    ATTRIBUTES &ui = Ui(screen);
    while (ui.GetAttributesNum() != 0)
        ui.DeleteAttributeClassX(ui.GetAttributeClass(0u));
    ui.SetAttribute("capability", screen.capability);
    ui.SetAttribute("screen", screen.script);
    FillTree(ui, screen.look);
    FillTree(ui, screen.data);
    FillModel(ui.CreateAttribute("model"), widgets);
    return ui;
}
// A native that failed, or a runtime error of the script (the interpreter logs it and goes on), fails the build or
// tap: what the script did after it rests on a half-made state.
void Failed(const storm::ui::ScriptScreen &screen, const Scope &scope, uint64_t errors, const std::string &event)
{
    if (!scope.state.failure.empty())
        throw std::runtime_error("script screen " + screen.capability + ": " + scope.state.failure);
    if (storm::android::scriptErrorEpoch != errors)
        throw std::runtime_error("script screen " + screen.capability + ": a script error in " + event +
                                 " (the error log has it)");
}
} // namespace

storm::script::Program &VmPort::Between(const storm::ui::ScriptScreen &screen, const std::string &what) const
{
    storm::script::Program *program = attacher_.Running();
    if (!program)
        throw std::runtime_error("script screen " + screen.capability + " " + what + ": no script program runs");
    if (program->Executing())
        throw std::logic_error("script screen " + screen.capability + " " + what +
                               " while a script runs: UI script events are raised only between scripts");
    return *program;
}

void VmPort::Build(const storm::ui::ScriptScreen &screen, const std::vector<const storm::ui::Widget *> &widgets,
                   storm::ui::ScriptCall &call)
{
    Between(screen, "build");
    ATTRIBUTES &ui = FillUi(screen, widgets);
    Scope scope(call);
    const uint64_t errors = storm::android::scriptErrorEpoch;
    core.Event("UiBuild", "a", &ui);
    Failed(screen, scope, errors, "UiBuild");
}

void VmPort::Event(const storm::ui::ScriptScreen &screen, const std::string &id, const std::string &verb,
                   const std::string &value, storm::ui::ScriptCall &call)
{
    Between(screen, "event " + id);
    ATTRIBUTES &ui = Ui(screen);
    if (!ui.GetAttributeClass("model"))
        throw std::logic_error("script screen " + screen.capability + " event " + id + " before its first build");
    Scope scope(call);
    const uint64_t errors = storm::android::scriptErrorEpoch;
    core.Event("UiTap", "assss", &ui, id.c_str(), verb.c_str(), value.c_str());
    Failed(screen, scope, errors, "UiTap " + id);
}
} // namespace storm::ui_scripts

namespace
{
// The natives of the touch interface's scripts ("#libriary \"UiScripts\"").
class UiScripts : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override
    {
        using namespace storm::ui_scripts;
        const struct
        {
            const char *name;
            const char *returns;
            uint32_t arguments;
            SIMPORTFUNC function;
        } natives[] = {
            {"UiShow", "void", 1, UiShow},
            {"UiPredict", "void", 1, UiPredict},
            {"UiOn", "void", 2, UiOn},
            {"UiModal", "void", 2, UiModal},
            {"UiBackTo", "void", 1, UiBackTo},
            {"UiPending", "int", 1, UiPending},
            {"UiText", "string", 1, UiText},
            {"UiSend", "void", 3, UiSend},
            {"UiSendTo", "void", 1, UiSendTo},
            {"UiBack", "void", 1, UiBack},
            {"UiOffers", "int", 2, UiOffers},
            {"UiHold", "void", 1, UiHold},
            {"UiAnswer", "string", 0, UiAnswer},
            {"UiAwait", "void", 0, UiAwait},
            {"UiRebuild", "void", 0, UiRebuild},
            {"UiFirst", "int", 0, UiFirst},
            {"UiCommand", "void", 2, UiCommand},
            {"UiPass", "void", 2, UiPass},
            {"UiTake", "int", 2, UiTake},
            {"UiState", "int", 2, UiState},
            {"UiToast", "void", 1, UiToast},
            {"UiRead", "void", 2, UiRead},
            {"UiEditText", "void", 2, UiEditText},
            {"UiError", "void", 1, UiError},
            {"UiAmount", "int", 2, UiAmount},
            {"UiHasAmount", "int", 1, UiHasAmount},
            {"UiNumber", "float", 2, UiNumber},
            {"UiDecimal", "string", 1, UiDecimal},
            {"UiOneLine", "string", 1, UiOneLine},
            {"UiFill", "string", 2, UiFill},
            {"UiDeal", "void", 2, UiDeal},
            {"UiOrder", "void", 3, UiOrder},
            {"UiGameInt", "int", 1, UiGameInt},
            {"UiGameText", "string", 1, UiGameText},
            {"UiTreeLayout", "void", 3, UiTreeLayout},
        };
        for (const auto &native : natives)
        {
            IFUNCINFO info;
            info.pFuncName = native.name;
            info.pReturnValueName = native.returns;
            info.nArguments = native.arguments;
            info.pFuncAddress = native.function;
            core.SetScriptFunction(&info);
        }
        return true;
    }
};
} // namespace

CREATE_SCRIPTLIBRIARY(UiScripts)
