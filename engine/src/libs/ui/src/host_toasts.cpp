// Toasts and alerts: the overlay's toast stack, its place clear of a top status and a screen's header, and the log
// line of every text the host shows.
#include "host_impl.h"

namespace storm::ui
{

// The host's toast stack: at the top edge (8 dp: its offset in base.rcss), clear
// of a top status of the HUD that is shown (LayoutElement::toast_clear: the cabin captain,
// the boarding bars), whose layout dp grow by the player's UI scale, and of a shown screen's
// header (its tabs stay visible and pressable under a toast).
void HostImpl::PlaceToasts()
{
    const auto [left, top, right, bottom] = display_safe_;
    const float scale = ui_scale_ > 0.0f ? ui_scale_ : 1.0f;
    if (overlay_)
        if (Rml::Element *stack = overlay_->GetElementById("toasts"))
        {
            stack->SetProperty(Rml::PropertyId::Left, Rml::Property(left, Rml::Unit::DP));
            stack->SetProperty(Rml::PropertyId::Right, Rml::Property(right, Rml::Unit::DP));
            const float clear = hud_toast_clear_ > 0.0f ? (hud_toast_clear_ + kToastStatusGap) * scale : 0.0f;
            const float header = screen_toast_clear_ > 0.0f ? screen_toast_clear_ + kToastStatusGap : 0.0f;
            stack->SetProperty(Rml::PropertyId::Top,
                               Rml::Property(std::max(top + std::max(8.0f, clear), header), Rml::Unit::DP));
        }
}

// The bottom of the shown screen's header in layout dp (0: none shown); true when it moved.
bool HostImpl::ScreenHeaderMoved()
{
    float bottom = 0.0f;
    if (screen_root_ && document_ && document_->IsVisible())
        if (Rml::Element *header = document_->QuerySelector("c-header"); header && header->IsVisible(true))
            bottom =
                (header->GetAbsoluteOffset(Rml::BoxArea::Border).y + header->GetBox().GetSize(Rml::BoxArea::Border).y) /
                config_.density;
    if (std::fabs(bottom - screen_toast_clear_) < 0.5f)
        return false;
    screen_toast_clear_ = bottom;
    PlaceToasts();
    return true;
}

void HostImpl::SetToastClear(float clear)
{
    if (clear == hud_toast_clear_)
        return;
    hud_toast_clear_ = clear;
    changed_ = true;
    PlaceToasts();
}

void HostImpl::Toast(const std::string &text, const std::string &tone)
{
    Show("toast", text, tone);
}

// On the overlay whatever is shown: over the game, over a screen, and through the change
// from one to the other (a failed load: the HUD's log would vanish with the menu that comes back).
void HostImpl::Alert(const std::string &text)
{
    ShowAlert("alert", text);
}

void HostImpl::GameMessage(const std::string &text, const std::string &target)
{
    Shown(target.empty() ? "game log" : "game log, opens " + target, "info", text);
    if (hud_->Notice(text, "info", target)) // over the game: the HUD's log, a tap opens the target
        return;
    OverlayToast(text, "info", hud::ToastSeconds(text)); // over a screen: only shown
}

void HostImpl::AlertString(const std::string &key)
{
    ShowAlert("alert " + key, Text(key));
}

// Every text the host puts on the screen goes through Show, ShowAlert or GameMessage, which log it first
// (Shown): what the player saw is in the log, with where it came from. `source` names the caller.
void HostImpl::Show(const std::string &source, const std::string &text, const std::string &tone)
{
    Shown(source, tone, text);
    ShowLogged(text, tone);
}

void HostImpl::ShowAlert(const std::string &source, const std::string &text)
{
    Shown(source, "danger", text);
    OverlayToast(text, "danger", Setting("alertMs", 6000.0) / 1000.0);
}

// A toast whose text is in the log already.
void HostImpl::ShowLogged(const std::string &text, const std::string &tone)
{
    if (hud_->Notice(text, tone)) // over the game, events go to the HUD's top-centre log
        return;
    OverlayToast(text, tone, hud::ToastSeconds(text));
}

void HostImpl::Shown(const std::string &source, const std::string &tone, const std::string &text) const
{
    if (text.empty())
        return;
    const auto type = tone == "danger" ? Rml::Log::LT_ERROR : tone == "warn" ? Rml::Log::LT_WARNING : Rml::Log::LT_INFO;
    Rml::Log::Message(type, "shown %s (%s): %s", tone.c_str(), source.c_str(), text.c_str());
}

// `seconds`: how long it stays (hud::ToastSeconds for a toast, longer for an alert); a repeat
// starts it again, a finger on it stops it (behaviour toast: hold / release).
void HostImpl::OverlayToast(const std::string &text, const std::string &tone, double seconds)
{
    if (!overlay_ || text.empty())
        return;
    // One place for every toast: the top edge (the HUD's event lines are there too).
    Rml::Element *stack = overlay_->GetElementById("toasts");
    if (!stack)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "overlay.rml has no #toasts stack: toast dropped: %s", text.c_str());
        return;
    }
    const double until = system_.GetElapsedTime() + seconds;
    for (Toast_ &shown : toasts_)
        if (shown.text == text && shown.element && shown.leaving < 0.0)
        {
            shown.element->SetAttribute("text", hud::RepeatedText(text, ++shown.count));
            if (shown.held >= 0.0)
                shown.held = seconds;
            else
                shown.until = until;
            changed_ = true;
            return;
        }
    // At most three shown: the oldest shown one starts to leave.
    if (std::count_if(toasts_.begin(), toasts_.end(), [](const Toast_ &toast) { return toast.leaving < 0.0; }) >= 3)
        for (Toast_ &shown : toasts_)
            if (shown.leaving < 0.0)
            {
                Leave(shown, system_.GetElapsedTime());
                break;
            }
    Rml::ElementPtr notice = overlay_->CreateElement("c-notice");
    notice->SetAttribute("variant", "toast");
    notice->SetAttribute("place", "none");
    notice->SetAttribute("tone", tone.empty() ? "info" : tone);
    notice->SetAttribute("text", text);
    notice->SetAttribute("compact", "");
    changed_ = true;
    Toast_ toast;
    toast.element = stack->AppendChild(std::move(notice))->GetObserverPtr();
    toast.until = until;
    toast.text = text;
    toasts_.push_back(std::move(toast));
}

