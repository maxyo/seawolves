#include "script_screen.h"

#include "app_settings.h"
#include "deal.h"
#include "../format.h"
#include "screen.h"
#include "script_props.h"
#include "../numbers.h"
#include "../text_utf8.h"
#include "storm_ui/interface_model.h"
#include "app_ui_names.hpp"

#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <variant>

namespace storm::ui::screens
{
namespace
{
// An answer the game completes in later frames need not change what the interface model carries (the rest window's
// hours are a global its slider handler sets a frame after the move): the build that awaits it runs again this long
// after the answer, and past it the build is "late". The journal's walk measured the game's slowest step at 3 s.
constexpr double kAwaitedSeconds = 3.0;

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
    bool OwnsBack() const override
    {
        return !back_.empty();
    }
    void Back(Bindings &) override
    {
        Raise(back_, "", "");
    }
    std::string Binding() const override
    {
        return screen_.capability;
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
        awaited_ = false;
        dealt_.clear();
        bound_.clear();
        back_.clear();
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
        std::erase_if(deals_, [this](const auto &deal) { return !dealt_.contains(deal.first); });
        if (!view_)
            throw std::runtime_error("script screen " + screen_.capability + " showed no view");
        // Before the game answers the view is the old model's with the tap's own marks; an answer the game completes
        // in later frames is not drawn until it is complete.
        const bool waiting = hold_ && (answer_ == "waiting" || (answer_ == "applied" && awaited_));
        if (waiting && answer_ == "applied")
        {
            if (!awaited_since_)
                awaited_since_ = bindings.Now();
            bindings.RebuildAfter(*awaited_since_ + kAwaitedSeconds - bindings.Now());
        }
        if (waiting && awaited_ && shown_)
        {
            // What the kept view shows of what this model has closed (an amount window) takes no input.
            for (const std::string &bound : shown_bound_)
                if (!bound_.contains(bound))
                    bindings.Ignore(bound.substr(0, bound.find('\x1f')), bound.substr(bound.find('\x1f') + 1));
            return Json(); // the router keeps the view it drew
        }
        if (hold_ && answer_ == "late" && awaited_)
            bindings.Error("script screen " + screen_.capability + ": the game did not complete its answer in " +
                           std::to_string(int(kAwaitedSeconds)) + " s");
        hold_ = waiting;
        if (!hold_)
            awaited_since_.reset();
        shown_ = true;
        shown_bound_ = bound_;
        return std::move(*view_);
    }

  private:
    // What became of the hold, as the build that runs under it sees it.
    std::string HoldState() const
    {
        if (!hold_)
            return "";
        switch (Answers())
        {
        case Bindings::Answers::Refused:
            return "refused";
        case Bindings::Answers::Waiting:
            return "waiting";
        default:
            return awaited_since_ && bindings_->Now() >= *awaited_since_ + kAwaitedSeconds ? "late" : "applied";
        }
    }
    Bindings::Answers Answers() const
    {
        const bool sent = bindings_->RequestFloor() != hold_floor_;
        return sent ? bindings_->AnsweredSince(hold_floor_) : Bindings::Answers::Applied;
    }

