// Touches: capture of a pointer by the component under it, resampling, the map of where a touch is the UI's
// (read by the thread that gets touches) and the touches held between scenes.
#include "host_impl.h"

namespace storm::ui
{

void HostImpl::Touch(TouchPhase phase, int pointer, float x, float y, double time)
{
    changed_ = true;
    if (config_.trace_actions && phase == TouchPhase::Down)
        Traced(tracer_.Finish(true)); // a new gesture: what the last one did is over
    else if (config_.trace_actions && phase == TouchPhase::Up)
        BeginTrace("tap " + std::to_string(int(x)) + "," + std::to_string(int(y)));
    // A pointer that went down on what is not laid out yet (an interface whose model is
    // still to come, a sheet opened since the last frame) waits for that layout, with the
    // rest of its gesture (ReleaseHeld).
    if (phase == TouchPhase::Down ? Holding() : held_pointers_.count(pointer) > 0)
    {
        if (phase == TouchPhase::Down)
        {
            held_pointers_.insert(pointer);
            Log("touch held for " +
                std::string(opening_ != Opening::None ? "an interface being opened"
                            : between_                ? "what comes after the transition"
                                                      : "the next layout") +
                ": " + RoutingFacts());
        }
        // Applied later, all at once: a sample is then a move like any other.
        held_.push_back({phase == TouchPhase::Sample ? TouchPhase::Move : phase, pointer, x, y, time});
        return;
    }
    if (phase == TouchPhase::Sample)
        Sampled(pointer, x, y, time);
    else
        Apply(phase, pointer, x, y, time);
}

// A move sample whose place comes with PlaceTouches.
void HostImpl::Sampled(int pointer, float x, float y, double time)
{
    const auto found = captures_.find(pointer);
    if (found == captures_.end())
        return;
    Capture &capture = found->second;
    capture.Remember({x, y}, time);
    capture.unplaced = true;
    if (!capture.exclusive && pointer == scroll_pointer_)
        scroll_.Sample({x, y}, time);
}

void HostImpl::PlaceTouches(double time)
{
    // What a move does may end any capture (a screen that closes): by pointer, looked up again.
    std::vector<int> pointers;
    for (const auto &entry : captures_)
        if (entry.second.unplaced)
            pointers.push_back(entry.first);
    for (const int pointer : pointers)
    {
        const auto found = captures_.find(pointer);
        if (found == captures_.end() || !found->second.unplaced)
            continue;
        found->second.unplaced = false;
        const Capture::Sample place = found->second.At(time - kTouchPlaceLatency);
        MoveTo(found->second, pointer, place.at, place.time, false);
    }
}

void HostImpl::Apply(TouchPhase phase, int pointer, float x, float y, double time)
{
    const Rml::Vector2f at = {x, y};
    if (phase == TouchPhase::Down)
    {
        Capture capture;
        capture.pointer.id = pointer;
        capture.pointer.position = capture.pointer.start = at;
        capture.pointer.time = time;
        capture.Remember(at, time);
        capture.pointer.target = context_->GetElementAtPoint(at);
        // The HUD takes input only while the player can act (Presentation::live).
        if (capture.pointer.target && capture.pointer.target->GetOwnerDocument() == hud_document_ &&
            !hud_->Interactive())
            return;
        ComponentElement *component = InteractiveComponentAt(capture.pointer.target, capture.pointer);
        // Claimed for something that is gone when it arrives (a load ended in gameplay in
        // between): nothing takes it, said once per gesture.
        if (!component && !InterfaceActive() && !hud_->Interactive())
            Log("touch reached nothing of the UI: " + RoutingFacts());
        bool deferred = false;
        if (component && component->PointerExclusive(capture.pointer) && scroll_pointer_ < 0)
        {
            scroll_.Down(capture.pointer.target, at, time, config_.density, Slop());
            deferred = scroll_.ScrollsVertically() && !scroll_.Caught();
            if (deferred || scroll_.Caught())
                scroll_pointer_ = pointer;
            else
                scroll_.Up(time, true); // no list to scroll: the drag is the component's
            if (deferred)
            {
                capture.deferred = true;
                capture.component = component->GetObserverPtr();
            }
            else if (scroll_.Caught())
                component = nullptr; // a finger that stops a list in motion drags it
        }
        if (!deferred && component && component->PointerExclusive(capture.pointer))
        {
            capture.exclusive = true;
            capture.component = component->GetObserverPtr();
            component->PointerDown(capture.pointer);
        }
        else if (!deferred)
        {
            if (scroll_pointer_ < 0)
            {
                scroll_pointer_ = pointer;
                scroll_.Down(capture.pointer.target, at, time, config_.density, Slop());
            }
            // A finger that stops a list in motion drags it: it presses nothing.
            const bool caught = pointer == scroll_pointer_ && scroll_.Caught();
            if (component && !caught && component->PointerDown(capture.pointer))
                capture.component = component->GetObserverPtr();
        }
        captures_[pointer] = std::move(capture);
        return;
    }

    if (phase == TouchPhase::Up) // the end of a tap: what a screen switch answers (ReportSwitch)
        last_tap_ = Clock::now() - std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double>(std::max(0.0, Now() - time)));
    auto found = captures_.find(pointer);
    if (found == captures_.end())
        return;
    if (phase == TouchPhase::Move)
    {
        found->second.Remember(at, time);
        found->second.unplaced = false;
        MoveTo(found->second, pointer, at, time, true);
        return;
    }
    // The gesture ends where its newest sample was, not where it was last placed.
    if (found->second.unplaced)
    {
        found->second.unplaced = false;
        const Capture::Sample newest = found->second.trail.back();
        MoveTo(found->second, pointer, newest.at, newest.time, false);
        found = captures_.find(pointer);
        if (found == captures_.end())
            return;
    }
    Capture &capture = found->second;
    capture.pointer.position = at;
    capture.pointer.time = time;
    auto *component = static_cast<ComponentElement *>(capture.component.get());
    if (capture.deferred && component && phase != TouchPhase::Cancel)
    {
        // A tap on the slider: it goes there as it would have outside a list.
        capture.deferred = false;
        capture.exclusive = true;
        scroll_.Up(time, true);
        scroll_pointer_ = -1;
        Pointer down = capture.pointer;
        down.position = down.start;
        component->PointerDown(down);
    }
    if (component && !capture.deferred)
        component->PointerUp(capture.pointer, phase == TouchPhase::Cancel);
    if (!capture.exclusive && pointer == scroll_pointer_)
    {
        scroll_.Up(time, phase == TouchPhase::Cancel);
        scroll_pointer_ = -1;
    }
    captures_.erase(pointer);
}

