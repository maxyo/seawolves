#include "shadow_element.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>
#include <memory>

namespace storm::ui
{

namespace
{

Rml::PropertyId g_shadow = Rml::PropertyId::Invalid;

const Rml::BoxShadowList *Shadows(Rml::Element &element)
{
    const Rml::Property *property = element.GetLocalProperty(g_shadow);
    if (!property || property->value.GetType() != Rml::Variant::BOXSHADOWLIST)
        return nullptr;
    const Rml::BoxShadowList &list = property->value.GetReference<Rml::BoxShadowList>();
    return list.empty() ? nullptr : &list;
}

// Corner radii scaled as CSS does so that adjacent curves do not overlap (as RmlUi draws the background).
Rml::CornerSizes Fitted(Rml::CornerSizes radii, Rml::Vector2f size)
{
    float scale = 1.0f;
    const auto limit = [&scale](float side, float a, float b) {
        if (a + b > 0.0f)
            scale = std::min(scale, side / (a + b));
    };
    limit(size.x, radii[0], radii[1]);
    limit(size.y, radii[1], radii[2]);
    limit(size.x, radii[2], radii[3]);
    limit(size.y, radii[3], radii[0]);
    for (float &radius : radii)
        radius = Rml::Math::Round(radius * scale);
    return radii;
}

Rml::Vector4f Vector(const Rml::CornerSizes &radii)
{
    return {radii[0], radii[1], radii[2], radii[3]};
}

// A rectangle as the shader takes it: centre, half size.
Rml::Vector4f Box(Rml::Vector2f position, Rml::Vector2f size)
{
    return {position.x + size.x * 0.5f, position.y + size.y * 0.5f, size.x * 0.5f, size.y * 0.5f};
}

// RmlUi's colour filters, with the shadow's reach added to the filtered region: drawn by the element, not by RmlUi's
// box-shadow, it is outside what RmlUi counts as the element's ink.
class ColourFilter final : public Rml::Filter
{
  public:
    ColourFilter(Rml::String name, float value) : name_(std::move(name)), value_(value)
    {
    }
    Rml::CompiledFilter CompileFilter(Rml::Element *element) const override
    {
        return element->GetRenderManager()->CompileFilter(name_, Rml::Dictionary{{"value", Rml::Variant(value_)}});
    }
    void ExtendInkOverflow(Rml::Element *element, Rml::Rectanglef &region) const override
    {
        if (auto *shadowed = dynamic_cast<ShadowedElement *>(element))
        {
            const Rml::Vector4f reach = shadowed->ShadowReach();
            region = region.Extend({reach.x, reach.y}, {reach.z, reach.w});
        }
    }

