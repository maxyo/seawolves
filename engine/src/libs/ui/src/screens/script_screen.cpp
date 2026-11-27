#include "script_screen.h"

#include "controls.h"
#include "script_props.h"
#include "storm_ui/interface_model.h"

#include <map>
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
        revision_ = admitted.model->revision;
        language_ = admitted.model->language;
        widgets_.clear();
        std::vector<const Widget *> known;
        for (const Widget *widget : admitted.known)
            widgets_.push_back(*widget);
        for (const Widget &widget : widgets_)
            known.push_back(&widget);
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
        port_.Build(screen_, known, *this);
        first_ = false;
        if (!view_)
            throw std::runtime_error("script screen " + screen_.capability + " showed no view");
        shown_ = *view_;
        return std::move(*view_);
    }

  private:
    void Show(const ViewTree &view) override
    {
        if (!building_)
            throw std::runtime_error("a view is shown only while the screen builds");
        view_ = ScriptProps(view, component_);
    }
    // The prediction is the change from the view of the last build, laid over every view built until the game has
    // answered: the busy marks and anything else those builds change stay theirs.
    void Predict(const ViewTree &view) override
    {
        if (building_)
            throw std::runtime_error("a prediction is made only in an event");
        bindings_->PredictThis([change = Json::diff(shown_, ScriptProps(view, component_))](
                                   const Event &, Json &built) { built = built.patch(change); });
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bindings_->On(id, verb,
                      [this](const Event &event) { port_.Event(screen_, event.id, event.verb, event.value, *this); });
    }
    void Send(const std::string &node, int row, const std::string &verb, const std::string &value,
              const std::string &guard) override
    {
        bindings_->Send(Find(node, row), verb, value, guard);
    }
    void SendAfter(const std::string &node, int row, const std::string &verb, const std::string &value,
                   const std::string &guard, double seconds, const std::string &lead) override
    {
        bindings_->SendAfter(seconds, Find(node, row), verb, value, guard,
                             lead.empty() ? std::nullopt : std::optional<std::string>(lead));
    }
    bool Waiting(const std::string &node, int row, const std::string &verb) const override
    {
        return bindings_->Waiting(Find(node, row), verb);
    }
    void Back(const std::string &node) override
    {
        bindings_->Back(node.empty() ? nullptr : &Find(node, -1));
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
        return Find(node, -1).Offers(verb);
    }
    std::string Text(const std::string &key) const override
    {
        return bindings_->Text(key);
    }

    void Ignore(const std::string &id, const std::string &verb) override
    {
        bindings_->Ignore(id, verb);
    }
    bool Bound(const std::string &id, const std::string &verb) const override
    {
        return bindings_->Bound(id, verb);
    }
    void Keep() override
    {
        if (!building_ || shown_.is_null())
            throw std::runtime_error("a view is kept only while the screen builds, after it has shown one");
        view_ = shown_;
    }
    void Await() override
    {
        bindings_->Await();
    }
    void Rebuild() override
    {
        bindings_->Rebuild();
    }
    void RebuildAfter(double seconds) override
    {
        bindings_->RebuildAfter(seconds);
    }
    bool First() const override
    {
        return first_;
    }
    uint64_t Revision() const override
    {
        return revision_;
    }
    double Now() const override
    {
        return bindings_->Now();
    }
    uint64_t RequestFloor() const override
    {
        return bindings_->RequestFloor();
    }
    int AnsweredSince(uint64_t floor) const override
    {
        switch (bindings_->AnsweredSince(floor))
        {
        case Bindings::Answers::Waiting:
            return 0;
        case Bindings::Answers::Applied:
            return 1;
        case Bindings::Answers::Refused:
            return 2;
        }
        throw std::logic_error("an answer that is none of Bindings::Answers");
    }
    void Toast(const std::string &text) override
    {
        bindings_->Toast(text);
    }
    void Read(const std::string &title, const std::string &text) override
    {
        bindings_->Read(title, text);
    }
    void Error(const std::string &text) override
    {
        bindings_->Error(text);
    }
    int Amount(const std::string &text, const std::string &what, int none) override
    {
        return screens::Amount(*bindings_, text, what.c_str(), none);
    }
    bool HasAmount(const std::string &text) const override
    {
        return ParseAmount(text).has_value();
    }
    double Number(const std::string &text, const std::string &what, double none) override
    {
        return screens::Number(*bindings_, text, what.c_str(), none);
    }
    std::string Decimal(const std::string &text) const override
    {
        return screens::Decimal(text, language_);
    }
    std::string OneLine(const std::string &text) const override
    {
        return screens::OneLine(text);
    }
    std::string Fill(const std::string &text, const std::string &argument) const override
    {
        return screens::Fill(text, {argument});
    }
    void DraftFollow(const std::string &id, const std::string &key, int origin, int other) override
    {
        drafts_[id].Follow(key, origin, other, *bindings_);
    }
    void DraftClose(const std::string &id) override
    {
        drafts_[id].Close();
    }
    int DraftOrigin(const std::string &id) const override
    {
        return Draft(id).Origin();
    }
    int DraftOther(const std::string &id) const override
    {
        return Draft(id).Other();
    }
    int DraftValue(const std::string &id, int low, int high) const override
    {
        return Draft(id).Value(low, high);
    }
    void DraftBind(const std::string &id, const std::string &element, int low, int high, const std::string &more_reason,
                   int grain) override
    {
        Draft(id).Bind(*bindings_, element, low, high, more_reason, grain);
    }
    ViewTree DraftButton(const std::string &id, int low, int high, const std::string &more, const std::string &less,
                         const std::string &sub, bool disabled, const std::string &reason,
                         const std::string &commit) override
    {
        const Json button = Draft(id).Buttons(
            *bindings_, low, high, {more.c_str(), less.c_str()}, sub, disabled, reason,
            [this, commit](int delta) { port_.Event(screen_, commit, "", std::to_string(delta), *this); });
        ViewTree tree{"button", "", {}};
        if (!button.is_null())
            for (const auto &[key, value] : button.items())
                tree.children.push_back(
                    {key, value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.get<std::string>(), {}});
        return tree;
    }

    // A draft the script follows; one it never followed is a fault of the script.
    AmountDraft &Draft(const std::string &id) const
    {
        const auto draft = drafts_.find(id);
        if (draft == drafts_.end())
            throw std::runtime_error("script screen " + screen_.capability + ": no amount draft " + id);
        return draft->second;
    }
    // The node's own widget (row -1), or its row `row`.
    const Widget &Find(const std::string &node, int row) const
    {
        for (const Widget &widget : widgets_)
            if (SameNode(widget.node, node) && (row < 0 || widget.index == row))
                return widget;
        throw std::runtime_error("script screen " + screen_.capability + ": node " + node +
                                 (row < 0 ? "" : " row " + std::to_string(row)) + " is not admitted");
    }

    const ScriptScreen screen_;
    const ComponentSpec &component_;
    ScriptPort &port_;
    std::vector<NodeRule> rules_;
    Bindings *bindings_ = nullptr;
    std::vector<Widget> widgets_; // the admitted widgets of the last build: the handlers act on them
    std::optional<Json> view_;
    Json shown_; // the view of the last build
    bool building_ = false;
    bool first_ = true;
    uint64_t revision_ = 0;
    std::string language_;
    mutable std::map<std::string, AmountDraft> drafts_; // the quantity controls' drafts by the script's ids
};
} // namespace

std::unique_ptr<Screen> CreateScriptScreen(const ScriptScreen &screen, const ComponentSpec &component, ScriptPort &port)
{
    return std::make_unique<ScriptScreenImpl>(screen, component, port);
}
} // namespace storm::ui::screens
