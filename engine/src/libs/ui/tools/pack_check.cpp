// The pack contract check: the real UI pack drawn with the real screen and HUD adapters
// on the recorded models and states, without a GL context and without game data.
//
//   storm_ui_pack_check --pack DIR --fixtures DIR [--only NAME] [--crawl 0|1] [--shard K/N] [--actions FILE]
//                       [--app-contract FILE] [--flicker FILE] [--prewarm FILE]
//
// What a case is, what the crawl taps and what fails a run: ../README.md, "Pack contract check".
// Exit 0 when every case is clean, 1 otherwise, 2 for bad arguments.
#include "../tests/fakes.h"
#include "../src/components.h"
#include "../src/elements.h"
#include "../src/prewarm.h"
#include "../src/screens/screen.h"
#include "../src/template.h"
#include "storm_ui/ui.h"
#include "script_game.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace storm::ui;

namespace
{

struct Case
{
    std::string name;
    std::vector<std::pair<std::string, fs::path>> states;   // channel, file
    fs::path interface;                                     // empty = none
    fs::path answer;                                        // the game's next model of it, empty = none
    std::vector<std::string> covers;                        // screens drawn besides the interface's own
    std::vector<std::string> taps;                          // element ids tapped after it is fed (a HUD list opened)
    float width = 0.0f, height = 0.0f;                      // layout size in dp; 0: a common phone (Session)
    std::vector<std::string> whole;                         // selectors of what must be shown whole (Unseen)
    std::vector<std::string> seen;                          // the same, as the state opens: without scrolling
    std::vector<std::pair<std::string, std::string>> shows; // selector, text: shown as the state opens, may be cut
    std::vector<std::pair<std::string, std::string>> holds; // selector, text: its long press shows it, sends nothing
    std::vector<std::vector<std::string>> align; // [edge, selectors…]: what shares that edge (top, left, right)
    std::string language;                        // the game language its states name instead of theirs
};

// The font uses of every document now (src/prewarm.h), for the pack's prewarm list,
// and the layout width (dp) they were laid out at. The list is made for the common phone: a case of another size
// (Case::width, height) adds nothing to it.
std::set<FontUse> g_font_uses;
double g_layout_width = 0.0;
bool g_common_size = true;

void NoteFontUses()
{
    if (!g_common_size)
        return;
    for (int c = 0; c < Rml::GetNumContexts(); ++c)
        if (Rml::Context *context = Rml::GetContext(c))
        {
            const float ratio = context->GetDensityIndependentPixelRatio();
            g_layout_width = std::round(context->GetDimensions().x / ratio * 1000.0) / 1000.0;
            for (int d = 0; d < context->GetNumDocuments(); ++d)
                CollectFontUses(context->GetDocument(d), ratio, g_font_uses);
        }
}

// Elements the runtime marked as broken (components.cpp: an unknown prop, a failed render).
int MarkedBroken(std::vector<std::string> &where)
{
    int count = 0;
    for (int c = 0; c < Rml::GetNumContexts(); ++c)
    {
        Rml::Context *context = Rml::GetContext(c);
        for (int d = 0; context && d < context->GetNumDocuments(); ++d)
        {
            Rml::ElementList marked;
            context->GetDocument(d)->QuerySelectorAll(marked, ".ui-error");
            for (Rml::Element *element : marked)
            {
                ++count;
                where.push_back(element->GetTagName() +
                                (element->GetId().empty() ? std::string() : "#" + element->GetId()));
            }
        }
    }
    return count;
}

// What of an element the player does not see where it is now: it lies outside the screen or outside a box around it
// (one that clips it hides the rest; one that does not lets it run over its neighbours), or text in it is cut (a
// <ui-clamp> ended in an ellipsis, a box hides its own content). Empty when all of it is shown. `cut_text`: an
// ellipsis is no fault.
std::string Unseen(Rml::Element *element, bool cut_text = false)
{
    const Rml::Vector2f at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return "not drawn";
    const Rml::Vector2f screen(element->GetContext()->GetDimensions());
    constexpr float kSlack = 0.5f; // px of rounding
    const auto outside = [&](Rml::Vector2f low, Rml::Vector2f high) {
        return at.x < low.x - kSlack || at.y < low.y - kSlack || at.x + size.x > high.x + kSlack ||
               at.y + size.y > high.y + kSlack;
    };
    if (outside({0.0f, 0.0f}, screen))
        return "off the screen";
    for (Rml::Element *parent = element->GetParentNode(); parent && parent->GetParentNode();
         parent = parent->GetParentNode())
    {
        const Rml::Vector2f low = parent->GetAbsoluteOffset(Rml::BoxArea::Padding);
        if (outside(low, low + parent->GetBox().GetSize(Rml::BoxArea::Padding)))
            return (parent->GetComputedValues().overflow_x() != Rml::Style::Overflow::Visible ||
                            parent->GetComputedValues().overflow_y() != Rml::Style::Overflow::Visible
                        ? "clipped by "
                        : "runs out of ") +
                   parent->GetAddress(false, false);
    }
    if (!cut_text && ClampedTextIn(element) == 1)
        return "its text is cut short";
    Rml::ElementList boxes;
    element->QuerySelectorAll(boxes, "*");
    boxes.push_back(element);
    for (Rml::Element *box : boxes)
    {
        // A box that scrolls shows the rest of its content on a swipe; one that hides it does not.
        const Rml::Style::ComputedValues &style = box->GetComputedValues();
        if ((style.overflow_x() == Rml::Style::Overflow::Hidden &&
             box->GetScrollWidth() > box->GetClientWidth() + kSlack) ||
            (style.overflow_y() == Rml::Style::Overflow::Hidden &&
             box->GetScrollHeight() > box->GetClientHeight() + kSlack))
            return "content cut in " + box->GetAddress(false, false);
    }
    return {};
}

// The drawn element with this id, or else the first one this selector finds (any document: the HUD's and the
// overlay's too); null when there is none or it has no size.
Rml::Element *Drawn(const std::string &id)
{
    Rml::Element *element = nullptr;
    for (int c = 0; c < Rml::GetNumContexts() && !element; ++c)
        if (Rml::Context *context = Rml::GetContext(c))
            for (int d = 0; d < context->GetNumDocuments() && !element; ++d)
                element = context->GetDocument(d)->GetElementById(id);
    for (int c = 0; c < Rml::GetNumContexts() && !element; ++c)
        if (Rml::Context *context = Rml::GetContext(c))
            for (int d = 0; d < context->GetNumDocuments() && !element; ++d)
                element = context->GetDocument(d)->QuerySelector(id);
    const Rml::Vector2f size = element ? element->GetBox().GetSize(Rml::BoxArea::Border) : Rml::Vector2f();
    return size.x > 0.0f && size.y > 0.0f ? element : nullptr;
}

Rml::Vector2f Middle(Rml::Element *element)
{
    return element->GetAbsoluteOffset(Rml::BoxArea::Border) + element->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
}

// What a host did while a case ran: its error and warning lines, the actions its screens sent, the text
// editors they asked for.
struct Observed
{
    std::vector<std::string> errors, warnings;
    std::vector<Action> actions;
    std::vector<TextRequest> texts;
};

// One host with one case fed to it (states, interface, answer), settled.
class Session
{
  public:
    // `settle`: seconds drawn after the case is fed. The recorded state waits out the router's settle time (a model
    // its screen rejects closes the interface one second after the opening); the crawl's hosts replay a state that
    // was accepted already.
    Session(const fs::path &pack, const Case &c, Observed &observed, double settle = 1.5)
        : case_(c), observed_(observed)
    {
        files_ = DirectoryFiles(pack.string());
        Config config;
        config.check_taps = true;    // a drawn tap that maps to nothing is an error
        config.trace_actions = true; // the flicker check: every tap's frames until they settle (ActionTrace)
        config.files = files_.get();
        config.backend = &backend_;
        config.density = 2.625f;
        config.width = c.width > 0.0f ? static_cast<int>(std::lround(c.width * config.density)) : 2400; // 914 x 411 dp
        config.height = c.height > 0.0f ? static_cast<int>(std::lround(c.height * config.density)) : 1080;
        // No frame prediction check (Config::verify_unchanged) here: it compares draw commands, which a
        // renderer that draws nothing cannot make as the real one does. Captures check it through the
        // real backend (storm_ui_shot), the runtime checks in tests/frame_cost_check.cpp.
        config.clock = [this] { return clock_; };
        config.on_log = [this](LogLevel level, const std::string &line) {
            // A danger notice the screen shows (a failure the player is told about) is logged at the error level
            // for the device's error log (Host Shown); here it is what the case shows, not a defect.
            if (level == LogLevel::Error && line.rfind("shown danger ", 0) != 0)
                observed_.errors.push_back(line);
            else if (level == LogLevel::Warning)
                observed_.warnings.push_back(line);
        };
        config.on_action = [this](const Action &action) { observed_.actions.push_back(action); };
        config.on_text_request = [this](const TextRequest &request) { observed_.texts.push_back(request); };
        host_ = Host::Create(config, &error);
        if (host_)
        {
            host_->SetScriptScreens(ScriptGame::Get().Screens(), &ScriptGame::Get().Port());
            Feed(settle);
        }
    }

