// <ui-icon name="ship">: one icon of the pack's atlas (app/mobile-ui/design/icon-atlas, baked from the SVG symbols by
// tools/icon_bake.cpp), drawn as quads tinted by their vertex colour: one texture for every icon, colour, size and
// fade, nothing rasterised on the device. One-colour symbols take the element's `color`; full-colour symbols
// (nation-*, c-*) keep their own; `plain` draws a one-colour symbol in its own fills (black). The accent layer of a
// symbol, when it has one, is drawn on top, untinted.
//
// A one-colour glyph is fill * T + W (icon_bake.cpp): the glyph cell (T, coverage) tinted by the colour, then where
// the symbol has white shading its light cell (W, 0) added untinted. The atlas holds each icon at a chain of sizes; an
// icon draws the smallest at least as large as itself, scaled down (above the largest it is scaled up).
//
// `icon-shadow: <colour> <x> <y> <sigma>`: a drop shadow under the glyph (as `filter: drop-shadow(...)` without its
// offscreen layer): the atlas's blurred coverage whose blur at this size is nearest to sigma, offset and tinted; a
// sigma below half the finest blur draws the glyph cell itself: exact for a black shadow; a coloured one (the light
// engraving on parchment) shows the glyph's dark shading where it reaches the outline. Full-colour icons always take a
// blurred cell (no style gives them a hard one).
#include "elements.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace storm::ui
{

namespace
{

struct Point
{
    int x = 0, y = 0;
};

struct Atlas
{
    std::vector<int> levels; // px, largest first
    std::vector<int> shadow_levels;
    float shadow_sigma = 1.0f; // texels
    int shadow_margin = 0;     // texels around a shadow cell's glyph
    std::string coverage_source, colour_source;
    Rml::Vector2f coverage_size, colour_size;
    struct Icon
    {
        std::vector<Point> glyph, light, accent, colour, shadow; // a cell per level
    };
    std::map<std::string, Icon, std::less<>> icons;
    std::unordered_set<std::string> reported; // missing icons logged
};

Atlas &TheAtlas()
{
    static Atlas atlas;
    return atlas;
}

struct Shadow
{
    Rml::Colourb colour{0, 0, 0, 0};
    float x = 0, y = 0, sigma = 0; // pixels
    bool On() const
    {
        return colour.alpha > 0;
    }
};

struct ShadowProperties
{
    Rml::PropertyId colour, x, y, sigma;
} shadow_ids;

// Appends the quad showing `cell` (side `texels` of a page of `page` size) at `origin`, `side` px.
void Quad(Rml::Mesh &mesh, Point cell, int texels, Rml::Vector2f page, Rml::Vector2f origin, float side,
          Rml::ColourbPremultiplied colour)
{
    const Rml::Vector2f from = {cell.x / page.x, cell.y / page.y};
    const Rml::Vector2f to = {(cell.x + texels) / page.x, (cell.y + texels) / page.y};
    Rml::MeshUtilities::GenerateQuad(mesh, origin, {side, side}, colour, from, to);
}

// The debug builds' mark of an icon the atlas lacks: a red frame with a cross, untextured.
void MissingIconMarker(Rml::Mesh &mesh, Rml::Vector2f origin, float side)
{
    const Rml::ColourbPremultiplied red = Rml::Colourb(0xc4, 0x47, 0x3a, 0xff).ToPremultiplied(); // dangerFill
    const float line = std::max(1.0f, std::round(side / 12.0f));
    Rml::MeshUtilities::GenerateQuad(mesh, origin, {side, line}, red);
    Rml::MeshUtilities::GenerateQuad(mesh, origin + Rml::Vector2f(0, side - line), {side, line}, red);
    Rml::MeshUtilities::GenerateQuad(mesh, origin, {line, side}, red);
    Rml::MeshUtilities::GenerateQuad(mesh, origin + Rml::Vector2f(side - line, 0), {line, side}, red);
    for (float at = 0.0f; at < side; at += line) // the cross, in steps
    {
        Rml::MeshUtilities::GenerateQuad(mesh, origin + Rml::Vector2f(at, at), {line, line}, red);
        Rml::MeshUtilities::GenerateQuad(mesh, origin + Rml::Vector2f(side - line - at, at), {line, line}, red);
    }
}

class IconElement final : public Rml::Element
{
  public:
    explicit IconElement(const Rml::String &tag) : Rml::Element(tag)
    {
    }

  protected:
    bool GetIntrinsicDimensions(Rml::Vector2f &dimensions, float &ratio) override
    {
        float dp = 1.0f;
        if (Rml::Context *context = GetContext())
            dp = context->GetDensityIndependentPixelRatio();
        const float size = GetAttribute<float>("size", 24.0f) * dp;
        dimensions = {size, size};
        ratio = 1.0f;
        return true;
    }

    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (changed.count("size"))
            DirtyLayout();
    }

    void OnRender() override
    {
        Rml::RenderManager *manager = GetRenderManager();
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        const std::string name = GetAttribute<Rml::String>("name", "");
        if (!manager || size.x < 1.0f || size.y < 1.0f || name.empty())
            return;
        Drawn drawn;
        drawn.plain = HasAttribute("plain");
        drawn.id = IconSymbolId(name, drawn.plain);
        drawn.colour = GetComputedValues().color();
        drawn.opacity = GetComputedValues().opacity();
        drawn.pixels = std::max(1, static_cast<int>(std::lround(std::min(size.x, size.y))));
        if (const Rml::Property *shadow_colour = GetProperty(shadow_ids.colour))
            drawn.shadow.colour = shadow_colour->Get<Rml::Colourb>();
        if (drawn.shadow.On())
        {
            drawn.shadow.x = ResolveLength(GetProperty(shadow_ids.x)->GetNumericValue());
            drawn.shadow.y = ResolveLength(GetProperty(shadow_ids.y)->GetNumericValue());
            drawn.shadow.sigma = std::max(0.0f, ResolveLength(GetProperty(shadow_ids.sigma)->GetNumericValue()));
        }
        if (!(drawn == built_))
            Build(*manager, size, drawn);
        const Rml::Vector2f offset = GetAbsoluteOffset(Rml::BoxArea::Content);
        const Rml::Vector2f at = {std::round(offset.x), std::round(offset.y)};
        if (coverage_)
            coverage_.Render(at, manager->LoadTexture(TheAtlas().coverage_source));
        if (colour_)
            colour_.Render(at, manager->LoadTexture(TheAtlas().colour_source));
        if (marker_)
            marker_.Render(at);
    }

  private:
    struct Drawn
    {
        std::string id;
        bool plain = false;
        Rml::Colourb colour;
        float opacity = 1.0f;
        int pixels = 0;
        Shadow shadow;
        bool operator==(const Drawn &other) const
        {
            return id == other.id && plain == other.plain && colour == other.colour && opacity == other.opacity &&
                   pixels == other.pixels && shadow.colour == other.shadow.colour && shadow.x == other.shadow.x &&
                   shadow.y == other.shadow.y && shadow.sigma == other.shadow.sigma;
        }
    };

    void Build(Rml::RenderManager &manager, Rml::Vector2f size, const Drawn &drawn)
    {
        built_ = drawn;
        const Atlas &atlas = TheAtlas();
        Rml::Mesh coverage = coverage_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
        Rml::Mesh colour = colour_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
        Rml::Mesh marker = marker_.Release(Rml::Geometry::ReleaseMode::ClearMesh);
        const float px = static_cast<float>(drawn.pixels);
        const Rml::Vector2f origin = {std::round((size.x - px) * 0.5f), std::round((size.y - px) * 0.5f)};
        const auto found = atlas.icons.find(drawn.id);
        if (found == atlas.icons.end())
        {
            // No silent blank: logged once per icon, and in debug builds drawn as a crossed red box.
            if (TheAtlas().reported.insert(drawn.id).second)
                Rml::Log::Message(Rml::Log::LT_ERROR, "Icon '%s' is not in the icon pack", drawn.id.c_str());
            const Rml::ElementDocument *document = GetOwnerDocument();
            if (document && document->IsClassSet(kDebugMarksClass))
                MissingIconMarker(marker, origin, px);
            Make(manager, marker_, std::move(marker));
            return;
        }
        const Atlas::Icon &icon = found->second;
        // The smallest level at least as large as the icon.
        size_t level = 0;
        while (level + 1 < atlas.levels.size() && atlas.levels[level + 1] >= drawn.pixels)
            ++level;
        const int texels = atlas.levels[level];
        const auto tint = [&](Rml::Colourb c) { return c.ToPremultiplied(drawn.opacity); };
        const Rml::ColourbPremultiplied white = tint(Rml::Colourb(255, 255, 255, 255));
        const bool full_colour = !icon.colour.empty();
        if (drawn.shadow.On())
        {
            // The shadow level whose blur at this size (px / level * sigma texels) is nearest to the asked one.
            const float wanted = drawn.shadow.sigma;
            size_t best = 0;
            for (size_t i = 1; i < atlas.shadow_levels.size(); ++i)
                if (std::fabs(std::log(wanted * atlas.shadow_levels[i] / (px * atlas.shadow_sigma))) <
                    std::fabs(std::log(wanted * atlas.shadow_levels[best] / (px * atlas.shadow_sigma))))
                    best = i;
            const float finest = px * atlas.shadow_sigma / static_cast<float>(atlas.shadow_levels.front());
            const Rml::Vector2f shift = {drawn.shadow.x, drawn.shadow.y};
            if (wanted < 0.5f * finest && !full_colour)
                Quad(coverage, icon.glyph[level], texels, atlas.coverage_size, origin + shift, px,
                     tint(drawn.shadow.colour));
            else
            {
                const int glyph = atlas.shadow_levels[best], margin = atlas.shadow_margin;
                const float scale = px / static_cast<float>(glyph);
                Quad(coverage, icon.shadow[best], glyph + 2 * margin, atlas.coverage_size,
                     origin + shift - Rml::Vector2f(margin * scale), (glyph + 2 * margin) * scale,
                     tint(drawn.shadow.colour));
            }
        }
        if (full_colour)
            Quad(colour, icon.colour[level], texels, atlas.colour_size, origin, px, white);
        else
        {
            Quad(coverage, icon.glyph[level], texels, atlas.coverage_size, origin, px,
                 tint(drawn.plain ? Rml::Colourb(0, 0, 0, 255) : drawn.colour));
            if (!icon.light.empty())
                Quad(coverage, icon.light[level], texels, atlas.coverage_size, origin, px, white);
            if (!icon.accent.empty() && !drawn.plain)
                Quad(colour, icon.accent[level], texels, atlas.colour_size, origin, px, white);
        }
        Make(manager, coverage_, std::move(coverage));
        Make(manager, colour_, std::move(colour));
    }

    static void Make(Rml::RenderManager &manager, Rml::Geometry &geometry, Rml::Mesh mesh)
    {
        if (!mesh.indices.empty())
            geometry = manager.MakeGeometry(std::move(mesh));
    }

    Rml::Geometry coverage_, colour_, marker_;
    Drawn built_;
};

} // namespace

