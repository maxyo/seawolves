#include "script_screen.h"

#include "script_props.h"
#include "storm_ui/interface_model.h"

#include <stdexcept>

namespace storm::ui::screens
{
namespace
{
class ScriptScreenImpl final : public Screen, private ScriptCall
{
  public:
    ScriptScreenImpl(const ScriptScreen &screen, const ComponentSpec &component, ScriptPort &port)
        : screen_(screen), component_(component), port_(port)
    {
        for (const ScriptNode &node : screen_.nodes)
            rules_.push_back({node.node.c_str(), node.type.c_str(), node.required});
    }

    const char *Component() const override
    {
        return screen_.component.c_str();
    }
    std::span<const NodeRule> Nodes() const override
    {
        return rules_;
    }

    Json Build(const Admission &admitted, Bindings &bindings) override
    {
        bindings_ = &bindings;
        widgets_.clear();
        std::vector<const Widget *> single;
        for (const Widget *widget : admitted.known)
        {
            if (widget->index >= 0)
                throw std::runtime_error("script screen " + screen_.capability + ": node " + widget->node +
                                         " has rows; script screens take single widgets");
            widgets_.push_back(*widget);
        }
        for (const Widget &widget : widgets_)
            single.push_back(&widget);
        view_.reset();
        building_ = true;
        struct Done
        {
            bool &flag;
            ~Done()
            {
                flag = false;
            }
        } done{building_};
        port_.Build(screen_, single, *this);
        if (!view_)
            throw std::runtime_error("script screen " + screen_.capability + " showed no view");
        return std::move(*view_);
    }

  private:
    void Show(const ViewTree &view) override
    {
        if (!building_)
            throw std::runtime_error("a view is shown only while the screen builds");
        view_ = ScriptProps(view, component_);
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bindings_->On(id, verb,
                      [this](const Event &event) { port_.Event(screen_, event.id, event.verb, event.value, *this); });
    }
    void Send(const std::string &node, const std::string &verb, const std::string &value) override
    {
        bindings_->Send(Find(node), verb, value);
    }
    void Back(const std::string &node) override
    {
        bindings_->Back(node.empty() ? nullptr : &Find(node));
    }
    void Modal(const std::string &id, const std::string &cancel) override
    {
        if (cancel.empty())
        {
            bindings_->Modal(id, nullptr);
            return;
        }
        bindings_->Modal(id, [this, cancel](const Event &) { port_.Event(screen_, cancel, "", "", *this); });
    }
    bool Pending(const std::string &id) const override
    {
        return bindings_->Pending(id);
    }
    bool Offers(const std::string &node, const std::string &verb) const override
    {
        return Find(node).Offers(verb);
    }
    std::string Text(const std::string &key) const override
    {
        return bindings_->Text(key);
    }

    const Widget &Find(const std::string &node) const
    {
        for (const Widget &widget : widgets_)
            if (SameNode(widget.node, node))
                return widget;
        throw std::runtime_error("script screen " + screen_.capability + ": node " + node + " is not admitted");
    }

    const ScriptScreen screen_;
    const ComponentSpec &component_;
    ScriptPort &port_;
    std::vector<NodeRule> rules_;
    Bindings *bindings_ = nullptr;
    std::vector<Widget> widgets_; // the admitted widgets of the last build: the handlers act on them
    std::optional<Json> view_;
    bool building_ = false;
};
} // namespace

std::unique_ptr<Screen> CreateScriptScreen(const ScriptScreen &screen, const ComponentSpec &component, ScriptPort &port)
{
    return std::make_unique<ScriptScreenImpl>(screen, component, port);
}
} // namespace storm::ui::screens