    // The crawl's way back to the case's state on the same host (a host starts slowly: fonts, the pack): the
    // interface closes and opens again as a new opening of the same model. A case drawn only from state channels
    // (a dialogue, the launcher) keeps its consumers' state; false then, and a fresh host is needed.
    bool Reopen(double settle)
    {
        if (case_.interface.empty())
            return false;
        host_->CloseInterface();
        Frames(1.0 / 60.0);
        ++reopened_;
        Feed(settle);
        return true;
    }

  private:
    // A state file as the case feeds it: with the case's language when it names one.
    std::string StateText(const fs::path &file)
    {
        if (case_.language.empty())
            return Read(file);
        Json state = Json::parse(Read(file), nullptr, false);
        if (!state.is_object() || !state.contains("language"))
        {
            failures.push_back("state " + file.string() + " names no language for the case's");
            return Read(file);
        }
        state["language"] = case_.language;
        return state.dump();
    }

    InterfaceModel Opening(const fs::path &file)
    {
        InterfaceModel parsed;
        std::string why;
        if (!ParseInterfaceModel(Read(file), parsed, &why))
            failures.push_back("interface model " + file.string() + " not accepted: " + why);
        parsed.opening += reopened_ * 1000; // every reopening is an opening of its own
        return parsed;
    }

    void Feed(double settle)
    {
        if (!reopened_)
            for (const auto &[channel, file] : case_.states)
            {
                if (!host_->SetState(channel, StateText(file)))
                    failures.push_back("state " + channel + "=" + file.string() + " not accepted");
                Frames(0.05);
            }
        if (!case_.interface.empty())
        {
            host_->ExpectInterface();
            model = Opening(case_.interface);
            host_->SetInterface(model);
            // A screen that asks the game for something on opening (a shop's selected row asks for its
            // deal) gets the model the game answers with, within the screen's hold.
            if (!case_.answer.empty())
            {
                Frames(0.05);
                AnswerRequests();
                model = Opening(case_.answer);
                host_->SetInterface(model);
            }
        }
        Frames(settle);
        AnswerAll();
        if (!reopened_)
            for (const std::string &id : case_.taps)
                Tap(id);
    }

    // A real touch on the middle of the drawn element with this id, or else the first one this selector finds.
    void Tap(const std::string &id)
    {
        Rml::Element *element = Drawn(id);
        if (!element)
        {
            failures.push_back("tap " + id + ": no such element is drawn");
            return;
        }
        const Rml::Vector2f at = Middle(element);
        host_->Touch(TouchPhase::Down, 1, at.x, at.y, clock_);
        Frames(1.0 / 60.0);
        host_->Touch(TouchPhase::Up, 1, at.x, at.y, clock_);
        Frames(0.5);
        AnswerAll();
    }

  public:
    ~Session()
    {
        host_.reset(); // before the files and the backend it uses
    }

    Host *operator->()
    {
        return host_.get();
    }
    explicit operator bool() const
    {
        return host_ != nullptr;
    }

    void Frames(double seconds)
    {
        for (double t = 0.0; t < seconds; t += 1.0 / 60.0)
        {
            clock_ += 1.0 / 60.0;
            host_->Frame();
        }
    }
    double Clock() const
    {
        return clock_;
    }

    // There is no game here: every widget action is answered as the engine answers one it took (applied, then the
    // model again as a new revision: a capability of the old one is used up), with nothing in it changed; every
    // text editor is cancelled.
    void AnswerAll()
    {
        bool applied = false, replied = false;
        for (; answered_ < observed_.actions.size(); ++answered_)
            if (const Action &action = observed_.actions[answered_]; action.request && action.screen == model.screen)
            {
                host_->Answer(action.request, true, {});
                applied = true;
            }
            else if (action.request && action.screen == "dialogue")
            {
                host_->Answer(action.request, true, {});
                replied = true;
            }
        if (applied)
        {
            ++model.revision;
            host_->SetInterface(model);
        }
        // The dialogue's model is the engine's, of its state: the game's answer to a reply is the same node again.
        for (const auto &[channel, file] : case_.states)
            if (replied && channel == "dialogue")
                host_->SetState(channel, StateText(file));
        for (; cancelled_ < observed_.texts.size(); ++cancelled_)
            host_->SubmitText(observed_.texts[cancelled_].token, {}, false);
        Frames(0.1);
    }

    // The game's answer to every widget action not answered yet (applied), without a model: a flow feeds its own.
    void AnswerRequests()
    {
        for (; answered_ < observed_.actions.size(); ++answered_)
            if (observed_.actions[answered_].request && observed_.actions[answered_].screen == model.screen)
                host_->Answer(observed_.actions[answered_].request, true, {});
    }
    // Reads a model the game answers with in a flow: the session's opening, the next revision.
    InterfaceModel Next(const fs::path &file)
    {
        InterfaceModel parsed;
        std::string why;
        if (!ParseInterfaceModel(Read(file), parsed, &why))
            failures.push_back("interface model " + file.string() + " not accepted: " + why);
        parsed.opening = model.opening;
        parsed.revision = model.revision + 1;
        model = parsed;
        return parsed;
    }

    // Lets what a tap started finish (a drag's delayed send, a step toward an amount, the screen's timed rebuilds)
    // and answers it, so that the next tap is the only thing its result comes from.
    void Settle()
    {
        AnswerAll();
        Frames(1.2);
        AnswerAll();
    }

    std::string error;
    std::vector<std::string> failures; // what of the case was not accepted
    InterfaceModel model;              // the newest interface model fed

