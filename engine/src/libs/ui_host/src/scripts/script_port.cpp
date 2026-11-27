#include "script_port.hpp"

#include "core.h"
#include "s_import_func.h"
#include "script_libriary.h"
#include "storm_ui/interface_model.h"
#include "v_s_stack.h"
#include "vma.hpp"

#include <spdlog/spdlog.h>

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

std::string PopText(VS_STACK *stack)
{
    auto *value = static_cast<VDATA *>(stack->Pop());
    const char *text = value ? value->GetString() : nullptr;
    return text ? text : "";
}

storm::ui::ViewTree Tree(const ATTRIBUTES &attributes)
{
    storm::ui::ViewTree tree{attributes.GetThisName(), attributes.HasValue() ? attributes.GetValue() : "", {}};
    for (size_t n = 0; n < attributes.GetAttributesNum(); n++)
        tree.children.push_back(Tree(*attributes.GetAttributeClass(static_cast<uint32_t>(n))));
    return tree;
}

// Runs `act` on the running screen call. Outside one, or when it throws, the native fails: an error line, no result,
// and the build or event that ran the script fails with it.
template <class Act> uint32_t Native(const char *native, Act &&act)
{
    if (!running)
    {
        spdlog::error("{}: called outside a UI script screen's build or event", native);
        return IFUNCRESULT_FAILED;
    }
    try
    {
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

uint32_t UiShow(VS_STACK *stack)
{
    auto *view = static_cast<VDATA *>(stack->Pop());
    ATTRIBUTES *root = view ? view->GetAClass() : nullptr;
    return Native("UiShow", [&](storm::ui::ScriptCall &call) {
        if (!root)
            throw std::runtime_error("the view is not an object");
        call.Show(Tree(*root));
    });
}

uint32_t UiOn(VS_STACK *stack)
{
    const std::string verb = PopText(stack), id = PopText(stack);
    return Native("UiOn", [&](storm::ui::ScriptCall &call) { call.On(id, verb); });
}

uint32_t UiSend(VS_STACK *stack)
{
    const std::string value = PopText(stack), verb = PopText(stack), node = PopText(stack);
    return Native("UiSend", [&](storm::ui::ScriptCall &call) { call.Send(node, verb, value); });
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

uint32_t UiPending(VS_STACK *stack)
{
    const std::string id = PopText(stack);
    return Native("UiPending", [&](storm::ui::ScriptCall &call) {
        const bool pending = call.Pending(id);
        static_cast<VDATA *>(stack->Push())->Set(static_cast<int32_t>(pending));
    });
}

uint32_t UiOffers(VS_STACK *stack)
{
    const std::string verb = PopText(stack), node = PopText(stack);
    return Native("UiOffers", [&](storm::ui::ScriptCall &call) {
        const bool offers = call.Offers(node, verb);
        static_cast<VDATA *>(stack->Push())->Set(static_cast<int32_t>(offers));
    });
}

uint32_t UiText(VS_STACK *stack)
{
    const std::string key = PopText(stack);
    return Native("UiText", [&](storm::ui::ScriptCall &call) {
        const std::string text = call.Text(key);
        static_cast<VDATA *>(stack->Push())->Set(text);
    });
}

// The admitted widgets as UiModel.<node>.{type, label, value, min, max, reason}.
void FillModel(const std::vector<const storm::ui::Widget *> &widgets)
{
    auto *model = static_cast<VDATA *>(core.GetScriptVariable("UiModel"));
    ATTRIBUTES *root = model ? model->GetAClass() : nullptr;
    if (!root)
        throw std::runtime_error("the touch interface's core script declares no object UiModel");
    while (root->GetAttributesNum() != 0)
        root->DeleteAttributeClassX(root->GetAttributeClass(0u));
    for (const storm::ui::Widget *widget : widgets)
    {
        ATTRIBUTES &node = root->CreateAttribute(widget->node);
        node.SetAttribute("type", widget->type);
        node.SetAttribute("label", widget->label);
        node.SetAttribute("value", widget->value);
        node.SetAttribute("reason", widget->unavailable_reason);
        if (widget->type == "slider")
        {
            node.SetAttribute("min", std::to_string(widget->min));
            node.SetAttribute("max", std::to_string(widget->max));
        }
    }
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
    FillModel(widgets);
    Scope scope(call);
    core.Event("UiBuild", "s", screen.capability.c_str());
    if (!scope.state.failure.empty())
        throw std::runtime_error("script screen " + screen.capability + ": " + scope.state.failure);
}

void VmPort::Event(const storm::ui::ScriptScreen &screen, const std::string &id, const std::string &verb,
                   const std::string &value, storm::ui::ScriptCall &call)
{
    Between(screen, "event " + id);
    Scope scope(call);
    core.Event("UiTap", "ssss", screen.capability.c_str(), id.c_str(), verb.c_str(), value.c_str());
    if (!scope.state.failure.empty())
        throw std::runtime_error("script screen " + screen.capability + ": " + scope.state.failure);
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
        } natives[] = {{"UiShow", "void", 1, UiShow},    {"UiOn", "void", 2, UiOn},
                       {"UiSend", "void", 3, UiSend},    {"UiBack", "void", 1, UiBack},
                       {"UiModal", "void", 2, UiModal},  {"UiPending", "int", 1, UiPending},
                       {"UiOffers", "int", 2, UiOffers}, {"UiText", "string", 1, UiText}};
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