// A tap that travels this far is a drag (a scroll), no longer a press.
float HostImpl::Slop() const
{
    return 10.0f * config_.density;
}

// The pointer is at `at`: what it drags follows. `sampled`: a sample of the touch screen
// (it counts for a list's release velocity), not a place between or past samples.
void HostImpl::MoveTo(Capture &capture, int pointer, Rml::Vector2f at, double time, bool sampled)
{
    capture.pointer.position = at;
    capture.pointer.time = time;
    auto *component = static_cast<ComponentElement *>(capture.component.get());
    if (capture.deferred && (at - capture.pointer.start).SquaredMagnitude() > Slop() * Slop())
    {
        capture.deferred = false;
        const Rml::Vector2f moved = at - capture.pointer.start;
        if (component && std::abs(moved.x) >= std::abs(moved.y))
        {
            // Sideways: the component's drag, from where the finger went down.
            scroll_.Up(time, true);
            scroll_pointer_ = -1;
            capture.exclusive = true;
            Pointer down = capture.pointer;
            down.position = down.start;
            component->PointerDown(down);
        }
        else
        {
            capture.component = {};
            component = nullptr;
        }
    }
    else if (capture.deferred)
    {
        if (pointer == scroll_pointer_)
            scroll_.Move(at, time, sampled);
        return;
    }
    if (capture.exclusive)
    {
        if (component)
            component->PointerMove(capture.pointer);
        return;
    }
    const bool scrolling = pointer == scroll_pointer_ && scroll_.Move(at, time, sampled);
    if (!component)
        return;
    if (scrolling || (at - capture.pointer.start).SquaredMagnitude() > Slop() * Slop())
    {
        component->PointerUp(capture.pointer, true); // the press is withdrawn
        capture.component = {};
    }
    else
        component->PointerMove(capture.pointer);
}

