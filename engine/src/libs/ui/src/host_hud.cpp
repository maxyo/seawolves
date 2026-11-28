// The HUD's mount (hud::Mount): the scene's elements patched in place on the HUD document's layer, its overlay and
// sheet, and the measures the scenes ask for.
#include "host_impl.h"

#include <algorithm>

namespace storm::ui
{

bool HostImpl::ShowHud(const hud::View &view)
{
    const std::string problem = hud::CheckView(layout_, view);
    if (!problem.empty())
    {
        // The scene rebuilds the same view each frame: the error once, not a toast ×N.
        if (problem != hud_problem_)
            Error(problem);
        hud_problem_ = problem;
        return false;
    }
    if (!hud_problem_.empty())
        Log("HUD view drawable again after: " + hud_problem_);
    hud_problem_.clear();
    Rml::Element *layer = HudLayer();
    if (!layer)
        return false;
    bool complete = MountHudElements(layer, view);
    complete = ShowHudOverlay(layer, view) && complete;
    complete = ShowHudSheet(layer, view) && complete;
    float clear = 0.0f;
    for (const hud::LayoutElement &element : layout_.at(view.layout))
        if (element.toast_clear > 0.0f && view.elements.contains(element.id))
            clear = std::max(clear, element.toast_clear);
    SetToastClear(clear);
    return complete;
}

// The HUD document's layer the scene's elements are mounted in, the document loaded and shown; null (an error)
// when the pack has none.
Rml::Element *HostImpl::HudLayer()
{
    if (!hud_document_)
    {
        hud_document_ = hud_path_.empty() ? nullptr : LoadPackDocument(hud_path_);
        if (!hud_document_)
        {
            Error("UI pack has no HUD document");
            return nullptr;
        }
        ApplyHudButtonOpacity();
        if (overlay_)
            overlay_->PullToFront();
    }
    auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
    if (root && !ComponentSlot(*root))
        root->RebuildNow();
    Rml::Element *layer = root ? ComponentSlot(*root) : nullptr;
    if (!layer)
    {
        Error("UI pack: the HUD document has no <s-hud id=\"hud\"> layer");
        return nullptr;
    }
    if (!hud_document_->IsVisible())
    {
        changed_ = true;
        hud_document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    return layer;
}

// The scene's elements. They are patched in place: one that both the old and the new view have stays
// mounted (a held stick survives explore ↔ combat), only its place and props follow.
bool HostImpl::MountHudElements(Rml::Element *layer, const hud::View &view)
{
    const std::vector<hud::LayoutElement> &elements = layout_.at(view.layout);
    std::unordered_map<std::string, HudMade> wanted; // id → what it must have been created as
    for (const hud::LayoutElement &element : elements)
        if (view.elements.contains(element.id))
        {
            HudMade &made = wanted[element.id];
            made.element = &element;
            for (const hud::LayoutElement &child : element.children)
                if (view.elements.contains(child.id))
                    made.children.push_back(&child);
        }
    // The same element of another layout scene (explore and combat) stays mounted when it is made the same way.
    const auto same = [](const HudMade &a, const HudMade &b) {
        const auto made = [](const hud::LayoutElement &x, const hud::LayoutElement &y) {
            return x.id == y.id && x.component == y.component && x.behavior == y.behavior && x.data == y.data;
        };
        return a.element && b.element && made(*a.element, *b.element) &&
               std::ranges::equal(a.children, b.children, [&](auto *x, auto *y) { return made(*x, *y); });
    };
    for (int i = layer->GetNumChildren(); i-- > 0;)
    {
        Rml::Element *mounted = layer->GetChild(i);
        const auto keep = wanted.find(mounted->GetId());
        if (mounted->GetId() == "sheet" || mounted->GetId() == "overlay" ||
            (keep != wanted.end() && same(hud_made_[mounted->GetId()], keep->second)))
            continue;
        RemoveHudElement(layer, mounted);
    }
    bool complete = true;
    Rml::Element *before = layer->GetElementById("overlay"); // layout order: later elements are drawn on top
    if (!before)
        before = layer->GetElementById("sheet");
    for (size_t i = elements.size(); i-- > 0;)
    {
        const hud::LayoutElement &element = elements[i];
        if (!wanted.count(element.id))
            continue;
        Rml::Element *mounted = hud_document_->GetElementById(element.id);
        if (!mounted)
        {
            Rml::ElementPtr created = CreateHudElement(element);
            for (const hud::LayoutElement &child : element.children)
                if (view.elements.contains(child.id))
                {
                    Rml::ElementPtr inner = CreateHudElement(child);
                    inner->SetAttribute("style", "width: 100%;");
                    created->AppendChild(std::move(inner));
                }
            mounted = before ? layer->InsertBefore(std::move(created), before) : layer->AppendChild(std::move(created));
            hud_made_[element.id] = wanted[element.id];
        }
        before = mounted;
        const std::string &style = left_handed_ ? element.rcss_mirrored : element.rcss;
        if (mounted->GetAttribute<Rml::String>("style", "") != style)
        {
            changed_ = true;
            mounted->SetAttribute("style", style);
        }
        complete = SetHudProps(element, view) && complete;
        for (const hud::LayoutElement &child : element.children)
            if (view.elements.contains(child.id))
                complete = SetHudProps(child, view) && complete;
        const bool dim = std::find(view.dimmed.begin(), view.dimmed.end(), element.id) != view.dimmed.end();
        if (mounted->IsClassSet("dim") != dim)
        {
            changed_ = true;
            mounted->SetClass("dim", dim);
        }
    }
    return complete;
}

// The overlay (the spyglass card) lies over the scene's elements and under the sheet.
bool HostImpl::ShowHudOverlay(Rml::Element *layer, const hud::View &view)
{
    bool complete = true;
    Rml::Element *overlay = hud_document_->GetElementById("overlay");
    if (overlay && overlay->GetTagName() != view.overlay)
    {
        RemoveHudElement(layer, overlay);
        overlay = nullptr;
    }
    if (!view.overlay.empty())
    {
        if (!overlay)
        {
            Rml::ElementPtr created = hud_document_->CreateElement(view.overlay);
            created->SetId("overlay");
            Rml::Element *above = layer->GetElementById("sheet");
            overlay = above ? layer->InsertBefore(std::move(created), above) : layer->AppendChild(std::move(created));
        }
        if (auto *component = dynamic_cast<ComponentElement *>(overlay))
            component->SetProps(view.overlay_props);
        else
        {
            Error("UI pack has no HUD overlay " + view.overlay);
            complete = false;
        }
    }
    return complete;
}

// The sheet lies over everything else of the scene.
bool HostImpl::ShowHudSheet(Rml::Element *layer, const hud::View &view)
{
    bool complete = true;
    Rml::Element *sheet = hud_document_->GetElementById("sheet");
    if (sheet && sheet->GetTagName() != view.sheet)
    {
        RemoveHudElement(layer, sheet);
        sheet = nullptr;
    }
    if (!view.sheet.empty())
    {
        if (!sheet)
        {
            changed_ = true;
            Rml::ElementPtr created = hud_document_->CreateElement(view.sheet);
            created->SetId("sheet");
            sheet = layer->AppendChild(std::move(created));
        }
        if (auto *component = dynamic_cast<ComponentElement *>(sheet))
            component->GiveProps(view.sheet_props);
        else
        {
            Error("UI pack has no HUD sheet " + view.sheet);
            complete = false;
        }
    }
    return complete;
}

bool HostImpl::Measure(const std::string &id, const std::string &selector, hud::Placement &out) const
{
    Rml::Element *element = hud_document_ && hud_document_->IsVisible() ? hud_document_->GetElementById(id) : nullptr;
    if (element && !selector.empty())
        element = element->QuerySelector(selector);
    if (!element || !element->IsVisible(true))
        return false;
    const Rml::Vector2f origin = element->GetAbsoluteOffset(Rml::BoxArea::Border),
                        size = element->GetBox().GetSize(Rml::BoxArea::Border);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return false;
    auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
    Rml::Element *layer = root ? ComponentSlot(*root) : nullptr;
    const Rml::Vector2f corner = layer ? layer->GetAbsoluteOffset(Rml::BoxArea::Border) : Rml::Vector2f(0.0f, 0.0f);
    const Rml::Vector2f extent =
        layer ? layer->GetBox().GetSize(Rml::BoxArea::Border)
              : Rml::Vector2f(static_cast<float>(config_.width), static_cast<float>(config_.height));
    out = {origin.x,
           origin.y,
           size.x,
           size.y,
           corner.x,
           corner.y,
           static_cast<float>(config_.width),
           static_cast<float>(config_.height),
           config_.density,
           extent.x,
           extent.y};
    return true;
}

void HostImpl::HideHud()
{
    if (!hud_document_)
        return;
    if (hud_document_->IsVisible())
        changed_ = true;
    auto *root = dynamic_cast<ComponentElement *>(hud_document_->GetElementById("hud"));
    if (Rml::Element *layer = root ? ComponentSlot(*root) : nullptr)
        while (layer->GetNumChildren() > 0)
            RemoveHudElement(layer, layer->GetLastChild());
    hud_document_->Hide();
    SetToastClear(0.0f);
}

Rml::ElementPtr HostImpl::CreateHudElement(const hud::LayoutElement &element)
{
    changed_ = true;
    Rml::ElementPtr created = hud_document_->CreateElement(element.component);
    created->SetId(element.id);
    if (!element.behavior.empty())
        created->SetAttribute("data-behavior", element.behavior);
    for (const auto &item : element.data.items())
        created->SetAttribute("data-" + item.key(), ToText(item.value()));
    return created;
}

// Layout props with the scene's live ones on top; a rebuild only when they changed.
bool HostImpl::SetHudProps(const hud::LayoutElement &element, const hud::View &view)
{
    auto *component = dynamic_cast<ComponentElement *>(hud_document_->GetElementById(element.id));
    if (!component)
    {
        Error("UI pack has no component <" + element.component + "> for HUD element " + element.id);
        return false;
    }
    Json props = element.props;
    if (left_handed_)
        props.update(element.props_mirrored);
    props.update(view.elements[element.id]);
    component->GiveProps(props);
    return true;
}

// Takes a HUD element off the screen. A pointer held on it ends first, so that its
// control reports the release (a stick stops, a held button lets go).
void HostImpl::RemoveHudElement(Rml::Element *layer, Rml::Element *mounted)
{
    changed_ = true;
    std::vector<std::pair<int, Rml::Vector2f>> held;
    for (const auto &capture : captures_)
        for (Rml::Element *at = capture.second.component.get(); at; at = at->GetParentNode())
            if (at == mounted)
            {
                held.emplace_back(capture.first, capture.second.pointer.position);
                break;
            }
    for (const auto &pointer : held)
        Touch(TouchPhase::Cancel, pointer.first, pointer.second.x, pointer.second.y);
    hud_made_.erase(mounted->GetId());
    layer->RemoveChild(mounted);
}

const ComponentSpec *HostImpl::PackComponent(const std::string &tag) const
{
    return registry_->Find(tag);
}

const hud::LayoutElement *HostImpl::HudElement(const std::string &layout, const std::string &element) const
{
    return hud::FindLayoutElement(layout_, layout, element);
}

} // namespace storm::ui
