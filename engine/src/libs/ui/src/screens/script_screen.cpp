#include "script_screen.h"

#include "controls.h"
#include "script_props.h"
#include "amount.h"
#include "../numbers.h"
#include "../text_utf8.h"
#include "storm_ui/interface_model.h"

#include <map>
#include <set>
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
        language_ = admitted.model->language;
        widgets_.clear();
        std::vector<const Widget *> known;
        for (const Widget *widget : admitted.known)
            widgets_.push_back(*widget);
        for (const Widget &widget : widgets_)
            known.push_back(&widget);
        view_.reset();
        answer_ = HoldState();
        answered_ = hold_ && Answered();
        awaited_ = false;
        dealt_.clear();
        bound_.clear();
        floor_ = bindings.RequestFloor();
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
        std::erase_if(drafts_, [this](const auto &draft) { return !dealt_.contains(draft.first); });
        if (!view_)
            throw std::runtime_error("script screen " + screen_.capability + " showed no view");
        // Before the game answers the view is the old model's with the tap's own marks; an answer the game completes
        // in later frames is not drawn until it is complete.
        const bool waiting = hold_ && (answer_ == "waiting" || (answer_ == "applied" && awaited_));
        if (waiting && awaited_ && !shown_.is_null())
        {
            // What the kept view shows of what this model has closed (an amount window) takes no input.
            for (const std::string &bound : shown_bound_)
                if (!bound_.contains(bound))
                    bindings.Ignore(bound.substr(0, bound.find('\x1f')), bound.substr(bound.find('\x1f') + 1));
            bindings.RebuildAfter(hold_until_ - bindings.Now());
            return shown_;
        }
        if (waiting)
            bindings.RebuildAfter(hold_until_ - bindings.Now());
        else if (hold_ && answer_ == "late" && (awaited_ || !answered_))
            bindings.Error("script screen " + screen_.capability + ": the game did not answer in " +
                           std::to_string(hold_seconds_) + " s");
        hold_ = waiting;
        shown_ = *view_;
        shown_bound_ = bound_;
        return std::move(*view_);
    }

  private:
    // What became of the hold, as the build that runs under it sees it.
    std::string HoldState() const
    {
        if (!hold_)
            return "";
        if (Answers() == Bindings::Answers::Refused)
            return "refused";
        if (bindings_->Now() >= hold_until_)
            return "late";
        return Answered() ? "applied" : "waiting";
    }
    Bindings::Answers Answers() const
    {
        const bool sent = bindings_->RequestFloor() != hold_floor_;
        return sent ? bindings_->AnsweredSince(hold_floor_) : Bindings::Answers::Applied;
    }
    bool Answered() const
    {
        return Answers() == Bindings::Answers::Applied;
    }

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
        bound_.insert(id + '\x1f' + verb);
        bindings_->On(id, verb, [this](const Event &event) { Raise(event.id, event.verb, event.value); });
    }
    void Raise(const std::string &id, const std::string &verb, const std::string &value)
    {
        floor_ = bindings_->RequestFloor();
        port_.Event(screen_, id, verb, value, *this);
    }
    void Send(const std::string &node, int row, const std::string &verb, const std::string &value,
              const std::string &guard, const std::string &lead) override
    {
        const Widget &widget = Find(node, row);
        if (!lead.empty())
            bindings_->Send(widget, verb, lead, guard);
        bindings_->Send(widget, verb, value, guard);
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
        bindings_->Modal(id, [this, cancel](const Event &) { Raise(cancel, "", ""); });
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

    void Hold(double seconds) override
    {
        if (!(seconds > 0.0))
            throw std::runtime_error("a hold lasts more than 0 s");
        hold_ = true;
        hold_floor_ = floor_;
        hold_seconds_ = seconds;
        hold_until_ = bindings_->Now() + seconds;
        if (building_)
            answer_ = HoldState();
        bindings_->RebuildAfter(seconds);
    }
    std::string Answer() const override
    {
        if (!building_)
            throw std::runtime_error("the answer of a hold is read only while the screen builds");
        return answer_;
    }
    void Await() override
    {
        if (!building_)
            throw std::runtime_error("an answer is awaited only while the screen builds");
        awaited_ = true;
        bindings_->Await();
    }
    void Rebuild() override
    {
        bindings_->Rebuild();
    }
    bool First() const override
    {
        return first_;
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
        throw std::runtime_error(text);
    }
    int Amount(const std::string &text, const std::string &what) const override
    {
        if (const std::optional<int> value = ParseAmount(text))
            return *value;
        throw std::runtime_error("no number in " + what + " ('" + text + "')");
    }
    bool HasAmount(const std::string &text) const override
    {
        return ParseAmount(text).has_value();
    }
    double Number(const std::string &text, const std::string &what) const override
    {
        if (const std::optional<double> value = ParseNumber<double>(StripWhitespace(text)))
            return *value;
        throw std::runtime_error("no number in " + what + " ('" + text + "')");
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
    DealView Deal(const DealSpec &spec) override
    {
        if (!building_)
            throw std::runtime_error("a deal is asked for only while the screen builds");
        if (!dealt_.insert(spec.element).second)
            throw std::runtime_error("script screen " + screen_.capability + ": two deals of " + spec.element);
        for (const char *verb : {"change", "commit"})
            bound_.insert(spec.element + '\x1f' + verb);
        bound_.insert(std::string("primary") + '\x1f');
        AmountDraft &draft = drafts_[spec.element];
        draft.Follow(spec.key, spec.origin, spec.other, *bindings_);
        draft.Bind(*bindings_, spec.element, spec.low, spec.high, spec.more_reason, 1, spec.less_reason);
        DealView view;
        view.value = draft.Value(spec.low, spec.high);
        view.change = view.value - draft.Origin();
        view.button.name = "button";
        const Json button = draft.Buttons(
            *bindings_, spec.low, spec.high, {spec.more.c_str(), spec.less.c_str()}, "", spec.disabled, spec.reason,
            [this, commit = spec.commit](int change) { Raise(commit, "", std::to_string(change)); });
        if (!button.is_null())
            for (const auto &[key, value] : button.items())
                view.button.children.push_back(
                    {key, value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.get<std::string>(), {}});
        return view;
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
    Json shown_; // the view of the last build shown
    bool building_ = false;
    bool first_ = true;
    std::string language_;
    uint64_t floor_ = 0; // the first widget action of the running build or event
    // The hold: the view shown stays until the widget actions from hold_floor_ on are answered.
    bool hold_ = false;
    uint64_t hold_floor_ = 0;
    double hold_seconds_ = 0.0, hold_until_ = 0.0;
    std::string answer_;    // HoldState at the start of the running build
    bool answered_ = false; // the game had answered before the running build
    bool awaited_ = false;
    std::map<std::string, AmountDraft> drafts_; // the deals by their element
    std::set<std::string> dealt_;               // the elements the running build asked a deal of
    std::set<std::string> bound_, shown_bound_; // "<id>\x1f<verb>" the running build and the shown one bound
};
} // namespace

std::unique_ptr<Screen> CreateScriptScreen(const ScriptScreen &screen, const ComponentSpec &component, ScriptPort &port)
{
    return std::make_unique<ScriptScreenImpl>(screen, component, port);
}
} // namespace storm::ui::screens