  private:
    const Case &case_;
    int reopened_ = 0;
    Observed &observed_;
    std::unique_ptr<FileSource> files_;
    NoBackend backend_;
    std::unique_ptr<Host> host_;
    double clock_ = 0.0;
    size_t answered_ = 0, cancelled_ = 0;
};

// --- The crawl -------------------------------------------------------------------------------------------------
//
// Every state of a screen that taps reach without the game (tabs, sheets, modals, pickers) is drawn and checked
// like the recorded one: from the case's state each tap the screen draws is made with a real touch, the screen
// documents before and after it are compared, and a tap that changed what the screen offers opens a state of its
// own, crawled the same way (to kMaxDepth taps from the recorded state, kMaxStates states per case). There is no
// game: the widget actions taps send are recorded, checked against the model the screen was built from (the node
// exists, the engine offers the verb) and refused at once.

constexpr int kMaxDepth = 4;
constexpr size_t kMaxStates = 40;
constexpr double kReplaySettle = 0.1;
// Taps of one element with one verb (the rows of a list, the cells of a grid) share their binding: the first
// kSampleTaps of them and the last are made in each state.
constexpr int kSampleTaps = 3;
// Longer than any screen waits for the game's answer to a step of its own (the hero screens: 3 s).
constexpr double kStepTimeout = 5.5;

struct TapAt
{
    std::string key;   // tag#id verb value [#n]: the same tap in a fresh host with the same path
    std::string group; // tag#id verb: taps that share a binding
    Rml::Element *part = nullptr;
    ComponentElement *component = nullptr;
    bool hold = false;
    bool disabled = false; // drawn disabled: a tap may shake it or show why, and must send nothing
};

Rml::Element *ScreenElement()
{
    for (int c = 0; c < Rml::GetNumContexts(); ++c)
        if (Rml::Context *context = Rml::GetContext(c))
            for (int d = 0; d < context->GetNumDocuments(); ++d)
                if (Rml::Element *screen = context->GetDocument(d)->GetElementById("screen");
                    screen && context->GetDocument(d)->IsVisible())
                    return screen;
    return nullptr;
}

std::string EventId(Rml::Element *element)
{
    for (; element; element = element->GetParentNode())
        if (!element->GetId().empty())
            return element->GetId();
    return {};
}

void CollectTaps(Rml::Element *element, std::vector<TapAt> &out, std::map<std::string, int> &seen)
{
    if (auto *component = dynamic_cast<ComponentElement *>(element); component && component->HasBehaviors())
    {
        std::vector<Tap> taps;
        component->Taps(taps);
        for (const Tap &tap : taps)
        {
            const std::string group = component->GetTagName() + "#" + EventId(component) + " " + tap.verb;
            std::string key = group + (tap.value.empty() ? "" : " " + tap.value);
            if (const int n = seen[key]++)
                key += " #" + std::to_string(n + 1);
            out.push_back({key, group, tap.part, component, tap.hold});
        }
        // The disabled ones of a press component: the component itself and its parts marked data-disabled.
        const auto &behaviors = component->Spec().behaviors;
        const bool untouchable = component->GetComputedValues().pointer_events() == Rml::Style::PointerEvents::None;
        if (std::find(behaviors.begin(), behaviors.end(), "press") != behaviors.end() && !untouchable &&
            !component->Flag("busy"))
        {
            std::vector<std::pair<Rml::Element *, std::string>> off;
            if (component->Flag("disabled") && !component->Flag("inert") &&
                component->Spec().constants.value("pressRoot", true))
                off.emplace_back(component, "click");
            Rml::ElementList parts;
            component->QuerySelectorAll(parts, "[data-action][data-disabled]");
            for (Rml::Element *part : parts)
            {
                Rml::Element *owner = part->GetParentNode();
                while (owner && !dynamic_cast<ComponentElement *>(owner))
                    owner = owner->GetParentNode();
                if (owner == component && part->GetComputedValues().pointer_events() != Rml::Style::PointerEvents::None)
                    off.emplace_back(part, part->GetAttribute<Rml::String>("data-action", "click"));
            }
            for (const auto &[part, verb] : off)
            {
                const std::string group =
                    component->GetTagName() + "#" + EventId(component) + " " + verb + " (disabled)";
                const std::string value = part->GetAttribute<Rml::String>("data-value", "");
                std::string key = group + (value.empty() ? "" : " " + value);
                if (const int n = seen[key]++)
                    key += " #" + std::to_string(n + 1);
                out.push_back({key, group, part, component, false, true});
            }
        }
    }
    for (int i = 0; i < element->GetNumChildren(); ++i)
        CollectTaps(element->GetChild(i), out, seen);
}

std::vector<TapAt> Taps()
{
    std::vector<TapAt> taps;
    std::map<std::string, int> seen;
    if (Rml::Element *screen = ScreenElement())
        CollectTaps(screen, taps, seen);
    return taps;
}

// What the screen offers: its taps, in order. Two states offering the same taps are one state of the crawl.
std::string Signature(const std::vector<TapAt> &taps)
{
    std::string out;
    for (const TapAt &tap : taps)
        if (!tap.disabled)
            out += tap.key + "\n";
    return out;
}

// An element's tree as it is drawn: tags, classes (also those code sets, which the class attribute does not show),
// attributes and text.
void DrawnTree(Rml::Element *element, std::string &out)
{
    for (int i = 0; i < element->GetNumChildren(); ++i)
    {
        Rml::Element *child = element->GetChild(i);
        if (auto *text = rmlui_dynamic_cast<Rml::ElementText *>(child))
        {
            out += text->GetText() + "\n";
            continue;
        }
        out += "<" + child->GetTagName() + " ." + child->GetClassNames();
        for (const auto &[name, value] : child->GetAttributes())
            if (name != "class")
                out += " " + name + "=" + value.Get<Rml::String>();
        out += ">\n";
        DrawnTree(child, out);
    }
}

// What is drawn: every visible document's tree (a toast, a selection, a changed detail all show here).
std::string Drawn()
{
    NoteFontUses();
    std::string out;
    for (int c = 0; c < Rml::GetNumContexts(); ++c)
        if (Rml::Context *context = Rml::GetContext(c))
            for (int d = 0; d < context->GetNumDocuments(); ++d)
                if (context->GetDocument(d)->IsVisible())
                    DrawnTree(context->GetDocument(d), out);
    return out;
}

bool Within(Rml::Element *element, Rml::Element *ancestor)
{
    for (; element; element = element->GetParentNode())
        if (element == ancestor)
            return true;
    return false;
}

// A point of the tap's part where a finger reaches it now: the topmost element there is the part or inside
// it, and not inside another press part of it. False when the part is covered (a modal over it) or off screen.
bool Reach(Session &session, const TapAt &tap, Rml::Vector2f &at)
{
    tap.part->ScrollIntoView(false);
    session.Frames(2.0 / 60.0);
    Rml::Context *context = tap.part->GetContext();
    const Rml::Vector2f origin = tap.part->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = tap.part->GetBox().GetSize(Rml::BoxArea::Border);
    if (!context || size.x <= 0.0f || size.y <= 0.0f)
        return false;
    for (const float fx : {0.5f, 0.15f, 0.85f})
        for (const float fy : {0.5f, 0.25f, 0.75f})
        {
            at = origin + Rml::Vector2f(size.x * fx, size.y * fy);
            if (at.x < 0 || at.y < 0 || at.x >= context->GetDimensions().x || at.y >= context->GetDimensions().y)
                continue;
            Rml::Element *hit = context->GetElementAtPoint(at);
            if (!Within(hit, tap.part))
                continue;
            bool other = false;
            for (Rml::Element *at_hit = hit; at_hit && at_hit != tap.part && !other; at_hit = at_hit->GetParentNode())
                other = at_hit->HasAttribute("data-action") || dynamic_cast<ComponentElement *>(at_hit);
            if (!other)
                return true;
        }
    return false;
}

void Press(Session &session, const TapAt &tap, Rml::Vector2f at)
{
    session->Touch(TouchPhase::Down, 1, at.x, at.y, session.Clock());
    // A hold acts before it is released: the game answers it meanwhile, as the engine does within the frame.
    if (tap.hold)
        for (int step = 0; step < 15; ++step)
            session.AnswerAll();
    else
        session.Frames(1.0 / 60.0);
    session->Touch(TouchPhase::Up, 1, at.x, at.y, session.Clock());
    session.Frames(0.1);
}

// A game interface's widget action against the model its screen was built from.
std::string CheckAction(const InterfaceModel &model, const Action &action)
{
    if (action.screen != model.screen)
        return "sent to the interface " + action.screen + " while " + model.screen + " is open";
    const Widget *node = nullptr, *exact = nullptr;
    for (const Widget &widget : model.widgets)
        if (SameNode(widget.node, action.node))
        {
            node = node ? node : &widget;
            if (widget.index == action.index)
                exact = exact ? exact : &widget;
        }
    if (!node)
        return "sent to the node " + action.node + ", which the model does not have";
    if (!exact)
        return "sent to " + action.node + "[" + std::to_string(action.index) + "], a row the model does not have";
    if (!exact->actions.empty())
    {
        if (std::ranges::find(exact->actions, action.verb) != exact->actions.end())
            return {};
        std::string offers;
        for (const std::string &offer : exact->actions)
            offers += (offers.empty() ? "" : ", ") + offer;
        return "sends " + action.verb + " to " + action.node + "[" + std::to_string(action.index) +
               "], which offers only " + offers;
    }
    static const std::set<std::string> kStandard = {"button", "choice", "check", "edit", "stepper"};
    if (kStandard.count(exact->type) && !exact->Offers(action.verb))
        return "sends " + action.verb + " to the " + (exact->enabled ? "" : "disabled ") + exact->type + " " +
               action.node + "[" + std::to_string(action.index) + "], which does not take it";
    return {};
}

struct Crawl
{
    int states = 0, taps = 0, unreachable = 0;
    std::vector<std::string> findings; // errors: each with the path of taps that leads to it
    std::vector<std::string> inert;    // taps that did nothing at all
    std::vector<std::string>
        notes; // states the crawl could not open again the same way (timed screen state): not crawled further
    std::vector<std::string> flicker;  // taps whose frames showed a transient state (ActionTrace), not exempted
    std::vector<std::string> exempted; // the same, exempted in pack-contract.json "flicker" with a reason
};

// A flicker the manifest exempts ("flicker": [{case, tap, reason}], `tap` a part of the tap's key or path): its reason.
struct FlickerExemption
{
    std::string case_name, tap, reason;
};
std::vector<FlickerExemption> g_flicker_exemptions;
std::vector<std::string> g_flicker_records; // --flicker: every tap's trace, one JSON object a line

std::string TraceText(const ActionTrace &trace)
{
    char head[256];
    std::snprintf(head, sizeof head, "%d transient frame%s, %.0f ms, %.0f%% of the screen", trace.transient,
                  trace.transient == 1 ? "" : "s", trace.transient_ms, trace.area * 100.0);
    std::string text = head;
    for (const std::string &detail : trace.details)
        text += "\n        " + detail;
    return text;
}

std::string PathText(const std::vector<std::string> &path)
{
    std::string out;
    for (const std::string &key : path)
        out += (out.empty() ? "" : " > ") + key;
    return out.empty() ? "(recorded state)" : out;
}

// Makes the taps of `path` in a fresh session; false when one is no longer offered or reachable.
bool Replay(Session &session, const std::vector<std::string> &path)
{
    for (const std::string &key : path)
    {
        const std::vector<TapAt> taps = Taps();
        const auto found = std::find_if(taps.begin(), taps.end(), [&](const TapAt &tap) { return tap.key == key; });
        Rml::Vector2f at;
        if (found == taps.end() || !Reach(session, *found, at))
            return false;
        Press(session, *found, at);
        session.Settle();
    }
    return true;
}

// The crawl of one case: every state its taps open without the game, breadth first from the recorded one.
class Crawler
{
  public:
    Crawler(const fs::path &pack, const Case &c, Crawl &crawl, std::vector<std::string> &records, const Json &commands)
        : pack_(pack), c_(c), crawl_(crawl), records_(records), commands_(commands)
    {
    }
    void Run()
    {
        while (!queue_.empty() && seen_.size() < kMaxStates)
        {
            const std::vector<std::string> path = queue_.front();
            queue_.erase(queue_.begin());
            if (!Visit(path))
                return;
        }
    }