  private:
    Rml::String name_;
    float value_;
};

class ColourFilterInstancer final : public Rml::FilterInstancer
{
  public:
    ColourFilterInstancer(bool angle, const char *fallback)
    {
        value_ = RegisterProperty("value", fallback).AddParser(angle ? "angle" : "number_percent").GetId();
        RegisterShorthand("filter", "value", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String &name,
                                               const Rml::PropertyDictionary &properties) override
    {
        const Rml::Property *property = properties.GetProperty(value_);
        if (!property)
            return nullptr;
        float value = property->Get<float>();
        if (property->unit == Rml::Unit::PERCENT)
            value *= 0.01f;
        else if (property->unit == Rml::Unit::DEG)
            value = Rml::Math::DegreesToRadians(value);
        return Rml::MakeShared<ColourFilter>(name, value);
    }

  private:
    Rml::PropertyId value_;
};

} // namespace

Rml::Vector4f ShadowedElement::ShadowReach()
{
    Rml::Vector4f reach(0.f);
    if (const Rml::BoxShadowList *list = Shadows(*this))
        for (const Rml::BoxShadow &shadow : *list)
        {
            if (shadow.inset)
                continue;
            // Three sigmas of the blur (sigma = blur / 2, as RmlUi) and the spread, moved by the offset.
            const float extent = 1.5f * ResolveLength(shadow.blur_radius) + ResolveLength(shadow.spread_distance);
            const float x = ResolveLength(shadow.offset_x), y = ResolveLength(shadow.offset_y);
            reach.x = std::max(reach.x, extent - x);
            reach.y = std::max(reach.y, extent - y);
            reach.z = std::max(reach.z, extent + x);
            reach.w = std::max(reach.w, extent + y);
        }
    return reach;
}

void ShadowedElement::OnPropertyChange(const Rml::PropertyIdSet &changed)
{
    Rml::Element::OnPropertyChange(changed);
    dirty_ = true; // a change of the shadow, the radii, the border widths or the opacity: cheap to make again
}

void ShadowedElement::OnDpRatioChange()
{
    Rml::Element::OnDpRatioChange();
    dirty_ = true;
}

void ShadowedElement::OnRender()
{
    Rml::RenderManager *manager = GetRenderManager();
    if (!manager)
        return;
    const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Border);
    if (dirty_ || size != built_size_)
    {
        Build(*manager);
        built_size_ = size;
        dirty_ = false;
    }
    if (shadows_.empty())
        return;
    const Rml::Vector2f origin = GetAbsoluteOffset(Rml::BoxArea::Border);
    // The first shadow of the list is on top, as RmlUi's box-shadow.
    for (auto shadow = shadows_.rbegin(); shadow != shadows_.rend(); ++shadow)
        shadow->quad.Render(origin, {}, shadow->shader);
}

void ShadowedElement::Build(Rml::RenderManager &manager)
{
    shadows_.clear();
    const Rml::BoxShadowList *list = Shadows(*this);
    if (!list)
        return;
    const Rml::Box &box = GetBox();
    const Rml::Vector2f border_size = box.GetSize(Rml::BoxArea::Border);
    if (border_size.x <= 0.f || border_size.y <= 0.f)
        return;
    const Rml::ComputedValues &computed = GetComputedValues();
    const Rml::CornerSizes radii = Fitted(computed.border_radius(), border_size);
    const float left = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Left),
                top = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Top),
                right = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Right),
                bottom = box.GetEdge(Rml::BoxArea::Border, Rml::BoxEdge::Bottom);
    const Rml::Vector2f padding_position(left, top);
    const Rml::Vector2f padding_size = box.GetSize(Rml::BoxArea::Padding);
    const Rml::CornerSizes padding_radii = {
        std::max(0.f, radii[0] - std::max(left, top)), std::max(0.f, radii[1] - std::max(right, top)),
        std::max(0.f, radii[2] - std::max(right, bottom)), std::max(0.f, radii[3] - std::max(left, bottom))};
    const float opacity = computed.opacity();

    for (const Rml::BoxShadow &shadow : *list)
    {
        const float blur = ResolveLength(shadow.blur_radius), spread = ResolveLength(shadow.spread_distance);
        const Rml::Vector2f offset(ResolveLength(shadow.offset_x), ResolveLength(shadow.offset_y));
        const bool inset = shadow.inset;
        // The shape's corners grow (shrink, inset) with the spread as in RmlUi's box-shadow (GeometryBoxShadow).
        Rml::CornerSizes shape_radii = radii;
        for (float &radius : shape_radii)
        {
            float factor = inset ? -1.f : 1.f;
            if (radius < spread)
            {
                const float ratio = radius / spread - 1.f;
                factor *= 1.f + ratio * ratio * ratio;
            }
            radius = std::max(radius + factor * spread, 0.f);
        }
        const float grow = inset ? -spread : spread;
        const Rml::Vector2f base_position = inset ? padding_position : Rml::Vector2f(0.f);
        const Rml::Vector2f base_size = inset ? padding_size : border_size;
        const Rml::Vector2f shape_position = base_position - Rml::Vector2f(grow) + offset;
        const Rml::Vector2f shape_size = Rml::Math::Max(base_size + Rml::Vector2f(2.f * grow), Rml::Vector2f(0.001f));

        // An outer shadow covers its shape and three sigmas around it (and one pixel for the edge); an inset one the
        // padding box it is kept to.
        Rml::Vector2f quad_position = padding_position, quad_size = padding_size;
        if (!inset)
        {
            const float reach = 1.5f * blur + 1.f;
            quad_position = shape_position - Rml::Vector2f(reach);
            quad_size = shape_size + Rml::Vector2f(2.f * reach);
        }
        if (quad_size.x <= 0.f || quad_size.y <= 0.f)
            continue;
        Rml::Mesh mesh;
        const auto faded = [opacity](Rml::byte value) { return Rml::byte(float(value) * opacity + 0.5f); };
        const Rml::ColourbPremultiplied colour(faded(shadow.color.red), faded(shadow.color.green),
                                               faded(shadow.color.blue), faded(shadow.color.alpha));
        const Rml::Vector2f corners[4] = {quad_position,
                                          {quad_position.x + quad_size.x, quad_position.y},
                                          quad_position + quad_size,
                                          {quad_position.x, quad_position.y + quad_size.y}};
        for (const Rml::Vector2f &corner : corners)
            mesh.vertices.push_back({corner, colour, corner}); // tex_coord: the element's pixels, for the shader
        mesh.indices = {0, 1, 2, 0, 2, 3};

        const Rml::Vector4f cut = inset ? Box(padding_position, padding_size) : Box({0.f, 0.f}, border_size);
        const Rml::Vector4f cut_radii = Vector(inset ? padding_radii : radii);
        BoxShadowQuad drawn;
        // sigma = blur / 2: RmlUi's convention for box-shadow (and CSS's); below half a pixel RmlUi does not blur.
        drawn.shader = manager.CompileShader(
            "ui-shadow", Rml::Dictionary{{"shape", Rml::Variant(Box(shape_position, shape_size))},
                                         {"shape_radii", Rml::Variant(Vector(shape_radii))},
                                         {"cut", Rml::Variant(cut)},
                                         {"cut_radii", Rml::Variant(cut_radii)},
                                         {"blur", Rml::Variant(Rml::Vector4f(blur >= 0.5f ? 0.5f * blur : 0.f,
                                                                             inset ? 1.f : 0.f, 0.f, 0.f))}});
        if (!drawn.shader)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "UI shadow of <%s>: the backend has no ui-shadow shader",
                              GetTagName().c_str());
            continue;
        }
        drawn.quad = manager.MakeGeometry(std::move(mesh));
        shadows_.push_back(std::move(drawn));
    }
}

void RegisterShadows()
{
    g_shadow =
        Rml::StyleSheetSpecification::RegisterProperty("shadow", "none", false, false).AddParser("box_shadow").GetId();
    static Rml::ElementInstancerGeneric<ShadowedElement> element;
    Rml::Factory::RegisterElementInstancer("*", &element);
    // Made again for every RmlUi initialisation: their property parsers are those of the style sheet specification it
    // made (a host started after another's shutdown).
    static std::unique_ptr<ColourFilterInstancer> angle, zero, one;
    angle = std::make_unique<ColourFilterInstancer>(true, "0rad");
    zero = std::make_unique<ColourFilterInstancer>(false, "0");
    one = std::make_unique<ColourFilterInstancer>(false, "1");
    Rml::Factory::RegisterFilterInstancer("hue-rotate", angle.get());
    for (const char *name : {"brightness", "contrast", "opacity", "saturate"})
        Rml::Factory::RegisterFilterInstancer(name, one.get());
    for (const char *name : {"grayscale", "invert", "sepia"})
        Rml::Factory::RegisterFilterInstancer(name, zero.get());
}

} // namespace storm::ui