bool HostImpl::WantsTouch(float x, float y) const
{
    const std::lock_guard<std::mutex> lock(touch_mutex_);
    if (touch_map_.everywhere)
        return true;
    for (const Rml::Rectanglef &region : touch_map_.regions)
        if (region.Contains({x, y}))
            return true;
    return false;
}

void HostImpl::CancelInput()
{
    changed_ = true;
    if (!held_.empty())
        Log(std::to_string(held_.size()) +
            " touch events dropped: the platform cancelled the input while they waited; " + RoutingFacts());
    held_.clear();
    held_pointers_.clear();
    std::vector<std::pair<int, Rml::Vector2f>> held;
    for (const auto &capture : captures_)
        held.emplace_back(capture.first, capture.second.pointer.position);
    for (const auto &pointer : held)
        Touch(TouchPhase::Cancel, pointer.first, pointer.second.x, pointer.second.y);
    hud_->Released();
}

bool HostImpl::WantsBack() const
{
    const std::lock_guard<std::mutex> lock(touch_mutex_);
    return touch_map_.back;
}

void HostImpl::CollectRegions(Rml::Element *element, std::vector<Rml::Rectanglef> &out) const
{
    if (!element->IsVisible(true))
        return;
    if (auto *component = dynamic_cast<ComponentElement *>(element))
    {
        std::vector<Rml::Element *> parts;
        component->TouchRegions(parts);
        for (Rml::Element *part : parts)
        {
            const Rml::Vector2f size = part->GetBox().GetSize(Rml::BoxArea::Border);
            if (size.x > 0.0f && size.y > 0.0f && part->IsVisible(true))
                out.push_back(Rml::Rectanglef::FromPositionSize(part->GetAbsoluteOffset(Rml::BoxArea::Border), size));
        }
    }
    for (int i = 0; i < element->GetNumChildren(); ++i)
        CollectRegions(element->GetChild(i), out);
}

// Called after everything that changes what a touch would hit, so WantsTouch never waits for
// this thread. Outside a frame (`laid_out` false) pointers that go down on what is not laid out
// yet are held until the next frame. The HUD's regions are walked again only when they can
// have moved (regions_moved_); Config::verify_unchanged checks a reuse against a fresh walk.
void HostImpl::PublishTouchMap(bool laid_out)
{
    TouchMap map;
    const bool hud = hud_->Interactive();
    const bool modal =
        !video_ && ((hud && hud_->Shown().modal) || router_->WantsBack()); // a modal HUD sheet or a screen's own modal
    map.back = modal || (!video_ && hud &&
                         hud_->Shown().back); // or a scene that takes the key without one (the raised spyglass)
    const bool was_between = between_;
    between_ = Between();
    // a document or an interface screen (also one the game opened whose model is still to
    // come), a modal HUD sheet, or nothing the player can use (Between)
    map.everywhere = !video_ && (document_ != nullptr || modal || opening_ != Opening::None || between_);
    if (!laid_out)
        regions_moved_ = true; // a host call: the next frame lays out what it changed
    if (hud && !map.everywhere && hud_document_)
    {
        if (!regions_current_ || (laid_out && regions_moved_))
        {
            hud_regions_.clear();
            CollectRegions(hud_document_, hud_regions_);
            regions_current_ = true;
            if (laid_out)
                regions_moved_ = false;
        }
        else if (laid_out && config_.verify_unchanged)
        {
            std::vector<Rml::Rectanglef> fresh;
            CollectRegions(hud_document_, fresh);
            if (fresh != hud_regions_)
                Error("UI touch map out of date: HUD touch regions moved without a layout");
        }
        map.regions = hud_regions_;
    }
    else
        regions_current_ = false; // the HUD takes no part: collected afresh when it does again
    const std::lock_guard<std::mutex> lock(touch_mutex_);
    // What comes after a transition is not laid out before the next frame either.
    if (!laid_out && map.everywhere && (!touch_map_.everywhere || (was_between && !between_)))
        unlaid_ = true;
    touch_map_ = std::move(map);
}