  private:
    void Finding(const std::vector<std::string> &path, const std::string &what)
    {
        if (reported_.insert(what).second)
            crawl_.findings.push_back(what + "\n      via " + PathText(path));
    }
    // The case's state again: the same host reopened, or a fresh one.
    bool Restart()
    {
        if (session_ && session_->Reopen(kReplaySettle))
            return true;
        session_.reset();
        session_ = std::make_unique<Session>(pack_, c_, observed_, kReplaySettle);
        return static_cast<bool>(*session_);
    }
    // Indexes of the taps made in a state: of each group the first kSampleTaps and the last.
    static std::vector<size_t> Sampled(const std::vector<TapAt> &all)
    {
        std::vector<size_t> sampled;
        std::map<std::string, std::vector<size_t>> groups;
        for (size_t i = 0; i < all.size(); ++i)
            groups[all[i].group].push_back(i);
        for (size_t i = 0; i < all.size(); ++i)
        {
            const std::vector<size_t> &group = groups[all[i].group];
            const size_t at = std::find(group.begin(), group.end(), i) - group.begin();
            if (at < kSampleTaps || at + 1 == group.size())
                sampled.push_back(i);
        }
        return sampled;
    }
    // The state `path` opens: its new errors, then each sampled tap in it; false when no host can be made.
    bool Visit(const std::vector<std::string> &path)
    {
        const size_t errors_before = observed_.errors.size();
        if (!Restart())
            return false;
        if (!Replay(*session_, path))
        {
            crawl_.notes.push_back(
                "the path no longer replays (a tap of it is gone or covered when the state is opened again): " +
                PathText(path));
            return true;
        }
        const std::string signature = Signature(Taps());
        if (!seen_.insert(signature).second)
            return true;
        ++crawl_.states;
        for (size_t e = errors_before; e < observed_.errors.size(); ++e)
            Finding(path, observed_.errors[e]);
        bool dirty = false;
        for (const size_t i : Sampled(Taps()))
        {
            if (dirty)
            {
                if (!Restart() || !Replay(*session_, path))
                    break;
                dirty = false;
            }
            if (!Tap(path, signature, i, dirty))
                break;
        }
        return true;
    }
    // Makes the tap `i` of the state and judges it; `dirty` when the state must be opened again for the next one.
    // False when the state does not come back the same (the rest of its taps are not made).
    bool Tap(const std::vector<std::string> &path, const std::string &signature, size_t i, bool &dirty)
    {
        std::vector<TapAt> taps = Taps();
        if (i < taps.size() && taps[i].disabled)
        {
            // Nothing left over from the taps before may look like this one's: the screen's timed work (a drag's
            // delayed send, a step's timeout) is let run out first. If that changed the state, the tap is not made.
            session_->Settle();
            session_->Frames(kStepTimeout);
            session_->AnswerAll();
            taps = Taps();
            if (Signature(taps) != signature)
            {
                dirty = true;
                return true;
            }
        }
        if (i >= taps.size() || Signature(taps) != signature)
        {
            crawl_.notes.push_back("the state does not come back the same when it is opened again: " + PathText(path));
            return false;
        }
        const TapAt &tap = taps[i];
        Rml::Vector2f at;
        if (!Reach(*session_, tap, at))
        {
            ++crawl_.unreachable;
            return true;
        }
        ++crawl_.taps;
        // Choosing what is chosen already (a selected row or tab) rightly does nothing.
        bool chosen = tap.component->Flag("selected");
        for (Rml::Element *at_part = tap.part; at_part && at_part != tap.component && !chosen;
             at_part = at_part->GetParentNode())
            chosen = at_part->IsClassSet("sel") || at_part->IsClassSet("selected");
        const std::string before = Drawn();
        const size_t errors = observed_.errors.size(), actions = observed_.actions.size(),
                     texts = observed_.texts.size();
        (*session_)->TakeActionTraces(); // the replay's taps: not this one
        Press(*session_, tap, at);
        std::vector<std::string> next = path;
        next.push_back(tap.key);
        bool acted = observed_.texts.size() > texts;
        bool asked_game = false; // a widget action the game answers: what the tap shows next is the game's
        RecordActions(tap, next, actions, acted, asked_game);
        session_->AnswerAll();
        JudgeTraces(tap, path, next, asked_game);
        // No game here either for a prediction: the same model again "corrects" it. The flows judge predictions.
        for (size_t e = errors; e < observed_.errors.size(); ++e)
            if (!(asked_game && observed_.errors[e].find("UI prediction corrected") != std::string::npos))
                Finding(next, observed_.errors[e]);
        const std::string after_signature = Signature(Taps());
        const bool moved = after_signature != signature;
        if (tap.disabled)
        {
            // It may shake or show why; it may not open anything.
            if (moved)
                Finding(next, "screen " + session_->model.screen + ": " + tap.key +
                                  " is drawn disabled and still changes what the screen offers");
            dirty = dirty || acted || moved || Drawn() != before;
            return true;
        }
        if (!acted && !moved && !chosen && Drawn() == before && observed_.errors.size() == errors)
            crawl_.inert.push_back(tap.key + " via " + PathText(path));
        if (moved)
        {
            dirty = true;
            if (static_cast<int>(next.size()) <= kMaxDepth && !seen_.count(after_signature))
                queue_.push_back(next);
        }
        else if (acted)
            dirty = true; // what the screen does after it sent something waits for the game's answer: a fresh host
        return true;
    }
    // The actions the tap sent (from the index `from` on): recorded (--actions) and checked against the model and the
    // engine's commands.
    void RecordActions(const TapAt &tap, const std::vector<std::string> &next, size_t from, bool &acted,
                       bool &asked_game)
    {
        for (size_t a = from; a < observed_.actions.size(); ++a)
        {
            const Action &action = observed_.actions[a];
            if (action.screen == "sound")
                continue;
            acted = true;
            const std::string command = to_string(action);
            asked_game = asked_game || !command.empty();
            if (tap.disabled)
                Finding(next, "screen " + action.screen + ": " + tap.key + " is drawn disabled and still sends " +
                                  (command.empty() ? action.node + " " + action.verb : command));
            Json record = {{"case", c_.name},       {"screen", action.screen}, {"node", action.node},
                           {"index", action.index}, {"verb", action.verb},     {"value", action.value},
                           {"command", command},    {"tap", tap.key},          {"path", PathText(next)}};
            if (AnsweredAction(action))
            {
                record["widget"] = true;
                const Widget *widget = session_->model.Find(action.node);
                record["type"] = widget ? widget->type : std::string();
                // A reply is checked against the dialogue's model by the router's checks (ui_screens): the session
                // holds the game interface's model only.
                if (const std::string wrong =
                        action.screen == "dialogue" ? std::string() : CheckAction(session_->model, action);
                    !wrong.empty())
                    Finding(next, "screen " + action.screen + ": " + tap.key + " " + wrong);
            }
            else if (!command.empty() &&
                     action.screen.rfind("app:", 0) != 0) // the application's screens: its Java (game_bindings.py)
            {
                bool known = false;
                for (const std::string prefix : commands_)
                    known = known || command == prefix ||
                            (prefix.back() == ':' || prefix.back() == '\t') && command.rfind(prefix, 0) == 0;
                if (!known)
                    Finding(next, "screen " + action.screen + ": " + tap.key + " sends the engine command '" + command +
                                      "', which tools/android/app_contract.json does not list");
            }
            records_.push_back(record.dump());
        }
    }
    // The flicker check: what the tap showed (Config::trace_actions, ActionTrace).
    // There is no game here: what the game answers to a widget action is the same model again, so a screen that
    // shows the asked-for state at once and then the game's (unchanged) one would count as a transient that the
    // real game does not make. Those taps are recorded, not judged: the device crawl (tools/ui_crawl) and the
    // flows of pack-contract.json "flows" (RunFlow), with the game's real answers, judge them.
    // The crawl moves on at its own pace (a screen's timed work must not run out between its taps): the tap's
    // frames so far are its trace.
    void JudgeTraces(const TapAt &tap, const std::vector<std::string> &path, const std::vector<std::string> &next,
                     bool asked_game)
    {
        for (const ActionTrace &trace : (*session_)->TakeActionTraces(true))
        {
            Json record = {{"case", c_.name},
                           {"tap", tap.key},
                           {"path", PathText(next)},
                           {"action", trace.action},
                           {"before", trace.before},
                           {"after", trace.after},
                           {"frames", trace.frames},
                           {"transient", trace.transient},
                           {"transient_ms", trace.transient_ms},
                           {"area", trace.area},
                           {"settle_ms", trace.settle_ms},
                           {"details", trace.details},
                           {"game", asked_game}};
            g_flicker_records.push_back(record.dump());
            // The latency rule holds with or without a game: what a screen predicts is drawn in the tap's frame.
            if (trace.late)
                crawl_.flicker.push_back(tap.key + " (" + trace.action + ") via " + PathText(path) +
                                         ": late feedback, the predicted result was first drawn in frame " +
                                         std::to_string(trace.feedback_frame) + " after the tap");
            if (!trace.transient || asked_game)
                continue;
            const std::string where = PathText(next);
            const auto exemption =
                std::find_if(g_flicker_exemptions.begin(), g_flicker_exemptions.end(), [&](const FlickerExemption &e) {
                    return e.case_name == c_.name && where.find(e.tap) != std::string::npos;
                });
            const std::string line =
                tap.key + " (" + trace.action + ") via " + PathText(path) + ": " + TraceText(trace);
            if (exemption != g_flicker_exemptions.end())
                crawl_.exempted.push_back(line + "\n        exempted: " + exemption->reason);
            else
                crawl_.flicker.push_back(line);
        }
    }

