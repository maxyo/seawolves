// Spec-driven components: every <c-…> tag is described by one entry of the UI pack's
// components.json (props, template, behaviours).
#pragma once

#include "component_spec.h"
#include "shadow_element.h"
#include "template.h"

#include "storm_ui/ui.h"

#include <RmlUi/Core.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::ui
{

class ComponentElement;
struct BoundTemplate; // components.cpp: the compiled template's live elements
// Empty when `value` is acceptable for the prop, else what is wrong (enum value not listed).
std::string CheckPropValue(const PropSpec &prop, const Json &value);
// Logs and marks (class "ui-error", drawn only under a ui-debug root) every <c-…> element under `root` that has no
// spec; returns how many it found. Nested components check their own content.
int ReportUnknownComponents(Rml::Element *root);

// Services the host gives to components and behaviours.
class Services
{
  public:
    virtual ~Services() = default;
    virtual double Now() const = 0;
    virtual float Density() const = 0;
    // Safe-area insets in dp: {left, top, right, bottom}.
    virtual Json SafeArea() const = 0;
    // A number from pack.json "settings" (timings from the design tokens).
    virtual double Setting(const std::string &name, double fallback) const = 0;
    virtual void Emit(ComponentElement &source, const std::string &verb, const std::string &value) = 0;
    virtual void Toast(const std::string &text, const std::string &tone) = 0;
    // Shows the long-press tooltip next to `anchor`; an empty text hides it.
    virtual void Tooltip(Rml::Element *anchor, const std::string &text, bool below) = 0;
    // What `component`'s template, props or attributes could not do: logged once per component and text; the screen or
    // HUD scene that shows it is wrong and is replaced by the host.
    virtual void ComponentError(ComponentElement &component, const std::string &what) = 0;
    // Keeps Tick() coming to this component until it asks to stop.
    virtual void RequestTicks(ComponentElement &component, bool enable) = 0;
    virtual void Forget(ComponentElement &component) = 0;
    // The counters of the frame being made (rebuilds, layout).
    virtual FrameStats &Counters() = 0;
    // A press started on an interactive element: the gesture's one interface click (a game
    // interface's sound; the actions the press sends later are silent in the engine).
    virtual void PressStarted(ComponentElement &component) = 0;
};

struct Pointer
{
    int id = 0;
    Rml::Vector2f position;         // context coordinates, physical pixels
    Rml::Vector2f start;            // where it went down
    Rml::Element *target = nullptr; // element under the finger at down
    double time = 0.0;              // when the newest sample (position) happened, host clock
};

// A tap a component offers (Behavior::Taps): the part a pointer goes down on, the verb and value it reports, and
// whether it acts only when held (hold-to-confirm).
struct Tap
{
    Rml::Element *part = nullptr;
    std::string verb;
    std::string value;
    bool hold = false;
};

class Behavior
{
  public:
    virtual ~Behavior() = default;
    // True when the behaviour keeps the pointer for itself (drags): such a pointer never
    // scrolls a list and never reaches other elements.
    virtual bool Exclusive(const Pointer &) const
    {
        return false;
    }
    virtual bool Down(ComponentElement &, const Pointer &)
    {
        return false;
    }
    virtual void Move(ComponentElement &, const Pointer &)
    {
    }
    // `cancelled`: the finger left for a scroll or the system took the gesture.
    virtual void Up(ComponentElement &, const Pointer &, bool /*cancelled*/)
    {
    }
    virtual void Tick(ComponentElement &, double /*now*/)
    {
    }
    // After a layout pass that laid the component out or followed its rebuild: its boxes are
    // final (SettleLaidOutComponents). True when the behaviour changed styles from them; the
    // host lays the document out again before drawing, so the first frame shows the result.
    virtual bool LaidOut(ComponentElement &)
    {
        return false;
    }
    virtual void Rebuilt(ComponentElement &)
    {
    }
    // The elements a pointer may go down on for this behaviour to take it. A layer over
    // the game claims touches only there (Host::WantsTouch).
    virtual void Regions(ComponentElement &, std::vector<Rml::Element *> &) const
    {
    }
    // The taps the component offers now (press: each enabled part with its data-action, the component itself
    // with "click"). The host checks a game interface's screen maps every one of them; the pack check taps them.
    virtual void Taps(ComponentElement &, std::vector<Tap> &) const
    {
    }
    // What the player asked of the component was refused (a scrim tap on a modal that must be
    // answered): true when the behaviour shows it (press: the live class "refused" for a moment).
    virtual bool Refused(ComponentElement &)
    {
        return false;
    }
};

using BehaviorFactory = std::unique_ptr<Behavior> (*)();
void RegisterBehavior(const std::string &name, BehaviorFactory factory);
void RegisterBuiltinBehaviors();

class ComponentElement final : public ShadowedElement
{
  public:
    ComponentElement(const Rml::String &tag, const CompiledComponent &component, Services &services);
    ~ComponentElement() override;

    const Json &Props() const
    {
        return props_;
    }
    Json Prop(const std::string &name) const;
    bool Flag(const std::string &name) const
    {
        return Truthy(Prop(name));
    }
    // Merges the given props; the template's parts that read the changed ones follow on the next update.
    void SetProps(const Json &props);
    // The given props are the whole state: every prop they leave out goes back to its default (a screen's view).
    void ReplaceProps(const Json &props);
    // One prop (null: its default), as a parent template passes it.
    void SetProp(const PropSpec &prop, Json value);
    // Live state classes ("pressed", "active", "done") owned by behaviours.
    void SetLive(const std::string &name, bool on);
    void Emit(const std::string &verb, const std::string &value = {});
    Services &Host()
    {
        return services_;
    }
    const ComponentSpec &Spec() const
    {
        return spec_;
    }
    bool HasBehaviors() const
    {
        return !behaviors_.empty();
    }
    // Elements of this component that take a pointer (see Behavior::Regions).
    void TouchRegions(std::vector<Rml::Element *> &out);
    // The taps it offers now (Behavior::Taps).
    void Taps(std::vector<Tap> &taps);
    // Shows that what the player asked was refused (Behavior::Refused); false when nothing of it can.
    bool Refuse();

    // Pointer routing (called by the host).
    bool PointerExclusive(const Pointer &pointer) const;
    bool PointerDown(const Pointer &pointer);
    void PointerMove(const Pointer &pointer);
    void PointerUp(const Pointer &pointer, bool cancelled);
    void Tick(double now);
    // SettleLaidOutComponents: Behavior::LaidOut of every behaviour.
    bool SettleLayout();
    // Independent subscriptions: one behavior cannot stop another owner.
    void RequestTicks(Behavior &owner, bool enable);
    // Builds the template's elements, or brings them up to the changed props, now instead of on the next update.
    void RebuildNow();
    // Where a parent template puts the content it passes: the slot, or held aside while the template shows none
    // (it decides on `children` whether it has one); before the first build the component itself.
    Rml::Element *ContentParent();
    // The parent template changed that content: `children` follows whether there is any.
    void ContentChanged();

  protected:
    void OnAttributeChange(const Rml::ElementAttributes &changed) override;
    void OnUpdate() override;
    void OnLayout() override;

  private:
    friend struct Binder;
    void Update();
    void BindNames();
    bool HasContent();
    void PlaceContent();
    void ApplyClasses();
    void ApplyBoxSize();
    // A wrong prop, enum value or template: logged with the element and drawn as the striped "ui-error" marker
    // instead of a silent default.
    void ReportError(const std::string &what);
    // Queued for SettleLaidOutComponents once per layout pass.
    void NoteLaidOut();

    const ComponentSpec &spec_;
    const CompiledComponent &compiled_;
    Services &services_;
    Json props_ = Json::object();
    std::vector<std::unique_ptr<Behavior>> behaviors_;
    std::set<Behavior *> ticking_behaviors_;
    std::vector<std::string> prop_classes_;
    std::vector<std::string> live_classes_;
    std::vector<std::string> applied_classes_;
    std::string extra_behavior_; // added by data-behavior
    float box_size_ = -2.0f;     // constants.boxSize applied (dp), -1 = none
    bool laid_out_noted_ = false;
    bool dirty_ = false;                   // props were set: the next update compares what changed
    std::unique_ptr<BoundTemplate> bound_; // null until the first build
    Reads changed_;                        // what changed since the last build or update
    // The values of the names that are not props (SpecName), as the elements show them.
    Json children_ = false, dp_, safe_;
    unsigned strings_ = 0;                // TemplateStringsRevision() shown
    std::vector<const Json *> names_;     // what each name of the spec is now (BindNames)
    Rml::ObserverPtr<Rml::Element> slot_; // the template's slot, while it shows one
    Rml::ElementPtr held_;                // the content while the template shows no slot
    bool error_ = false;                  // ReportError happened
};

class ComponentRegistry
{
  public:
    explicit ComponentRegistry(Services &services) : services_(services)
    {
    }
    // Parses components.json, compiles every template and registers one element instancer per component tag.
    // False with the component and line of the first error: the pack is broken.
    bool Load(const Json &pack, std::string *error);
    const ComponentSpec *Find(const std::string &tag) const;

  private:
    class Instancer;
    Services &services_;
    std::unordered_map<std::string, std::unique_ptr<ComponentSpec>> specs_;
    std::unordered_map<std::string, CompiledComponent> compiled_;
    std::vector<std::unique_ptr<Rml::ElementInstancer>> instancers_;
};

// The element that holds the component's children, once it is built; null without a slot.
Rml::Element *ComponentSlot(ComponentElement &component);

// The component that takes a pointer going down on `element`: the closest ancestor-or-self
// with a behaviour that keeps the pointer for itself (drags, holds), else the closest one
// with any behaviour.
ComponentElement *InteractiveComponentAt(Rml::Element *element, const Pointer &pointer);

// After a context update: Behavior::LaidOut of every component laid out (or rebuilt) in it.
// True when one changed styles: the host updates the context again before drawing.
bool SettleLaidOutComponents();

} // namespace storm::ui
