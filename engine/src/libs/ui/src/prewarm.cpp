#include "prewarm.h"

#include "elements.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/TextShapingContext.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace storm::ui
{

Json FontUseJson(const FontUse &use)
{
    Json entry = {{"family", use.family}, {"weight", use.weight}, {"size", use.size}};
    if (use.italic)
        entry["italic"] = true;
    if (!use.effect.empty())
        entry["effect"] = use.effect;
    if (use.min > 0.0)
        entry["min"] = use.min;
    if (use.handle)
        entry["handle"] = true;
    return entry;
}

bool ParseFontUse(const Json &entry, FontUse &use, std::string &why)
{
    if (!entry.is_object() || !entry.contains("family") || !entry["family"].is_string() || !entry.contains("size") ||
        !entry["size"].is_number() || !entry.value("weight", Json(400)).is_number_integer())
    {
        why = "expected {family, weight, size[, italic][, effect]}: " + entry.dump();
        return false;
    }
    use.family = entry["family"].get<std::string>();
    use.weight = entry.value("weight", 400);
    use.size = entry["size"].get<double>();
    use.italic = entry.value("italic", false);
    use.effect = entry.value("effect", std::string());
    use.min = entry.value("min", 0.0);
    use.handle = entry.value("handle", false);
    if (use.family.empty() || use.size <= 0.0 || use.min < 0.0 || use.min > use.size ||
        ((use.min > 0.0 || use.handle) && !use.effect.empty()) || (use.handle && use.min > 0.0))
    {
        why = "font use: needs a family and a size above 0; min or handle only without an effect, not both: " +
              entry.dump();
        return false;
    }
    return true;
}

namespace
{

FontUse FontOf(Rml::Element &element, float dp_ratio, float size_px)
{
    const Rml::ComputedValues &values = element.GetComputedValues();
    FontUse use;
    use.family = values.font_family();
    use.italic = values.font_style() == Rml::Style::FontStyle::Italic;
    use.weight = static_cast<int>(values.font_weight());
    // The handle is made at the truncated pixel size: the dp that gives it back at this ratio, to 1/1000.
    use.size = std::round(static_cast<double>(size_px) / dp_ratio * 1000.0) / 1000.0;
    return use;
}

} // namespace

void CollectFontUses(Rml::Element *root, float dp_ratio, std::set<FontUse> &out)
{
    if (!root || dp_ratio <= 0.0f)
        return;
    const Rml::ComputedValues &values = root->GetComputedValues();
    if (!values.font_family().empty() && values.font_size() >= 1.0f)
    {
        FontUse use = FontOf(*root, dp_ratio, values.font_size());
        auto *text = dynamic_cast<Rml::ElementText *>(root);
        if (text && !text->GetText().empty())
        {
            if (values.has_font_effect())
                if (const Rml::Property *property = text->GetProperty(Rml::PropertyId::FontEffect))
                    if (const Rml::FontEffectsPtr effects = property->Get<Rml::FontEffectsPtr>())
                        use.effect = effects->value;
        }
        else
            use.handle = true;
        out.insert(std::move(use));
    }
    float base_px = 0.0f, low_px = 0.0f;
    if (ClampSteps(root, base_px, low_px))
    {
        FontUse use = FontOf(*root, dp_ratio, base_px);
        use.min = std::min(std::round(static_cast<double>(low_px) / dp_ratio * 1000.0) / 1000.0, use.size);
        out.insert(std::move(use));
    }
    for (int i = 0; i < root->GetNumChildren(true); ++i)
        CollectFontUses(root->GetChild(i), dp_ratio, out);
}

std::set<FontUse> WithoutCoveredHandles(const std::set<FontUse> &uses)
{
    std::set<FontUse> kept;
    for (const FontUse &use : uses)
        if (!use.handle || !std::any_of(uses.begin(), uses.end(), [&](const FontUse &other) {
                return !other.handle && other.min == 0.0 && other.SameHandle(use);
            }))
            kept.insert(use);
    return kept;
}

bool Covers(const std::set<FontUse> &listed, const FontUse &use)
{
    if (listed.count(use))
        return true;
    return use.handle && std::any_of(listed.begin(), listed.end(),
                                     [&](const FontUse &other) { return other.min == 0.0 && other.SameHandle(use); });
}

std::string BlockSample(const std::string &utf8)
{
    std::set<char32_t> blocks = {0}; // ASCII (and the Latin-1 supplement a handle loads with it)
    std::string sample = "A";
    for (Rml::StringIteratorU8 it(utf8); it;)
    {
        const char *start = it.get();
        const auto code = static_cast<char32_t>(*it);
        ++it;
        // The Latin-1 supplement comes with ASCII; ideographic scripts (U+2E80 on) load one glyph at a time.
        if (code >= 0x100 && code < 0x2E80 && blocks.insert(code >> 7).second)
            sample.append(start, it.get());
    }
    return sample;
}

void Warmer::Reset(std::vector<FontUse> uses, std::string sample, float dp_ratio, double narrower)
{
    uses_ = std::move(uses);
    sample_ = std::move(sample);
    dp_ratio_ = dp_ratio;
    narrower_ = std::clamp(narrower, 0.0, 1.0);
    next_ = 0;
}

bool Warmer::Step(Rml::RenderManager &render, double &made_ms, std::string &error)
{
    made_ms = 0.0;
    if (Done())
        return false;
    const auto started = std::chrono::steady_clock::now();
    const FontUse &use = uses_[next_++];
    Rml::FontEngineInterface *fonts = Rml::GetFontEngineInterface();
    const Rml::Style::FontStyle style = use.italic ? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal;
    const Rml::String language;
    const Rml::TextShapingContext shaping{language};
    if (use.handle)
    {
        // RmlUi's handle for an element's font: made with ASCII and the Latin-1 supplement, nothing more.
        if (!fonts->GetFontFaceHandle(use.family, style, static_cast<Rml::Style::FontWeight>(use.weight),
                                      static_cast<int>(use.size * dp_ratio_ + 1e-3)))
            error = "UI prewarm: the pack has no font face '" + use.family + "' its font uses name";
        made_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return !Done();
    }
    if (use.min > 0.0)
    {
        // As Fit steps down: from the style sheet's size, rounded, to the size that fits, in a narrower layout smaller.
        const int base = static_cast<int>(std::lround(use.size * dp_ratio_));
        const int least = std::clamp(static_cast<int>(use.min * narrower_ * dp_ratio_ + 1e-3), 1, std::max(base, 1));
        for (int pixels = base; pixels >= least; --pixels)
            if (const Rml::FontFaceHandle handle = fonts->GetFontFaceHandle(
                    use.family, style, static_cast<Rml::Style::FontWeight>(use.weight), pixels))
                fonts->GetStringWidth(handle, sample_, shaping);
            else
            {
                error = "UI prewarm: the pack has no font face '" + use.family + "' its font uses name";
                break;
            }
        made_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return !Done();
    }
    // As RmlUi sizes a handle (ElementStyle: the computed pixel size, truncated); the 1/1000 dp of the list stays
    // below.
    const int pixels = static_cast<int>(use.size * dp_ratio_ + 1e-3);
    const Rml::FontFaceHandle handle =
        fonts->GetFontFaceHandle(use.family, style, static_cast<Rml::Style::FontWeight>(use.weight), pixels);
    if (!handle)
    {
        error = "UI prewarm: the pack has no font face '" + use.family + "' (weight " + std::to_string(use.weight) +
                (use.italic ? ", italic" : "") + ") its font uses name";
        return !Done();
    }
    static const Rml::FontEffectList none;
    const Rml::FontEffectList *effects = &none;
    Rml::PropertyDictionary declaration;
    Rml::FontEffectsPtr parsed;
    if (!use.effect.empty())
    {
        if (Rml::StyleSheetSpecification::ParsePropertyDeclaration(declaration, "font-effect", use.effect))
            if (const Rml::Property *property = declaration.GetProperty(Rml::PropertyId::FontEffect))
                parsed = property->Get<Rml::FontEffectsPtr>();
        if (!parsed)
        {
            error = "UI prewarm: font effect '" + use.effect + "' does not parse";
            return !Done();
        }
        effects = &parsed->list;
    }
    const Rml::FontEffectsHandle effects_handle = fonts->PrepareFontEffects(handle, *effects);
    // The width loads the glyph blocks of the sample; the meshes name the layer textures, drawn now (their callbacks:
    // the glyphs and the font effects rasterised, the texture handed to the backend).
    fonts->GetStringWidth(handle, sample_, shaping);
    Rml::TexturedMeshList meshes;
    fonts->GenerateString(render, handle, effects_handle, sample_, {0.0f, 0.0f},
                          Rml::ColourbPremultiplied(255, 255, 255, 255), 1.0f, shaping, meshes);
    for (const Rml::TexturedMesh &mesh : meshes)
        if (mesh.texture)
            mesh.texture.GetDimensions();
    made_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return !Done();
}

} // namespace storm::ui