    const fs::path &pack_;
    const Case &c_;
    Crawl &crawl_;
    std::vector<std::string> &records_;
    const Json &commands_;
    std::vector<std::vector<std::string>> queue_ = {{}};
    std::set<std::string> seen_;     // the signatures of the states visited
    std::set<std::string> reported_; // findings said already
    Observed observed_;
    std::unique_ptr<Session> session_;
};

// The case's `whole`: every element the selector finds on the screen, scrolled into view, is shown whole (Unseen);
// `seen` (scroll false): as the screen opened.
std::vector<std::string> Whole(Session &session, const std::string &selector, bool scroll)
{
    Rml::ElementList found;
    if (Rml::Element *screen = ScreenElement())
        screen->QuerySelectorAll(found, selector);
    if (found.empty())
        return {(scroll ? "whole " : "seen ") + selector + ": nothing of it is drawn"};
    std::vector<std::string> lines;
    for (Rml::Element *element : found)
    {
        if (scroll)
        {
            element->ScrollIntoView(false);
            session.Frames(2.0 / 60.0);
        }
        if (const std::string unseen = Unseen(element); !unseen.empty())
            lines.push_back((scroll ? "whole " : "seen ") + selector + ": " + element->GetAddress(false, false) + " " +
                            unseen);
    }
    return lines;
}

// The text drawn in an element: its text nodes in order.
std::string DrawnText(Rml::Element *element)
{
    if (auto *text = dynamic_cast<Rml::ElementText *>(element))
        return text->GetText();
    std::string drawn;
    for (int i = 0; i < element->GetNumChildren(); ++i)
        drawn += DrawnText(element->GetChild(i));
    return drawn;
}

// The case's `shows`: what the selector finds as the screen opened lies whole in its boxes and draws this text (its
// own may end in an ellipsis after it).
std::vector<std::string> Shows(const std::string &selector, const std::string &text)
{
    Rml::ElementList found;
    if (Rml::Element *screen = ScreenElement())
        screen->QuerySelectorAll(found, selector);
    if (found.empty())
        return {"shows " + selector + ": nothing of it is drawn"};
    std::vector<std::string> lines;
    for (Rml::Element *element : found)
        if (const std::string unseen = Unseen(element, true); !unseen.empty())
            lines.push_back("shows " + selector + ": " + element->GetAddress(false, false) + " " + unseen);
        else if (const std::string drawn = DrawnText(element); drawn.find(text) == std::string::npos)
            lines.push_back("shows " + selector + ": \"" + drawn + "\" without \"" + text + "\"");
    return lines;
}

// The case's `holds`: a long press on what the selector finds opens the tooltip with this text, whole on the screen,
// and sends nothing, neither while held nor on release; a tap on it then sends.
std::vector<std::string> Holds(Session &session, const Observed &observed, const std::string &selector,
                               const std::string &text)
{
    Rml::Element *element = Drawn(selector);
    if (!element)
        return {"holds " + selector + ": nothing of it is drawn"};
    const Rml::Vector2f at = Middle(element);
    std::vector<std::string> lines;
    const size_t before = observed.actions.size();
    session->Touch(TouchPhase::Down, 1, at.x, at.y, session.Clock());
    session.Frames(1.0); // well past the tooltip's delay (host setting tooltipMs)
    Rml::Element *tip = Drawn("tooltip");
    if (!tip || !tip->IsClassSet("open"))
        lines.push_back("holds " + selector + ": no tooltip after a one-second press");
    else if (const std::string shown = DrawnText(tip); shown != BindShortWords(text))
        lines.push_back("holds " + selector + ": the tooltip shows \"" + shown + "\"");
    else if (const std::string unseen = Unseen(tip); !unseen.empty())
        lines.push_back("holds " + selector + ": the tooltip is " + unseen);
    session->Touch(TouchPhase::Up, 1, at.x, at.y, session.Clock());
    session.Frames(0.5);
    const size_t held = observed.actions.size();
    if (held != before)
        lines.push_back("holds " + selector + ": the long press sent " + to_string(observed.actions[before]));
    session->Touch(TouchPhase::Down, 1, at.x, at.y, session.Clock());
    session.Frames(1.0 / 60.0);
    session->Touch(TouchPhase::Up, 1, at.x, at.y, session.Clock());
    session.Frames(0.5);
    if (observed.actions.size() == held)
        lines.push_back("holds " + selector + ": a tap on it sends nothing");
    session.AnswerAll();
    return lines;
}

// The case's `align`: the first element each selector of a group finds has the group's edge (top, left or right of
// its border box) where the first one has it: side by side, not wrapped under it; in one column with its neighbours.
std::vector<std::string> Align(const std::vector<std::string> &group)
{
    const std::string &edge = group[0];
    const auto at = [&edge](Rml::Element *element) {
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        return edge == "top"    ? offset.y
               : edge == "left" ? offset.x
                                : offset.x + element->GetBox().GetSize(Rml::BoxArea::Border).x;
    };
    std::vector<std::string> lines;
    Rml::Element *screen = ScreenElement();
    Rml::Element *first = screen && group.size() > 1 ? screen->QuerySelector(group[1]) : nullptr;
    for (size_t i = 1; i < group.size(); ++i)
    {
        Rml::Element *element = screen ? screen->QuerySelector(group[i]) : nullptr;
        if (!element || !first)
            lines.push_back("align " + edge + " " + group[i] + ": nothing of it is drawn");
        else if (std::fabs(at(element) - at(first)) > 0.5f)
            lines.push_back("align " + edge + " " + group[i] + ": at " + std::to_string(at(element)) + " px, " +
                            group[1] + " at " + std::to_string(at(first)));
    }
    return lines;
}

// Runs one case: the recorded state on a host of its own, then the crawl; prints what is wrong and returns false then.
bool Run(const fs::path &pack, const Case &c, std::vector<std::string> &records, const Json &commands,
         bool crawl_states)
{
    g_common_size = c.width == 0.0f && c.height == 0.0f;
    Observed observed;
    std::vector<std::string> marked;
    int broken = 0;
    std::vector<std::string> failures;
    bool started = true;
    std::string error;
    {
        Session session(pack, c, observed);
        if (!session)
        {
            started = false;
            error = session.error;
        }
        else
        {
            broken = MarkedBroken(marked);
            NoteFontUses();
            failures = session.failures;
            for (const std::string &selector : c.seen)
                for (const std::string &line : Whole(session, selector, false))
                    failures.push_back(line);
            for (const auto &[selector, text] : c.shows)
                for (const std::string &line : Shows(selector, text))
                    failures.push_back(line);
            for (const std::vector<std::string> &group : c.align)
                for (const std::string &line : Align(group))
                    failures.push_back(line);
            for (const std::string &selector : c.whole)
                for (const std::string &line : Whole(session, selector, true))
                    failures.push_back(line);
            for (const auto &[selector, text] : c.holds)
                for (const std::string &line : Holds(session, observed, selector, text))
                    failures.push_back(line);
        }
    }
    if (!started)
    {
        std::printf("FAIL %s: the UI host does not start with this pack: %s\n", c.name.c_str(), error.c_str());
        return false;
    }
    Crawl crawl;
    if (crawl_states)
        Crawler(pack, c, crawl, records, commands).Run();
    g_common_size = true;
    const bool ok = failures.empty() && observed.errors.empty() && broken == 0 && crawl.findings.empty() &&
                    crawl.inert.empty() && crawl.flicker.empty();
    std::printf("%s %s", ok ? "PASS" : "FAIL", c.name.c_str());
    if (crawl_states)
        std::printf(" (%d state%s, %d tap%s", crawl.states, crawl.states == 1 ? "" : "s", crawl.taps,
                    crawl.taps == 1 ? "" : "s");
    if (!observed.warnings.empty())
        std::printf("%s%zu warning%s", crawl_states ? ", " : " (", observed.warnings.size(),
                    observed.warnings.size() == 1 ? "" : "s");
    if (crawl_states || !observed.warnings.empty())
        std::printf(")");
    std::printf("\n");
    for (const std::string &line : failures)
        std::printf("  failed: %s\n", line.c_str());
    for (const std::string &line : observed.errors)
        std::printf("  error: %s\n", line.c_str());
    for (const std::string &line : marked)
        std::printf("  marked ui-error: %s\n", line.c_str());
    for (const std::string &line : crawl.findings)
        std::printf("  crawl: %s\n", line.c_str());
    for (const std::string &line : crawl.inert)
        std::printf("  inert tap (does nothing at all): %s\n", line.c_str());
    for (const std::string &line : crawl.flicker)
        std::printf("  flicker: %s\n", line.c_str());
    for (const std::string &line : crawl.exempted)
        std::printf("  flicker (exempted): %s\n", line.c_str());
    for (const std::string &line : crawl.notes)
        std::printf("  note: %s\n", line.c_str());
    if (!ok)
        for (const std::string &line : observed.warnings)
            std::printf("  warning: %s\n", line.c_str());
    return ok;
}

// --- Flows: the game's real answers to a tap ------------------------------------------------------------------
//
// The crawl has no game, so a tap the game answers in steps is not judged there. A flow (pack-contract.json "flows")
// is such a tap with the game's answer as it comes, one game frame at a time: the taps (keys as the crawl names them,
// the last one is the one judged), then per game frame what the game does in it (`answer`: every widget action not
// answered yet is applied; `model`: the interface model it publishes; `states`: state channels it publishes, as the
// game does every frame; `close`: it closes the interface; `wait`: game frames before the step in which the game shows
// nothing new; nothing: a frame in which the game does nothing visible). The flicker check must find no transient frame
// in what the last tap showed until it settled, and its settled state must be `after` (a part of FrameStats::shown).
// `steady` lists selectors of what must not move: every element found keeps its box from the tap until it settled.
struct FlowStep
{
    bool answer = false, close = false;
    int wait = 0; // game frames before it in which the game shows nothing new
    fs::path model;
    std::vector<std::pair<std::string, fs::path>> states; // channel, file: state channels the game publishes in it
};
struct Flow
{
    std::string name, after;
    bool predicted = false; // the judged tap must be predicted by its screen: its result in the tap's own frame
    Case base;
    std::vector<std::string> taps;
    std::vector<FlowStep> steps;
    std::vector<std::string> steady;
};

// The boxes of what the selectors find on the screen, in order.
std::vector<Rml::Rectanglef> Boxes(const std::vector<std::string> &selectors)
{
    std::vector<Rml::Rectanglef> boxes;
    Rml::Element *screen = ScreenElement();
    for (const std::string &selector : selectors)
    {
        Rml::ElementList found;
        if (screen)
            screen->QuerySelectorAll(found, selector);
        for (Rml::Element *element : found)
            boxes.push_back(Rml::Rectanglef::FromPositionSize(element->GetAbsoluteOffset(Rml::BoxArea::Border),
                                                              element->GetBox().GetSize(Rml::BoxArea::Border)));
    }
    return boxes;
}

std::string BoxesText(const std::vector<Rml::Rectanglef> &boxes)
{
    std::string out;
    for (const Rml::Rectanglef &box : boxes)
        out += " " + std::to_string(int(std::lround(box.Left()))) + "," + std::to_string(int(std::lround(box.Top()))) +
               " " + std::to_string(int(std::lround(box.Width()))) + "x" +
               std::to_string(int(std::lround(box.Height())));
    return out;
}

bool RunFlow(const fs::path &pack, const Flow &flow)
{
    Observed observed;
    Session session(pack, flow.base, observed);
    std::vector<std::string> problems = session.failures;
    std::optional<ActionTrace> judged;
    if (!session)
        problems.push_back("the UI host does not start: " + session.error);
    for (size_t t = 0; session && problems.empty() && t < flow.taps.size(); ++t)
    {
        const std::vector<TapAt> taps = Taps();
        const auto found =
            std::find_if(taps.begin(), taps.end(), [&](const TapAt &tap) { return tap.key == flow.taps[t]; });
        Rml::Vector2f at;
        if (found == taps.end() || !Reach(session, *found, at))
        {
            std::string offered;
            for (const TapAt &tap : taps)
                offered += "\n      " + tap.key;
            problems.push_back("no tap " + flow.taps[t] + " here; the screen offers:" + offered);
            break;
        }
        if (t + 1 < flow.taps.size())
        {
            Press(session, *found, at); // a step before the judged tap (an amount)
            session.Frames(0.3);
            continue;
        }
        const std::vector<Rml::Rectanglef> steady = Boxes(flow.steady);
        if (steady.empty() != flow.steady.empty())
            problems.push_back("steady: nothing of it is drawn");
        const auto moved = [&] {
            const std::vector<Rml::Rectanglef> now = Boxes(flow.steady);
            bool same = now.size() == steady.size();
            for (size_t i = 0; same && i < now.size(); ++i)
                same = std::abs(now[i].Left() - steady[i].Left()) < 1.0f &&
                       std::abs(now[i].Top() - steady[i].Top()) < 1.0f &&
                       std::abs(now[i].Width() - steady[i].Width()) < 1.0f &&
                       std::abs(now[i].Height() - steady[i].Height()) < 1.0f;
            if (!same)
                problems.push_back("steady: moved from" + BoxesText(steady) + " to" + BoxesText(now));
            return !same;
        };
        bool shifted = false;
        session->Touch(TouchPhase::Down, 1, at.x, at.y, session.Clock());
        session->TakeActionTraces(); // the steps before it (the down ended theirs)
        session.Frames(1.0 / 60.0);
        session->Touch(TouchPhase::Up, 1, at.x, at.y, session.Clock());
        // The engine applies what the tap sent in the loop the tap reached it, answers it and publishes its model,
        // and draws the UI at that loop's end: each step is what one loop did before its frame.
        for (const FlowStep &step : flow.steps)
        {
            session.Frames(step.wait / 60.0);
            for (const auto &[channel, file] : step.states)
                if (!session->SetState(channel, Read(file)))
                    problems.push_back("state " + channel + "=" + file.string() + " not accepted");
            if (step.answer)
                session.AnswerRequests();
            if (!step.model.empty())
                session->SetInterface(session.Next(step.model));
            if (step.close)
                session->CloseInterface();
            session.Frames(1.0 / 60.0);
            if ((shifted = moved()))
                break;
        }
        std::vector<ActionTrace> traces;
        for (double waited = 0.0; waited < 2.0 && traces.empty(); waited += 1.0 / 60.0)
        {
            session.Frames(1.0 / 60.0);
            traces = session->TakeActionTraces();
        }
        if (traces.empty())
            problems.push_back("the tap's frames did not settle in 2 s");
        else
            judged = traces.back();
        if (!shifted)
            moved();
    }
    for (const std::string &error : observed.errors)
        problems.push_back("error: " + error);
    if (judged && judged->transient)
        problems.push_back("flicker: " + TraceText(*judged));
    // The latency rule: what a screen predicts is drawn in the frame of the tap.
    if (judged && flow.predicted && !judged->predicted)
        problems.push_back("the tap is not predicted by its screen (Bindings::Predict): its result waits for the game");
    if (judged && (judged->late || (flow.predicted && judged->feedback_frame != 1)))
        problems.push_back("late feedback: the first change was drawn in frame " +
                           std::to_string(judged->feedback_frame) + " after the tap, not in the tap's own frame");
    if (judged && !flow.after.empty() && judged->after.find(flow.after) == std::string::npos)
        problems.push_back("settled in '" + judged->after + "', not in '" + flow.after + "'");
    std::printf("%s flow %s", problems.empty() ? "PASS" : "FAIL", flow.name.c_str());
    if (judged)
        std::printf(" (%s -> %s in %.0f ms, %d frames; first change in frame %d%s)", judged->before.c_str(),
                    judged->after.c_str(), judged->settle_ms, judged->frames, judged->feedback_frame,
                    judged->predicted ? ", predicted" : "");
    std::printf("\n");
    for (const std::string &line : problems)
        std::printf("  %s\n", line.c_str());
    return problems.empty();
}

} // namespace

