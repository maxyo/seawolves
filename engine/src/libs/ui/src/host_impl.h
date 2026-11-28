// The UI host behind Host::Create as one class; its members are defined in ui_host.cpp and, by concern, in the
// host_*.cpp files beside it.
#pragma once

#include "storm_ui/ui.h"
#include "storm_ui/render_backend.h"

#include "action_trace.h"
#include "components.h"
#include "touch_motion.h"
#include "frame_renderer.h"
#include "hud/hud.h"
#include "prewarm.h"
#include "screens/screen.h"
#include "storm_ui/interface_model.h"

#include <RmlUi/Core.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <mutex>
#include <set>
#include <unordered_map>

namespace storm::ui
{

class PackFiles;
class CountedDocumentInstancer;

class System final : public Rml::SystemInterface
{
  public:
    std::function<double()> clock;
    std::function<void(LogLevel, const std::string &)> sink;

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;
};

inline double Milliseconds(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

class HostImpl final : public Host, public Services, public screens::Surface, public hud::Mount
{
  public:
    ~HostImpl() override;
    bool Start(const Config &config, std::string *error);
    bool Prewarm(double budget_ms, bool hidden) override;

    // --- Host ---------------------------------------------------------------------

    bool LoadDocument(const std::string &path) override;
    Rml::ElementDocument *LoadPackDocument(const std::string &path);
    bool OpenDocument(const std::string &path);
    void Resize(int width, int height, float density) override;
    void SetDisplayOptions(float ui_scale, float hud_button_opacity, bool left_handed) override;
    void SetSafeArea(float left, float top, float right, float bottom) override;
    void ApplyHudButtonOpacity();
    void ApplySafeArea();
    void PlaceToasts();
    static constexpr float kToastStatusGap = 6.0f; // dp between a top status and the toasts under it

    bool ScreenHeaderMoved();
    void SetToastClear(float clear);

    using Host::Touch;

    // PlaceTouches places a pointer where it was this long before the frame's time, as Android's
    // own resampling of batched touches does: at the usual sample rates the place then lies
    // between two samples, not past the newest.
    static constexpr double kTouchPlaceLatency = 0.005;
    struct Capture
    {
        Pointer pointer;
        Rml::ObserverPtr<Rml::Element> component;
        bool exclusive = false;
        // A drag (a slider) on a list that scrolls vertically: the first movement past the slop
        // decides — sideways the component takes the pointer, up or down the list scrolls; a
        // tap without movement goes to the component.
        bool deferred = false;
        struct Sample
        {
            Rml::Vector2f at;
            double time = 0.0;
        };
        std::vector<Sample> trail; // the newest samples of the pointer, oldest first
        bool unplaced = false;     // samples came since the pointer was last placed (PlaceTouches)

        void Remember(Rml::Vector2f at, double time)
        {
            if (trail.size() == 8)
                trail.erase(trail.begin());
            trail.push_back({at, time});
        }
        // Where the pointer was at `time`: on the line between the two samples around it; past
        // the newest sample, on along the last two for at most half their distance in time and
        // 8 ms, when they are 2 to 20 ms apart (the rules of Android's touch resampling:
        // InputConsumer). Otherwise the nearest sample.
        Sample At(double time) const
        {
            const Sample &newest = trail.back();
            if (trail.size() < 2)
                return newest;
            if (time >= newest.time)
            {
                const Sample &before = trail[trail.size() - 2];
                const double apart = newest.time - before.time;
                if (apart < 0.002 || apart > 0.020)
                    return newest;
                const double ahead = std::min({time - newest.time, apart * 0.5, 0.008});
                return {newest.at + (newest.at - before.at) * float(ahead / apart), newest.time + ahead};
            }
            for (size_t i = trail.size() - 1; i > 0; --i)
                if (time >= trail[i - 1].time)
                {
                    const Sample &a = trail[i - 1], &b = trail[i];
                    const double apart = b.time - a.time;
                    return {apart > 0.0 ? a.at + (b.at - a.at) * float((time - a.time) / apart) : b.at, time};
                }
            return trail.front();
        }
    };

    void Touch(TouchPhase phase, int pointer, float x, float y, double time) override;
    void Sampled(int pointer, float x, float y, double time);
    void PlaceTouches(double time) override;
    void Apply(TouchPhase phase, int pointer, float x, float y, double time);
    float Slop() const;
    void MoveTo(Capture &capture, int pointer, Rml::Vector2f at, double time, bool sampled);
    bool Prepare(double &delay) override;
    void UpdateContext();
    double Frame() override;
    FrameStats LastFrameStats() const override;
    void Toast(const std::string &text, const std::string &tone) override;
    void Alert(const std::string &text) override;
    void GameMessage(const std::string &text, const std::string &target) override;
    void AlertString(const std::string &key) override;
    void Show(const std::string &source, const std::string &text, const std::string &tone);
    void ShowAlert(const std::string &source, const std::string &text);
    void ShowLogged(const std::string &text, const std::string &tone);
    void Shown(const std::string &source, const std::string &tone, const std::string &text) const override;
    void OverlayToast(const std::string &text, const std::string &tone, double seconds);
    void ToastTouched(ComponentElement &source, const std::string &verb);
    bool SetProps(const std::string &element_id, const std::string &json);
    void ScrollTo(const std::string &element_id);

    // --- game interfaces -----------------------------------------------------------

    void Answer(uint64_t request, bool applied, const std::string &reason, uint64_t revision) override;
    void SetInterface(const InterfaceModel &model) override;
    void SetScriptScreens(const std::vector<ScriptScreen> &screens, ScriptPort *port) override;
    void CloseInterface() override;
    void ExpectInterface() override;
    bool SetState(const std::string &channel, const std::string &json) override;
    void SetState(const HudState &state) override;
    void SetDialogue(const Dialogue &dialogue) override;
    void SubmitText(const std::string &token, const std::string &text, bool accepted) override;
    bool Mirrored() const override;
    bool ScreenMounted() const override;
    bool InterfaceActive() const override;
    bool SetVideo(const Json &state);
    void ShowDocument();
    bool WantsTouch(float x, float y) const override;
    void CancelInput() override;
    bool WantsBack() const override;
    void Back() override;

    // --- hud::Mount ---------------------------------------------------------------

    bool ShowHud(const hud::View &view) override;
    Rml::Element *HudLayer();
    bool MountHudElements(Rml::Element *layer, const hud::View &view);
    bool ShowHudOverlay(Rml::Element *layer, const hud::View &view);
    bool ShowHudSheet(Rml::Element *layer, const hud::View &view);
    bool Measure(const std::string &id, const std::string &selector, hud::Placement &out) const override;
    void HideHud() override;

    // --- screens::Surface ---------------------------------------------------------

    void Show(const std::string &tag, const Json &props) override;
    void Hide() override;
    void Send(const Action &action) override;
    void RequestText(TextRequest request, const std::string &anchor) override;
    void Toast(const std::string &text) override;
    void SetLanguage(const std::string &game_language) override;
    std::string Text(const std::string &key) override;
    void Log(const std::string &line) override;
    void Error(const std::string &line) override;
    void Refuse(const std::string &id) override;
    void CheckTaps(Rml::Element *element);
    void LogTaps();
    void MapTaps(Rml::Element *element, const std::string &where, std::vector<std::string> &lines);
    static void TapText(Rml::Element *element, std::string &text);

    // --- Services -----------------------------------------------------------------

    double Now() const override;
    float Density() const override;
    Json SafeArea() const override;
    double Setting(const std::string &name, double fallback) const override;
    void Emit(ComponentElement &source, const std::string &verb, const std::string &value) override;
    void Tooltip(Rml::Element *anchor, const std::string &text, bool below) override;
    void RequestTicks(ComponentElement &component, bool enable) override;
    void PressStarted(ComponentElement &component) override;
    FrameStats &Counters() override;
    void Forget(ComponentElement &component) override;

  private:
    // Where a pointer going down belongs to the UI; read by the thread that gets touches.
    struct TouchMap
    {
        bool everywhere = false;
        bool back = false;
        std::vector<Rml::Rectanglef> regions; // physical pixels
    };

    // --- HUD mount ---

    Rml::ElementPtr CreateHudElement(const hud::LayoutElement &element);
    bool SetHudProps(const hud::LayoutElement &element, const hud::View &view);
    void RemoveHudElement(Rml::Element *layer, Rml::Element *mounted);
    void CollectRegions(Rml::Element *element, std::vector<Rml::Rectanglef> &out) const;
    void PublishTouchMap(bool laid_out = false);
    std::vector<ActionTrace> TakeActionTraces(bool finish_open) override;

    // --- the flicker check (Config::trace_actions, action_trace.h) ----------------

    std::string ScreenTag() const;
    FrameContent TraceContent(std::string shown);
    void BeginTrace(const std::string &what);
    void Traced(std::optional<ActionTrace> trace);
    void CutOffWhenLeft();
    void ReportSlowFrame();
    void ReportSwitch(bool idle);
    std::string Shown() const;
    bool Between() const;
    std::string RoutingFacts() const;
    bool Holding() const;
    void ReleaseHeld();

    struct Toast_
    {
        Rml::ObserverPtr<Rml::Element> element;
        double until = 0.0;
        std::string text; // the message; the element shows it with its repeat count
        int count = 1;
        double leaving = -1.0; // when it started to animate out (c-notice leaving), -1 = shown
        double held = -1.0;    // the seconds it had left when a finger stopped it (-1 = running)
    };

    double ToastLeaveSeconds() const;
    void Leave(Toast_ &toast, double now);
    void RemoveToast(size_t index);
    void ExpireToasts(double now);

    Config config_;
    System system_;
    std::unique_ptr<PackFiles> files_;
    RenderBackend *backend_ = nullptr;
    std::unique_ptr<FrameRenderer> renderer_;
    std::unique_ptr<CountedDocumentInstancer> document_instancer_; // registered for "body" until Rml::Shutdown
    FrameStats counters_;   // the frame being made, including the work done since the last one
    FrameStats last_frame_; // what the last Frame did
    bool video_ = false;    // a full-screen game video plays (channel "video")
    bool changed_ = true;   // something since the last drawn frame may look different (Prepare)
    bool drawn_ = false;
    uint64_t shown_hash_ = 0; // drawing commands of the frame on the screen
    std::unique_ptr<ComponentRegistry> registry_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    Rml::ElementDocument *overlay_ = nullptr;
    std::string overlay_path_;
    std::string screen_path_;
    std::unordered_map<std::string, Json> strings_; // language code → {key: text}
    std::string language_ = "en";
    // Prewarm: the pack's font uses, made for this language and dp ratio.
    std::vector<FontUse> font_uses_;
    std::vector<IconUse> icon_uses_;
    double prewarm_width_ = 0.0; // dp: the layout width the list was recorded at
    Warmer warmer_;
    std::string prewarm_language_;
    float prewarm_ratio_ = 0.0f;
    bool prewarm_reported_ = false;
    struct
    {
        double ms = 0.0, slowest_ms = 0.0, slowest_icons_ms = 0.0;
        int textures = 0;
        size_t bytes = 0;
    } prewarm_;
    std::set<std::string> unknown_languages_, missing_strings_;
    std::string logged_taps_; // LogTaps: the last block logged
    int taps_block_ = 0;
    std::set<std::string>
        unmapped_taps_; // said once each: screen, element id and verb drawn with no mapping (CheckTaps)
    std::unique_ptr<screens::Router> router_;
    Rml::ElementDocument *screen_root_ = nullptr; // the screen document while a game interface is mounted
    // HUD
    std::unique_ptr<hud::Hud> hud_;
    hud::Layout layout_;
    float hud_toast_clear_ = 0.0f;    // the shown HUD's top status bottom (layout dp), 0 = none
    float screen_toast_clear_ = 0.0f; // the shown screen's header bottom (layout dp), 0 = none
    std::string hud_path_;
    Rml::ElementDocument *hud_document_ = nullptr;
    std::unordered_map<std::string, Json> hud_props_;       // props last given to each mounted element
    std::unordered_map<std::string, std::string> hud_made_; // what each mounted element was created as
    mutable std::mutex touch_mutex_;
    TouchMap touch_map_;
    std::vector<Rml::Rectanglef> hud_regions_; // the HUD's touch regions as last collected (PublishTouchMap)
    bool regions_current_ = false;             // hud_regions_ belong to the HUD shown now
    bool regions_moved_ = true;                // something since that collection can have moved them
    // An interface the game opened (ExpectInterface): its model is still to come, then its
    // first frame.
    enum class Opening
    {
        None,
        AwaitModel,
        AwaitFrame
    };
    Opening opening_ = Opening::None;
    std::string hud_problem_;     // the last undrawable HUD view's error, logged once
    bool game_interface_ = false; // a game interface model is shown (SetInterface until CloseInterface)
    std::string game_screen_;     // the screen of that game interface model
    // A screen switch, timed from the engine action before it to its first frames (ReportSwitch).
    using Clock = std::chrono::steady_clock;
    struct Switch
    {
        bool open = false;
        bool expected = false; // the engine announced it (ExpectInterface) before its model
        uint64_t opening = 0;  // InterfaceModel::opening of the screen being timed
        std::string screen;
        Clock::time_point begun, modelled, first;
        bool drawn = false; // its first frame with the model is drawn
        FrameStats first_frame, settle;
        int frames = 0;
        void Begin(Clock::time_point now, bool by_expect)
        {
            *this = Switch{false, by_expect, opening};
            open = true;
            begun = now;
            modelled = {};
        }
    } switch_;
    Clock::time_point last_tap_{},
        reported_{};                    // the newest touch end (its own time); the end of the last switch reported
    Clock::time_point slow_reported_{}; // the last "UI slow frame" line
    bool unlaid_ = false;               // something covers the game since the last frame (PublishTouchMap)
    bool between_ = false;              // nothing the player can use is shown (Between)
    struct HeldTouch
    {
        TouchPhase phase;
        int pointer;
        float x, y;
        double time;
    };
    std::vector<HeldTouch> held_;
    std::set<int> held_pointers_;
    Json settings_ = Json::object();
    Json safe_ = {{"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}};
    std::array<float, 4> display_safe_{}; // SetSafeArea's insets in the display's dp
    float display_density_ = 1.0f;        // Resize's density; config_.density is it times ui_scale_
    float ui_scale_ = 1.0f;
    float hud_button_opacity_ = 1.0f;
    bool left_handed_ = false;     // «Под левую руку»: the HUD mounted mirrored
    std::string hud_button_class_; // the class ApplyHudButtonOpacity set on the HUD document
    std::unordered_map<int, Capture> captures_;
    std::vector<Rml::ObserverPtr<Rml::Element>> ticking_;
    std::vector<Toast_> toasts_;
    int scroll_pointer_ = -1; // the pointer that drags lists (one at a time)
    TouchScroll scroll_;
    ActionTracer tracer_;             // Config::trace_actions
    std::vector<ActionTrace> traces_; // finished, until TakeActionTraces
    bool game_live_ = false;          // the HUD's presentation was live at the last state (CutOffWhenLeft)
    bool initialised_ = false;
};

} // namespace storm::ui