// The game is between scenes and shows nothing the player can use (no interface, video,
// telescope or gameplay mode, and Gameplay::blocked says it is starting, loading or launching
// an interface): the UI owns the touches then and holds them for the next interface; gameplay
// drops them (ReleaseHeld). Every other reason leaves the touches to the game.
bool HostImpl::Between() const
{
    if (video_ || opening_ != Opening::None || InterfaceActive())
        return false;
    const hud::States &states = hud_->Current();
    // Engine states only (gameplay, video, spyglass): they arrive in the engine's own frame.
    // The presentation's cinematic flag is polled (every 200 ms, main.cpp) and stays stale
    // through a load that follows a video (the title).
    if ((states.spyglass.known && states.spyglass.on) || hud_->InGameplay())
        return false;
    const std::string &why = states.gameplay.blocked;
    return why == "starting" || why == "loading" || why == "transition" || why == "no-scene" || why == "interface";
}

// What decided a held or dropped touch, for the log (the player's shared log shows a state
// that was missing).
std::string HostImpl::RoutingFacts() const
{
    const hud::States &states = hud_->Current();
    const hud::Presentation &shown = states.presentation;
    return "gameplay " + states.gameplay.mode +
           (states.gameplay.blocked.empty() ? "" : " (" + states.gameplay.blocked + ")") + ", presentation " +
           (shown.known ? "screen '" + shown.screen + "' loading " + std::to_string(shown.loading) + " cinematic " +
                              std::to_string(shown.cinematic) + " dialogue " + std::to_string(shown.dialogue) +
                              " live " + std::to_string(shown.live)
                        : std::string("unknown")) +
           ", interface " + std::to_string(InterfaceActive()) + ", video " + std::to_string(video_);
}

bool HostImpl::Holding() const
{
    return opening_ != Opening::None || unlaid_ || between_;
}

// After a frame: the touches held for what it laid out are applied in order. Touches held
// through a transition that ends in gameplay are dropped (the player aimed at a screen), but a
// finger still down starts its gesture now: its pointer stays the UI's until it lifts.
void HostImpl::ReleaseHeld()
{
    if (opening_ == Opening::AwaitFrame)
        opening_ = Opening::None;
    unlaid_ = false;
    if (opening_ != Opening::None || between_ || held_.empty())
        return;
    const std::vector<HeldTouch> held = std::move(held_);
    held_.clear();
    held_pointers_.clear();
    if (!InterfaceActive())
    {
        std::map<int, const HeldTouch *> down; // the last event of each finger that did not lift
        for (const HeldTouch &touch : held)
            if (touch.phase == TouchPhase::Up || touch.phase == TouchPhase::Cancel)
                down.erase(touch.pointer);
            else
                down[touch.pointer] = &touch;
        Log(std::to_string(held.size()) + " touch events dropped: gameplay came next" +
            (down.empty() ? std::string()
                          : "; " + std::to_string(down.size()) + " finger(s) still down start their gesture now") +
            "; " + RoutingFacts());
        for (const auto &[pointer, touch] : down)
            Apply(TouchPhase::Down, pointer, touch->x, touch->y, touch->time);
        return;
    }
    for (const HeldTouch &touch : held)
        Apply(touch.phase, touch.pointer, touch.x, touch.y, touch.time);
}

} // namespace storm::ui
