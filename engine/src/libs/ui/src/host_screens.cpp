// What the game hands the host (interface models, state channels, the dialogue, a full-screen video, the back
// key) and the screens' surface (Show, Hide, Send, text requests, refusals).
#include "host_impl.h"

namespace storm::ui
{

void HostImpl::Answer(uint64_t request, bool applied, const std::string &reason)
{
    const auto started = std::chrono::steady_clock::now();
    router_->Answer(request, applied, reason);
    counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
}

void HostImpl::SetInterface(const InterfaceModel &model)
{
    const auto started = std::chrono::steady_clock::now();
    if (model.opening != switch_.opening)
    {
        if (!switch_.open)
            switch_.Begin(started, false);
        switch_.opening = model.opening;
        switch_.screen = model.screen;
        switch_.modelled = started;
    }
    game_interface_ = true;
    game_screen_ = model.screen;
    router_->SetInterface(model);
    counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
    if (opening_ == Opening::AwaitModel)
        opening_ = Opening::AwaitFrame;
    PublishTouchMap();
}

void HostImpl::CloseInterface()
{
    switch_.open = false;
    game_interface_ = false;
    router_->CloseGameInterface(game_screen_);
    game_screen_.clear();
    if (opening_ != Opening::None)
    {
        opening_ = Opening::None;
        if (!held_.empty())
            Log(std::to_string(held_.size()) + " touch events dropped: the interface closed before it was shown; " +
                RoutingFacts());
        held_.clear();
        held_pointers_.clear();
    }
    PublishTouchMap();
}

void HostImpl::ExpectInterface()
{
    if (!switch_.open)
        switch_.Begin(std::chrono::steady_clock::now(), true);
    changed_ = true;
    opening_ = Opening::AwaitModel;
    PublishTouchMap();
}

bool HostImpl::SetState(const std::string &channel, const std::string &json)
{
    const Json state = Json::parse(json, nullptr, false);
    if (state.is_discarded())
    {
        Error("state channel " + channel + ": malformed JSON");
        return false;
    }
    if (channel == "video")
        return SetVideo(state);
    const auto started = std::chrono::steady_clock::now();
    try
    {
        router_->SetState(channel, state);
        counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
    }
    catch (const Json::exception &error)
    {
        Error("state channel " + channel + ": " + error.what());
        return false;
    }
    CutOffWhenLeft();
    PublishTouchMap(); // a sheet the state opened owns the touches from now
    return true;
}

void HostImpl::SetState(const HudState &state)
{
    if (!config_.game_hud)
    {
        Error(std::string("state channel ") + HudChannel(state) + ": this host has no HUD");
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    hud_->Set(state);
    counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
    CutOffWhenLeft();
    PublishTouchMap(); // a sheet the state opened owns the touches from now
}

// The game stopped being live (the app left the screen) with an action open: nothing drawn from now on answers it.
void HostImpl::CutOffWhenLeft()
{
    const bool live = hud_ && hud_->GameLive();
    if (game_live_ && !live)
        Traced(tracer_.CutOff());
    game_live_ = live;
}

void HostImpl::SetDialogue(const Dialogue &dialogue)
{
    const auto started = std::chrono::steady_clock::now();
    router_->SetDialogue(dialogue);
    counters_.apply_ms += Milliseconds(started, std::chrono::steady_clock::now());
    PublishTouchMap(); // the dialogue owns the touches from now
}

void HostImpl::SubmitText(const std::string &token, const std::string &text, bool accepted)
{
    changed_ = true;
    router_->SubmitText(token, text, accepted);
}

bool HostImpl::Mirrored() const
{
    return left_handed_;
}

bool HostImpl::ScreenMounted() const
{
    return router_->Active() || document_ != nullptr;
}

bool HostImpl::InterfaceActive() const
{
    // hud::Mount too: a plain document covers the game as well; a full-screen video covers both
    return !video_ && (router_->Active() || document_ != nullptr);
}

// Channel "video" ({"fullscreen": true} or null, engine aviplayer.cpp): while a game video
// plays over the whole screen the interface screen is hidden and takes no touch, so the
// video is seen and a tap reaches the game (the player's tap-to-skip). The screen itself
// stays mounted: the script that started the video may leave its interface open (the main
// menu's «Создатели» exits without a release) and it returns as it was.
bool HostImpl::SetVideo(const Json &state)
{
    if (!state.is_null() && !(state.is_object() && state.contains("fullscreen") && state["fullscreen"].is_boolean()))
    {
        Error("state channel video: expected {fullscreen: bool} or null, got " + state.dump());
        return false;
    }
    const bool video = state.is_object() && state["fullscreen"].get<bool>();
    if (video == video_)
        return true;
    video_ = video;
    if (video_)
        CancelInput();
    ShowDocument();
    PublishTouchMap();
    return true;
}

void HostImpl::ShowDocument()
{
    changed_ = true;
    if (document_)
        video_ ? document_->Hide() : document_->Show();
}

void HostImpl::Back()
{
    changed_ = true;
    if (config_.trace_actions)
    {
        BeginTrace("back");
        tracer_.Named("back");
    }
    bool hud_sheet = false;
    {
        const std::lock_guard<std::mutex> lock(touch_mutex_);
        hud_sheet = touch_map_.back;
    }
    if (router_->WantsBack()) // a screen's own modal (the dialogue's answer editor)
        router_->Back();
    else if (hud_sheet || !router_->Active())
        hud_->Back();
    else
        router_->Back();
}

void HostImpl::Show(const std::string &tag, const Json &props)
{
    changed_ = true;
    if (!screen_root_ && (screen_path_.empty() || !OpenDocument(screen_path_)))
    {
        Error("UI pack has no screen document");
        return;
    }
    screen_root_ = document_;
    Rml::Element *mounted = document_->GetElementById("screen");
    if (!mounted || mounted->GetTagName() != tag)
    {
        if (mounted)
            document_->RemoveChild(mounted);
        Rml::ElementPtr created = document_->CreateElement(tag);
        created->SetId("screen");
        mounted = document_->AppendChild(std::move(created));
    }
    if (auto *component = dynamic_cast<ComponentElement *>(mounted))
        component->SetProps(props);
    else
        Error("UI pack has no screen " + tag);
    if (video_)
        ShowDocument();
    PublishTouchMap();
}

void HostImpl::Hide()
{
    changed_ = true;
    captures_.clear();
    if (screen_root_ && document_)
    {
        document_->Close();
        document_ = nullptr;
    }
    screen_root_ = nullptr;
    PublishTouchMap();
}

void HostImpl::Send(const Action &action)
{
    if (config_.on_action)
        config_.on_action(action);
}

void HostImpl::RequestText(TextRequest request, const std::string &anchor)
{
    if (Rml::Element *element = document_ ? document_->GetElementById(anchor) : nullptr)
    {
        // A field marks the box the text sits in (c-field: data-action="focus"); the
        // editor goes exactly over it, not over the label and error lines.
        if (Rml::Element *box = element->QuerySelector("[data-action=focus]"))
            element = box;
        const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
        request.x = origin.x;
        request.y = origin.y;
        request.width = size.x;
        request.height = size.y;
    }
    if (config_.on_text_request)
        config_.on_text_request(request);
    else
        router_->SubmitText(request.token, {}, false);
}

// A modal of the screen refused a tap beside it or the back key (Bindings::Modal without a
// cancel): its component shows it (C-Dialog shakes).
void HostImpl::Refuse(const std::string &id)
{
    auto *component = document_ ? dynamic_cast<ComponentElement *>(document_->GetElementById(id)) : nullptr;
    if (!component || !component->Refuse())
        Error("refused modal " + id + ": no component with that id can show it");
    changed_ = true;
}

} // namespace storm::ui
