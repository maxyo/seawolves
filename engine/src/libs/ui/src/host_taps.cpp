// The taps a drawn screen offers: each must be one its screen maps (CheckTaps), and Config::log_taps logs every
// touch region (LogTaps).
#include "host_impl.h"

namespace storm::ui
{

// Every tap the screen document offers (Behavior::Taps: id as Emit finds it, verb) is one its screen maps
// (Router::Maps): an unmapped one is an error, once per screen, element and verb.
void HostImpl::CheckTaps(Rml::Element *element)
{
    if (auto *component = dynamic_cast<ComponentElement *>(element); component && component->HasBehaviors())
    {
        std::vector<Tap> taps;
        component->Taps(taps);
        std::string id;
        for (Rml::Element *at = component; at && id.empty(); at = at->GetParentNode())
            id = at->GetId();
        for (const Tap &tap : taps)
            if (!router_->Maps(id, tap.verb) &&
                unmapped_taps_.insert(router_->Using() + ' ' + id + ' ' + tap.verb).second)
                Error("screen " + router_->Using() + ": " + component->GetTagName() + " " +
                      (id.empty() ? "(no id)" : id) + " " + tap.verb +
                      " is drawn with no mapping: a tap on it does nothing");
    }
    for (int i = 0; i < element->GetNumChildren(); ++i)
        CheckTaps(element->GetChild(i));
}

// Config::log_taps: every touch region of every drawn component, one line each, as a block logged when it
// differs from the last one. `at` is the middle of what of the region its scrolling ancestors leave visible
// ("-" when nothing), `reach` whether a pointer going down there is taken by this component (not by a
// scrim, a modal or another component over it), `hold` whether it acts only when held (hold-to-confirm), `st` a
// hash of the component's props (what it shows changed).
void HostImpl::LogTaps()
{
    std::vector<std::string> lines;
    for (int d = 0; d < context_->GetNumDocuments(); ++d)
        if (Rml::ElementDocument *document = context_->GetDocument(d); document && document->IsVisible())
            MapTaps(document,
                    document == hud_document_ ? "hud"
                    : document == document_   ? (router_->Using().empty() ? "document" : router_->Using())
                                              : "layer",
                    lines);
    std::string joined;
    for (const std::string &line : lines)
        joined += line + '\n';
    if (joined == logged_taps_)
        return;
    logged_taps_ = joined;
    ++taps_block_;
    Log("taps begin " + std::to_string(taps_block_) + " " + std::to_string(lines.size()) + " " +
        std::to_string(config_.width) + "x" + std::to_string(config_.height));
    for (const std::string &line : lines)
        Log("taps " + std::to_string(taps_block_) + " " + line);
    Log("taps end " + std::to_string(taps_block_));
}

void HostImpl::MapTaps(Rml::Element *element, const std::string &where, std::vector<std::string> &lines)
{
    if (!element->IsVisible(true))
        return;
    if (auto *component = dynamic_cast<ComponentElement *>(element); component && component->HasBehaviors())
    {
        std::string id;
        for (Rml::Element *at = component; at && id.empty(); at = at->GetParentNode())
            id = at->GetId();
        // The behaviours' touch regions (enabled or not) and every part a tap is offered on (Behavior::Taps: a
        // press row with action parts is a tap besides them too). A region with no offered tap is disabled.
        std::vector<Rml::Element *> parts;
        component->TouchRegions(parts);
        std::vector<Tap> taps;
        component->Taps(taps);
        for (const Tap &tap : taps)
            if (std::find(parts.begin(), parts.end(), tap.part) == parts.end())
                parts.insert(tap.part == component ? parts.begin() : parts.end(), tap.part);
        const auto untouchable = [](Rml::Element *e) {
            return e->GetComputedValues().pointer_events() == Rml::Style::PointerEvents::None;
        };
        for (Rml::Element *part : parts)
        {
            const Rml::Vector2f size = part->GetBox().GetSize(Rml::BoxArea::Border);
            Rml::Element *owner = part;
            while (owner && !dynamic_cast<ComponentElement *>(owner))
                owner = owner->GetParentNode();
            if (owner != component || size.x <= 0.0f || size.y <= 0.0f || !part->IsVisible(true) || untouchable(part))
                continue; // a nested component's own part (listed with it); pointer-events: none: it only shows
                          // something
            const Rml::Rectanglef box =
                Rml::Rectanglef::FromPositionSize(part->GetAbsoluteOffset(Rml::BoxArea::Border), size);
            Rml::Rectanglef shown = box;
            for (Rml::Element *clip = part->GetParentNode(); clip; clip = clip->GetParentNode())
                if (clip->GetComputedValues().overflow_x() != Rml::Style::Overflow::Visible ||
                    clip->GetComputedValues().overflow_y() != Rml::Style::Overflow::Visible)
                    shown = shown.Intersect(Rml::Rectanglef::FromPositionSize(
                        clip->GetAbsoluteOffset(Rml::BoxArea::Padding), clip->GetBox().GetSize(Rml::BoxArea::Padding)));
            shown = shown.Intersect(Rml::Rectanglef::FromSize({float(config_.width), float(config_.height)}));
            std::string at = "-";
            bool reach = false;
            if (shown.Width() >= 1.0f && shown.Height() >= 1.0f)
            {
                const Rml::Vector2f middle = shown.Center();
                at = std::to_string(int(middle.x)) + "," + std::to_string(int(middle.y));
                Pointer pointer;
                pointer.position = pointer.start = middle;
                reach = InteractiveComponentAt(context_->GetElementAtPoint(middle), pointer) == component;
            }
            const auto offered =
                std::find_if(taps.begin(), taps.end(), [part](const Tap &tap) { return tap.part == part; });
            const std::string verb = offered != taps.end() ? offered->verb
                                     : part == component   ? "click"
                                                           : part->GetAttribute<Rml::String>("data-action", "click");
            // Behaviours that report no taps (a stick, a lever, a swipe) take their regions as they are.
            const bool disabled = taps.empty() ? component->Flag("disabled") : offered == taps.end();
            const bool hold = offered != taps.end() && offered->hold;
            std::string text;
            TapText(part, text);
            char line[200];
            std::snprintf(line, sizeof line, " %s %s %s box=%d,%d,%d,%d at=%s reach=%d dis=%d hold=%d st=%08x \"",
                          component->GetTagName().c_str(), id.empty() ? "-" : id.c_str(), verb.c_str(), int(box.Left()),
                          int(box.Top()), int(box.Width()), int(box.Height()), at.c_str(), reach ? 1 : 0,
                          disabled ? 1 : 0, hold ? 1 : 0,
                          unsigned(std::hash<std::string>{}(component->Props().dump()) & 0xffffffffu));
            lines.push_back(where + line + std::string(Utf8Prefix(text, 60)) + "\"");
        }
    }
    for (int i = 0; i < element->GetNumChildren(); ++i)
        MapTaps(element->GetChild(i), where, lines);
}

void HostImpl::TapText(Rml::Element *element, std::string &text)
{
    if (text.size() >= 60 || !element->IsVisible(true))
        return;
    if (auto *node = rmlui_dynamic_cast<Rml::ElementText *>(element))
    {
        for (char c : node->GetText())
            if (c != '"' && c != '\n' && c != '\r' && c != '\t' && !(c == ' ' && (text.empty() || text.back() == ' ')))
                text += c;
        if (!text.empty() && text.back() != ' ')
            text += ' ';
    }
    for (int i = 0; i < element->GetNumChildren(); ++i)
        TapText(element->GetChild(i), text);
}

} // namespace storm::ui