// Command line options (see --help).
struct Options
{
    fs::path pack, fixtures, actions_out, contract, flicker_out, prewarm_out;
    std::string only;
    bool crawl = true;
    int shard = 0, shards = 1; // --shard K/N: the cases K, K+N, …; the coverage check in shard 0 (run them in parallel)
};

bool ParseOptions(int argc, char **argv, Options &options)
{
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--pack")
            options.pack = value;
        else if (key == "--fixtures")
            options.fixtures = value;
        else if (key == "--only")
            options.only = value;
        else if (key == "--actions")
            options.actions_out = value;
        else if (key == "--flicker")
            options.flicker_out = value;
        else if (key == "--prewarm")
            options.prewarm_out = value;
        else if (key == "--app-contract")
            options.contract = value;
        else if (key == "--crawl")
            options.crawl = value != "0";
        else if (key == "--shard")
        {
            if (std::sscanf(value.c_str(), "%d/%d", &options.shard, &options.shards) != 2 || options.shards < 1 ||
                options.shard < 0 || options.shard >= options.shards)
            {
                std::fprintf(stderr, "--shard takes K/N with 0 <= K < N\n");
                return false;
            }
        }
        else
        {
            std::fprintf(stderr, "unknown option %s\n", key.c_str());
            return false;
        }
    }
    if (options.pack.empty() || options.fixtures.empty() || argc % 2 == 0)
    {
        std::fprintf(stderr,
                     "usage: storm_ui_pack_check --pack DIR --fixtures DIR [--only NAME] [--crawl 0|1] [--shard K/N] "
                     "[--actions FILE] [--app-contract FILE] [--flicker FILE] [--prewarm FILE]\n");
        return false;
    }
    return true;
}

