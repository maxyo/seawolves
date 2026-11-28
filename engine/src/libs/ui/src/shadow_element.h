// The pack's `shadow` property (box-shadow's syntax, parsed by RmlUi's own box-shadow parser): every element of the
// runtime draws its shadows itself, after its background, border and decorators (as CSS paints an inset shadow above
// the background, and an outer one cut out of the border box), one quad each with the backend's "ui-shadow" program.
// RmlUi's own box-shadow renders a layer, blurs it and saves it as a texture; the pack build refuses it.
#pragma once

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

#include <vector>

namespace storm::ui
{

class ShadowedElement : public Rml::Element
{
  public:
    explicit ShadowedElement(const Rml::String &tag) : Rml::Element(tag)
    {
    }
    // How far the outer shadows reach beyond the border box (left, top, right, bottom; px).
    Rml::Vector4f ShadowReach();

  protected:
    void OnRender() override;
    void OnPropertyChange(const Rml::PropertyIdSet &changed) override;
    void OnDpRatioChange() override;

  private:
    void Build(Rml::RenderManager &manager);

    struct BoxShadowQuad
    {
        Rml::Geometry quad;
        Rml::CompiledShader shader;
    };
    std::vector<BoxShadowQuad> shadows_;
    bool dirty_ = true;
    Rml::Vector2f built_size_;
};

// The property, the instancer of every element without its own, and the colour filters that keep a filtered
// element's outer shadow inside the filtered region. After Rml::Initialise.
void RegisterShadows();

} // namespace storm::ui