// A finger on an overlay toast (behaviour toast): its timer stops while held.
void HostImpl::ToastTouched(ComponentElement &source, const std::string &verb)
{
    const double now = system_.GetElapsedTime();
    for (Toast_ &toast : toasts_)
        if (toast.element.get() == &source && toast.leaving < 0.0)
        {
            if (verb == "hold" && toast.held < 0.0)
                toast.held = std::max(0.0, toast.until - now);
            else if (verb == "release" && toast.held >= 0.0)
            {
                toast.until = now + toast.held;
                toast.held = -1.0;
                changed_ = true;
            }
        }
}

void HostImpl::Toast(const std::string &text)
{
    Show("screen " + router_->Using(), text, "info");
}

// How long a toast animates out (pack time.toastLeave); the stack closes up meanwhile.
double HostImpl::ToastLeaveSeconds() const
{
    return Setting("toastLeave", 0.22);
}

void HostImpl::Leave(Toast_ &toast, double now)
{
    changed_ = true;
    toast.leaving = now;
    if (Rml::Element *element = toast.element.get())
        element->SetAttribute("leaving", "1");
}

void HostImpl::RemoveToast(size_t index)
{
    changed_ = true;
    if (Rml::Element *element = toasts_[index].element.get())
        if (Rml::Element *parent = element->GetParentNode())
            parent->RemoveChild(element);
    toasts_.erase(toasts_.begin() + static_cast<long>(index));
}

// An expired toast animates out first; it goes once that has run.
void HostImpl::ExpireToasts(double now)
{
    for (size_t i = toasts_.size(); i-- > 0;)
        if (toasts_[i].leaving < 0.0 && toasts_[i].held < 0.0 && now >= toasts_[i].until)
            Leave(toasts_[i], now);
        else if (toasts_[i].leaving >= 0.0 && now >= toasts_[i].leaving + ToastLeaveSeconds())
            RemoveToast(i);
}

} // namespace storm::ui