// The flicker exemptions of pack-contract.json (g_flicker_exemptions); false (said) for an incomplete one.
bool ReadFlickerExemptions(const Json &manifest)
{
    for (const Json &entry : manifest.value("flicker", Json::array()))
    {
        FlickerExemption exemption{entry.value("case", std::string()), entry.value("tap", std::string()),
                                   entry.value("reason", std::string())};
        if (exemption.case_name.empty() || exemption.tap.empty() || exemption.reason.empty())
        {
            std::fprintf(stderr, "pack-contract.json: a flicker exemption needs case, tap and reason: %s\n",
                         entry.dump().c_str());
            return false;
        }
        g_flicker_exemptions.push_back(std::move(exemption));
    }

    return true;
}

// The cases of pack-contract.json; `used`: the fixtures they name. False (said) for a malformed one.
bool ReadCases(const Json &manifest, const fs::path &fixtures, std::vector<Case> &cases, std::set<fs::path> &used)
{
    for (const Json &entry : manifest["cases"])
    {
        Case c;
        c.name = entry.value("name", std::string());
        for (const Json &pair : entry.value("states", Json::array()))
        {
            const std::string text = pair.get<std::string>();
            const size_t equals = text.find('=');
            if (equals == std::string::npos)
            {
                std::fprintf(stderr, "case %s: state '%s' is not channel=file\n", c.name.c_str(), text.c_str());
                return false;
            }
            c.states.emplace_back(text.substr(0, equals), fixtures / text.substr(equals + 1));
            used.insert(c.states.back().second);
        }
        if (entry.contains("interface"))
        {
            c.interface = fixtures / entry["interface"].get<std::string>();
            used.insert(c.interface);
        }
        if (entry.contains("answer"))
        {
            c.answer = fixtures / entry["answer"].get<std::string>();
            used.insert(c.answer);
        }
        for (const Json &screen : entry.value("covers", Json::array()))
            c.covers.push_back(screen.get<std::string>());
        for (const Json &id : entry.value("taps", Json::array()))
            c.taps.push_back(id.get<std::string>());
        if (entry.contains("size") &&
            std::sscanf(entry["size"].get<std::string>().c_str(), "%fx%f", &c.width, &c.height) != 2)
        {
            std::fprintf(stderr, "case %s: size '%s' is not WxH (dp)\n", c.name.c_str(),
                         entry["size"].get<std::string>().c_str());
            return false;
        }
        for (const Json &selector : entry.value("whole", Json::array()))
            c.whole.push_back(selector.get<std::string>());
        for (const Json &selector : entry.value("seen", Json::array()))
            c.seen.push_back(selector.get<std::string>());
        for (const Json &group : entry.value("align", Json::array()))
        {
            const std::vector<std::string> parts = group.get<std::vector<std::string>>();
            if (parts.size() < 3 || (parts[0] != "top" && parts[0] != "left" && parts[0] != "right"))
            {
                std::fprintf(stderr, "case %s: align %s is not [top|left|right, selector, selector…]\n", c.name.c_str(),
                             group.dump().c_str());
                return false;
            }
            c.align.push_back(parts);
        }
        const Json shows = entry.value("shows", Json::object());
        for (const auto &[selector, text] : shows.items())
            c.shows.emplace_back(selector, text.get<std::string>());
        const Json holds = entry.value("holds", Json::object());
        for (const auto &[selector, text] : holds.items())
            c.holds.emplace_back(selector, text.get<std::string>());
        c.language = entry.value("language", std::string());
        cases.push_back(std::move(c));
    }
    return true;
}

// Every recorded model no case names is a case of its own; one that is no model is a problem.
int AddModelCases(const fs::path &fixtures, const std::set<fs::path> &used, std::vector<Case> &cases)
{
    std::vector<fs::path> models;
    for (const fs::directory_entry &file : fs::directory_iterator(fixtures / "models"))
        if (file.path().extension() == ".json")
            models.push_back(file.path());
    std::sort(models.begin(), models.end());
    int problems = 0;
    for (const fs::path &model : models)
    {
        if (used.count(model))
            continue;
        InterfaceModel parsed;
        std::string error;
        if (!ParseInterfaceModel(Read(model), parsed, &error))
        {
            std::printf("FAIL %s: neither an interface model nor named by a case of pack-contract.json (%s)\n",
                        model.filename().c_str(), error.c_str());
            ++problems;
            continue;
        }
        Case c;
        c.name = model.stem().string();
        c.interface = model;
        cases.push_back(std::move(c));
    }

    return problems;
}

// Coverage: every application screen and every script screen is drawn by a case, or listed with its reason.
int CheckCoverage(const std::vector<Case> &cases, const Json &manifest, int shard)
{
    int problems = 0;
    std::set<std::string> covered;
    for (const Case &c : cases)
    {
        covered.insert(c.covers.begin(), c.covers.end());
        if (!c.interface.empty())
        {
            InterfaceModel parsed;
            if (ParseInterfaceModel(Read(c.interface), parsed))
                covered.insert(parsed.screen);
        }
    }
    const Json &uncovered = manifest["uncovered"];
    std::vector<std::string> screens; // the application's screens and those of the scripts
    if (shard == 0)
    {
        for (const screens::ScreenEntry &entry : screens::AppScreens())
            screens.push_back(entry.id);
        // A HUD scene has no interface of the game, the generic layout and the Other sheet stand for any: each is
        // covered by its capability (a case of its states, of a window no screen draws, of one with an Other sheet).
        for (const ScriptScreen &screen : ScriptGame::Get().Screens())
            screens.push_back(screen.interface == kHudInterface || screen.interface == kAnyInterface ||
                                      screen.interface == kOtherInterface
                                  ? screen.capability
                                  : screen.interface);
    }
    for (const std::string &id : screens)
    {
        const bool listed = uncovered.contains(id);
        if (covered.count(id) && listed)
        {
            std::printf("FAIL coverage: %s is drawn by a case and still listed as uncovered in pack-contract.json\n",
                        id.c_str());
            ++problems;
        }
        else if (!covered.count(id) && !listed)
        {
            std::printf("FAIL coverage: the screen %s has no case (add a recorded model, or list it as "
                        "uncovered with the reason)\n",
                        id.c_str());
            ++problems;
        }
        else if (listed)
            std::printf("UNCOVERED %s: %s\n", id.c_str(), uncovered[id].get<std::string>().c_str());
    }

    return problems;
}

// A launcher result with a detail (the reader's entry or file names) places it by {1} in every language; one
// without has no {1}.
// Every gallery page of the pack draws without a host error: its samples give what the components read, as the
// screens do (a page nobody draws rots unseen).
int CheckGallery(const fs::path &pack)
{
    int problems = 0;
    std::vector<fs::path> pages;
    for (const auto &entry : fs::directory_iterator(pack / "gallery"))
        if (entry.path().extension() == ".rml")
            pages.push_back(entry.path());
    std::sort(pages.begin(), pages.end());
    std::unique_ptr<FileSource> files = DirectoryFiles(pack.string());
    NoBackend backend;
    double clock = 0.0;
    std::vector<std::string> errors;
    Config config = CheckConfig(*files, backend, clock);
    config.on_log = [&errors](LogLevel level, const std::string &line) {
        if (level == LogLevel::Error)
            errors.push_back(line);
    };
    std::string error;
    std::unique_ptr<Host> host = Host::Create(config, &error);
    if (!host)
    {
        std::printf("FAIL gallery: the runtime does not start: %s\n", error.c_str());
        return 1;
    }
    for (const fs::path &page : pages)
    {
        errors.clear();
        const std::string name = "gallery/" + page.filename().string();
        if (!host->LoadDocument(name))
            errors.push_back("does not load");
        clock += 1.0 / 60.0;
        host->Frame();
        for (const std::string &line : errors)
            std::printf("FAIL %s: %s\n", name.c_str(), line.c_str());
        problems += errors.empty() ? 0 : 1;
    }
    return problems;
}