    void Show(const ViewNode &view) override
    {
        if (!building_)
            throw std::runtime_error("a view is shown only while the screen builds");
        view_ = ScriptProps(view, component_);
    }
    // The router keeps what the view the tap leads to changes, by field and row id, until the game has answered.
    void Predict(const ViewNode &view) override
    {
        if (building_)
            throw std::runtime_error("a prediction is made only in an event");
        bindings_->PredictThis(ScriptProps(view, component_));
    }
    void On(const std::string &id, const std::string &verb) override
    {
        bound_.insert(id + '\x1f' + verb);
        bindings_->On(id, verb, [this](const Event &event) {
            event_ = &event;
            Raise(event.id, event.verb, event.value);
            event_ = nullptr;
        });
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
    void BackTo(const std::string &id) override
    {
        if (!building_)
            throw std::runtime_error("the back key is taken only while the screen builds");
        back_ = id;
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

    void Hold() override
    {
        // A hold that is still waiting keeps its first action: a move made meanwhile that sent nothing answers none.
        hold_floor_ = hold_ ? std::min(hold_floor_, floor_) : floor_;
        hold_ = true;
        awaited_since_.reset();
        if (building_)
            answer_ = HoldState();
        else
            bindings_->BuildLater();
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
    void Command(const ScriptCommand &command) override
    {
        std::visit([this](const auto &asked) { Run(asked); }, command);
    }
    void Run(const HostCommand &command)
    {
        namespace host = app::host_action;
        for (const char *known : {host::app_about, host::app_cache_clear, host::app_cache_measure,
                                  host::app_exit_to_main_menu, host::app_settings})
            if (command.action == known)
                return bindings_->SendHost(command.action, "");
        throw std::runtime_error("'" + command.action + "' is not a host action of the application");
    }
    void Run(const QuickCommand &command)
    {
        if (command.which != "save" && command.which != "load")
            throw std::runtime_error("'" + command.which + "' is neither the quick save nor the quick load");
        bindings_->QuickGame(command.which);
    }
    // The title's commands of the application's own title (Bindings::Title: the engine's "main:").
    void Run(const TitleCommand &command)
    {
        const std::string &name = command.command;
        if (name != "New" && name != "Load" && name != "Options" && name != "Credits" && name != "Exit" &&
            name != "Continue")
            throw std::runtime_error("'" + name + "' is not a title command");
        bindings_->Title(name);
    }
    void Run(const SettingsCommand &command)
    {
        if (command.verb == "open")
            return OpenAppSettings(*bindings_);
        if (command.verb == "keep")
            return KeepAppSettings(*bindings_);
        throw std::runtime_error("'" + command.verb + "' is neither open nor keep of the settings");
    }
    void Run(const SettingCommand &command)
    {
        SetAppSetting(*bindings_, command.key, command.value);
    }
    template <class HudCommand> void Run(const HudCommand &)
    {
        throw std::runtime_error("a HUD command is sent by a HUD scene, not by the screen " + screen_.capability);
    }
    void Pass(const std::string &screen, const ViewTree &note) override
    {
        bindings_->Pass(screen, note);
    }
    std::optional<ViewTree> Take(const std::string &screen) override
    {
        return bindings_->Take(screen);
    }
    std::optional<ViewTree> State(const std::string &channel) const override
    {
        // The engine's own table of the application's settings, made from the host's and the renderer's channels.
        if (channel == kAppOptionsChannel)
            return StateTree(channel, AppOptions(*bindings_));
        const Json &state = bindings_->State(channel);
        if (state.is_null())
            return std::nullopt;
        return StateTree(channel, state);
    }
    void Toast(const std::string &text) override
    {
        bindings_->Toast(text);
    }
    void Read(const std::string &title, const std::string &text) override
    {
        bindings_->Read(title, text);
    }
    void EditText(const std::string &node, const std::string &title) override
    {
        if (!event_)
            throw std::runtime_error("a text is asked only in a tap of an element");
        bindings_->EditText(*event_, Find(node, -1), title, false);
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
        return ui::Decimal(text, language_);
    }
    std::string OneLine(const std::string &text) const override
    {
        return ui::OneLine(text);
    }
    std::string Fill(const std::string &text, const std::string &argument) const override
    {
        return ui::Fill(text, {argument});
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
        DealState &deal = deals_[spec.element];
        AmountDraft &draft = deal.draft;
        draft.Follow(spec.key, spec.origin, spec.other, *bindings_);
        // The amount asked for shows until the game has answered it; then the game's, and why it is less.
        if (deal.asking && bindings_->AnsweredSince(deal.floor) != Bindings::Answers::Waiting)
        {
            deal.asking = false;
            if (spec.answer != deal.asked && !spec.cut_reason.empty() && bindings_->Now() - deal.said_at > 1.5)
            {
                deal.said_at = bindings_->Now();
                bindings_->Toast(spec.cut_reason);
            }
        }
        if (!deal.asking)
            draft.Answered(spec.answer);
        draft.Bind(*bindings_, spec.element, spec.low, spec.high, {}, {},
                   [this, &deal, offer = spec.offer, origin = spec.origin, answer = spec.answer](int target) {
                       if (target == answer && !deal.asking)
                           return;
                       deal.asking = true;
                       deal.asked = target;
                       deal.floor = bindings_->RequestFloor();
                       Raise(offer, "", std::to_string(target - origin));
                   });
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
    bool shown_ = false; // a build of this screen was shown
    bool building_ = false;
    bool first_ = true;
    std::string language_;
    uint64_t floor_ = 0; // the first widget action of the running build or event
    // The hold: the view shown stays until the widget actions from hold_floor_ on are answered (the ledger), and
    // while the builds after the answer await the rest of it, at most kAwaitedSeconds from the first of them.
    bool hold_ = false;
    uint64_t hold_floor_ = 0;
    std::optional<double> awaited_since_;
    std::string answer_; // HoldState at the start of the running build
    bool awaited_ = false;
    struct DealState
    {
        AmountDraft draft;
        bool asking = false; // an amount was offered to the game and is not answered yet
        int asked = 0;
        uint64_t floor = 0;     // the first request of that offer
        double said_at = -10.0; // when the game's cut was last said (once per push: a drag offers many)
    };
    std::map<std::string, DealState> deals_;    // the deals by their element
    std::set<std::string> dealt_;               // the elements the running build asked a deal of
    std::set<std::string> bound_, shown_bound_; // "<id>\x1f<verb>" the running build and the shown one bound
    std::string back_;                          // the element the back key raises, "" the router's way out
    const Event *event_ = nullptr;              // the tap of an element the script is running for
};
} // namespace

std::unique_ptr<Screen> CreateScriptScreen(const ScriptScreen &screen, const ComponentSpec &component, ScriptPort &port)
{
    return std::make_unique<ScriptScreenImpl>(screen, component, port);
}
} // namespace storm::ui::screens