std::string IconSymbolId(std::string name, bool &plain)
{
    if (name.rfind("i-", 0) == 0)
        name = name.substr(2);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, "-acc") == 0)
        name.resize(name.size() - 4); // the accent layer is never drawn alone
    const bool colour_art = name.rfind("c-", 0) == 0;
    plain = plain || colour_art || name.rfind("nation-", 0) == 0;
    return colour_art ? name : "i-" + name;
}

bool HasIcon(const std::string &name)
{
    bool plain = false;
    return TheAtlas().icons.count(IconSymbolId(name, plain)) > 0;
}

void LoadIconPack(const Json &table, const std::string &dir)
{
    Atlas atlas;
    for (const Json &level : table.value("levels", Json::array()))
        atlas.levels.push_back(level.get<int>());
    if (const Json shadow = table.value("shadow", Json::object()); !shadow.empty())
    {
        for (const Json &level : shadow.at("levels"))
            atlas.shadow_levels.push_back(level.get<int>());
        atlas.shadow_sigma = shadow.at("sigma").get<float>();
        atlas.shadow_margin = shadow.at("margin").get<int>();
    }
    for (const Json &page : table.value("pages", Json::array()))
    {
        const Rml::Vector2f size = {page.at("size")[0].get<float>(), page.at("size")[1].get<float>()};
        const bool coverage = page.at("channels").get<int>() == 2;
        (coverage ? atlas.coverage_source : atlas.colour_source) = dir + page.at("file").get<std::string>();
        (coverage ? atlas.coverage_size : atlas.colour_size) = size;
    }
    const Json icons = table.value("icons", Json::object());
    for (const auto &item : icons.items())
    {
        Atlas::Icon &icon = atlas.icons[item.key()];
        const std::pair<const char *, std::vector<Point> *> parts[] = {{"glyph", &icon.glyph},
                                                                       {"light", &icon.light},
                                                                       {"accent", &icon.accent},
                                                                       {"colour", &icon.colour},
                                                                       {"shadow", &icon.shadow}};
        for (const auto &[part, cells] : parts)
            for (const Json &cell : item.value().value(part, Json::array()))
                cells->push_back({cell[0].get<int>(), cell[1].get<int>()});
    }
    TheAtlas() = std::move(atlas);
}

void RegisterIconElement()
{
    using Rml::StyleSheetSpecification;
    shadow_ids.colour = StyleSheetSpecification::RegisterProperty("icon-shadow-color", "transparent", false, false)
                            .AddParser("color")
                            .GetId();
    shadow_ids.x =
        StyleSheetSpecification::RegisterProperty("icon-shadow-x", "0px", false, false).AddParser("length").GetId();
    shadow_ids.y =
        StyleSheetSpecification::RegisterProperty("icon-shadow-y", "0px", false, false).AddParser("length").GetId();
    shadow_ids.sigma =
        StyleSheetSpecification::RegisterProperty("icon-shadow-sigma", "0px", false, false).AddParser("length").GetId();
    StyleSheetSpecification::RegisterShorthand("icon-shadow",
                                               "icon-shadow-color, icon-shadow-x, icon-shadow-y, icon-shadow-sigma",
                                               Rml::ShorthandType::FallThrough);
    static Rml::ElementInstancerGeneric<IconElement> instancer;
    Rml::Factory::RegisterElementInstancer("ui-icon", &instancer);
}

} // namespace storm::ui