int CheckLauncherStrings(const fs::path &pack)
{
    int problems = 0;
    const Json pack_json = Json::parse(Read(pack / "pack.json"), nullptr, false);
    const Json files = pack_json.value("strings", Json::object());
    for (auto file = files.begin(); file != files.end(); ++file)
    {
        const std::string language = file.key();
        const Json table = Json::parse(Read(pack / file.value().get<std::string>()), nullptr, false);
        for (const screens::LauncherResult &result : screens::LauncherResults())
        {
            const auto text = table.find(result.key);
            if (text == table.end() || !text->is_string())
                std::printf("FAIL strings %s: no %s\n", language.c_str(), result.key);
            else if ((text->get<std::string>().find("{1}") != std::string::npos) != result.detail)
                std::printf("FAIL strings %s: %s %s\n", language.c_str(), result.key,
                            result.detail ? "carries a detail but has no {1} for it" : "has {1} but no detail");
            else
                continue;
            ++problems;
        }
    }
    return problems;
}

// The flows of pack-contract.json; false (said) for a malformed one.
bool ReadFlows(const Json &manifest, const fs::path &fixtures, std::vector<Flow> &flows)
{
    for (const Json &entry : manifest.value("flows", Json::array()))
    {
        Flow flow;
        flow.name = entry.value("name", std::string());
        flow.after = entry.value("after", std::string());
        flow.predicted = entry.value("predicted", false);
        flow.base.name = "flow " + flow.name;
        if (entry.contains("interface"))
            flow.base.interface = fixtures / entry["interface"].get<std::string>();
        if (entry.contains("answer"))
            flow.base.answer = fixtures / entry["answer"].get<std::string>();
        for (const Json &tap : entry.value("taps", Json::array()))
            flow.taps.push_back(tap.get<std::string>());
        for (const Json &selector : entry.value("steady", Json::array()))
            flow.steady.push_back(selector.get<std::string>());
        for (const Json &frame : entry.value("frames", Json::array()))
        {
            FlowStep step;
            step.answer = frame.value("answer", false);
            step.close = frame.value("close", false);
            step.wait = frame.value("wait", 0);
            if (frame.contains("model"))
                step.model = fixtures / frame["model"].get<std::string>();
            for (const Json &pair : frame.value("states", Json::array()))
            {
                const std::string text = pair.get<std::string>();
                const size_t equals = text.find('=');
                if (equals == std::string::npos)
                {
                    std::fprintf(stderr, "flow %s: state '%s' is not channel=file\n", flow.name.c_str(), text.c_str());
                    return false;
                }
                step.states.emplace_back(text.substr(0, equals), fixtures / text.substr(equals + 1));
            }
            flow.steps.push_back(step);
        }
        if (flow.name.empty() || flow.base.interface.empty() || flow.taps.empty())
        {
            std::fprintf(stderr, "pack-contract.json: a flow needs name, interface and taps: %s\n",
                         entry.dump().c_str());
            return false;
        }
        flows.push_back(std::move(flow));
    }

    return true;
}

// Writes `lines`, one a line; false (said) when the file cannot be written.
bool WriteLines(const fs::path &path, const std::vector<std::string> &lines)
{
    std::ofstream out(path);
    for (const std::string &line : lines)
        out << line << "\n";
    if (!out)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

// The pack's prewarm list against what the states drew; the problems found.
int CheckPrewarm(const fs::path &pack, bool whole_run)
{
    int problems = 0;
    const Json pack_json = Json::parse(Read(pack / "pack.json"), nullptr, false);
    const std::string listed_path = pack_json.is_object() ? pack_json.value("prewarm", std::string()) : std::string();
    const Json list = listed_path.empty() ? Json() : Json::parse(Read(pack / listed_path), nullptr, false);
    if (!list.is_object() || !list.value("fonts", Json()).is_array() || !list.value("layoutWidth", Json()).is_number())
    {
        std::printf("FAIL prewarm: the pack has no list (pack.json prewarm: {layoutWidth, fonts}): "
                    "Host::Prewarm makes nothing ahead\n");
        ++problems;
    }
    else
    {
        const auto fail = [&](const std::string &line) {
            std::printf("FAIL prewarm: %s (write the list with --prewarm)\n", line.c_str());
            ++problems;
        };
        if (g_layout_width > 0.0 && list["layoutWidth"].get<double>() != g_layout_width)
            fail(listed_path + " was recorded at a layout " + Json(list["layoutWidth"]).dump() +
                 " dp wide, the cases are laid out " + Json(g_layout_width).dump() + " dp wide");
        std::set<FontUse> fonts;
        std::string why;
        for (const Json &entry : list["fonts"])
        {
            FontUse use;
            if (ParseFontUse(entry, use, why))
                fonts.insert(use);
            else
                fail(listed_path + ": " + why);
        }
        for (const FontUse &use : g_font_uses)
            if (!Covers(fonts, use))
                fail("a font use drawn but not listed: " + FontUseJson(use).dump());
        if (whole_run)
        {
            for (const FontUse &use : fonts)
                if (!g_font_uses.count(use))
                    fail("a font use listed but drawn by no state: " + FontUseJson(use).dump());
        }
    }
    return problems;
}

// The prewarm list of what the states drew (--prewarm); false (said) when it cannot be written.
bool WritePrewarm(const fs::path &path)
{
    std::ofstream out(path);
    out << "{\n \"layoutWidth\": " << Json(g_layout_width).dump() << ",\n \"fonts\": [\n";
    size_t i = 0;
    const std::set<FontUse> fonts = WithoutCoveredHandles(g_font_uses);
    for (const FontUse &use : fonts)
        out << "  " << FontUseJson(use).dump() << (++i < fonts.size() ? ",\n" : "\n");
    out << " ]\n}\n";
    if (!out)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
        return 2;
    const fs::path &pack = options.pack, &fixtures = options.fixtures;
    fs::path contract = options.contract;
    // The engine commands the application takes (engine tools/android/app_contract.json): a screen that sends
    // another one sends it to nothing.
    if (contract.empty())
        contract = fixtures / "../../../../tools/android/app_contract.json";
    const Json app_contract = Json::parse(Read(contract), nullptr, false);
    if (!app_contract.is_object() || !app_contract.contains("engine_commands"))
    {
        std::fprintf(stderr,
                     "%s: no engine_commands (pass the engine's tools/android/app_contract.json with --app-contract)\n",
                     contract.c_str());
        return 2;
    }
    const Json manifest = Json::parse(Read(fixtures / "pack-contract.json"), nullptr, false);
    if (!manifest.is_object() || !manifest.contains("cases") || !manifest.contains("uncovered"))
    {
        std::fprintf(stderr, "%s: expected {cases, uncovered}\n", (fixtures / "pack-contract.json").c_str());
        return 2;
    }

    if (!ReadFlickerExemptions(manifest))
        return 2;
    std::vector<Case> cases;
    std::set<fs::path> used; // fixtures a manifest case names
    if (!ReadCases(manifest, fixtures, cases, used))
        return 2;
    int problems = AddModelCases(fixtures, used, cases);
    problems += CheckCoverage(cases, manifest, options.shard);
    // A launcher result with a detail (the reader's entry or file names) places it by {1} in every language; one
    // without has no {1}.
    if (options.shard == 0)
    {
        problems += CheckLauncherStrings(pack);
        problems += CheckGallery(pack);
    }
    std::vector<Flow> flows;
    if (!ReadFlows(manifest, fixtures, flows))
        return 2;

    int failed = 0, ran = 0, index = -1;
    std::vector<std::string> records; // every action the crawl's taps sent, one JSON object a line (--actions)
    for (const Case &c : cases)
    {
        if (!options.only.empty() && c.name != options.only)
            continue;
        if (++index % options.shards != options.shard)
            continue;
        ++ran;
        if (!Run(pack, c, records, app_contract["engine_commands"], options.crawl))
            ++failed;
    }
    for (const Flow &flow : flows)
    {
        if (!options.only.empty() && flow.name != options.only)
            continue;
        if (++index % options.shards != options.shard)
            continue;
        ++ran;
        if (!RunFlow(pack, flow))
            ++failed;
    }
    if (!options.actions_out.empty() && !WriteLines(options.actions_out, records))
        return 2;
    if (!options.flicker_out.empty() && !WriteLines(options.flicker_out, g_flicker_records))
        return 2;
    problems += CheckPrewarm(pack, options.only.empty() && options.shards == 1);
    if (!options.prewarm_out.empty() && !WritePrewarm(options.prewarm_out))
        return 2;
    std::printf("pack contract: %d of %d cases clean, %d coverage/manifest problem%s\n", ran - failed, ran, problems,
                problems == 1 ? "" : "s");
    return failed || problems ? 1 : 0;
}
