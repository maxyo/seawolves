// The icon atlas of a UI pack, baked on the developer's machine (app/mobile-ui/design/bake_icons.py runs it):
//
//   storm_ui_icon_bake --in symbols.json --out DIR
//
// symbols.json: {"digest", "defs", "symbols": {id: {svg, deps?, acc?}}} as the design build writes it. Every icon is
// rasterised with lunasvg at a fixed chain of sizes (kLevels, px); the runtime (src/icons.cpp) draws the smallest
// level at least as large as the icon and scales it down, so nothing is rasterised on the phone. Writes DIR/atlas.json
// (the pages, the cells as [x, y, page], the levels and the digest the design build checks) and the pages:
//
//   coverage-N.png  grey + alpha (RG8 at run time), drawn by the coverage shader as (colour.rgb * R, colour.a * G):
//                   one-colour glyphs with dark shading: R = how much of the colour shows (the shading lowers it),
//                   G = coverage.
//   mask-N.png      grey (R8), drawn by the mask shader as colour * R: what needs one channel: glyphs without dark
//                   shading (R = coverage); their white shading (R = the light it adds, a second quad, untinted and
//                   of alpha 0); accent icons' occluders (glyph and accent coverage).
//   colour-N.png    RGBA (straight alpha in the file): full-colour icons (c-*, nation-*) and the accent layers.
//
// Shadows (the icon's coverage blurred, every channel, tinted by the shadow colour) lie on the page of what covers
// them: the glyph's page, the colour page, or the mask page beside an accent icon's occluder.
//
// No page is larger than kPageLimit in either dimension; an icon's cells of one kind share a page.
//
// A one-colour glyph is drawn as fill * T + W: lunasvg's source-over is linear in the fill colour, so two renders, in
// white and in black, give both exactly (T = white - black, W = black).
#include <lunasvg.h>
#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "plutovg-stb-image-write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{

using Json = nlohmann::json;

// Each level half the one above: an icon is scaled down by less than 2, which bilinear filtering does without losing
// thin strokes. 128 px: the pack's 36 dp icons at density 3.5 (a 1440p phone); larger ones are scaled up (the
// 52 dp helm knob at density 2.81 by 1.15).
constexpr int kLevels[] = {128, 64, 32, 16};
// Shadows: the coverage at these sizes blurred by kShadowSigma texels; at run time a shadow of blur s px under an
// icon of p px takes the level whose p / level is nearest to s.
constexpr int kShadowLevels[] = {24, 12};
constexpr float kShadowSigma = 1.0f;
constexpr int kShadowMargin = 4; // 3 sigma + 1
// The texture size GLES 3.0 guarantees (GL_MAX_TEXTURE_SIZE >= 2048), so every device samples every page; the runtime
// still refuses a pack whose page exceeds the device's limit (RenderBackend::RefusedTexture).
constexpr int kPageLimit = 2048;

[[noreturn]] void Fail(const std::string &why)
{
    std::fprintf(stderr, "storm_ui_icon_bake: %s\n", why.c_str());
    std::exit(1);
}

struct Pack
{
    std::string defs;
    std::map<std::string, Json> symbols;
};

void Collect(const Pack &pack, const std::string &id, std::vector<std::string> &order)
{
    if (std::find(order.begin(), order.end(), id) != order.end())
        return;
    const auto found = pack.symbols.find(id);
    if (found == pack.symbols.end())
        Fail("symbol '" + id + "' is named but not defined");
    order.push_back(id);
    for (const Json &dep : found->second.value("deps", Json::array()))
        Collect(pack, dep.get<std::string>(), order);
}

// Premultiplied RGBA of `use` (the <use> elements drawn) over the symbols they need.
std::vector<uint8_t> Render(const Pack &pack, const std::vector<std::string> &ids, const std::string &use, int px)
{
    std::vector<std::string> order;
    for (const std::string &id : ids)
        Collect(pack, id, order);
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                      "viewBox=\"0 0 48 48\"><defs>" +
                      pack.defs;
    for (const std::string &part : order)
        svg += pack.symbols.at(part).at("svg").get<std::string>();
    svg += "</defs>" + use + "</svg>";
    const auto document = lunasvg::Document::loadFromData(svg);
    if (!document)
        Fail("'" + ids.front() + "' does not parse");
    const lunasvg::Bitmap bitmap = document->renderToBitmap(px, px);
    if (bitmap.isNull())
        Fail("'" + ids.front() + "' does not render");
    std::vector<uint8_t> out(static_cast<size_t>(px) * px * 4);
    for (int y = 0; y < px; ++y)
    {
        const auto *row = reinterpret_cast<const uint32_t *>(bitmap.data() + static_cast<size_t>(y) * bitmap.stride());
        for (int x = 0; x < px; ++x)
        {
            uint8_t *pixel = &out[(static_cast<size_t>(y) * px + x) * 4];
            pixel[0] = uint8_t(row[x] >> 16), pixel[1] = uint8_t(row[x] >> 8), pixel[2] = uint8_t(row[x]);
            pixel[3] = uint8_t(row[x] >> 24);
        }
    }
    return out;
}

std::string Use(const std::string &id, const char *fill)
{
    return "<use href=\"#" + id + "\"" + (fill ? std::string(" fill=\"") + fill + "\"" : std::string()) + "/>";
}

// The glyph's alpha on a canvas `margin` larger on each side, blurred (separable Gaussian, zero outside).
std::vector<uint8_t> Blurred(const std::vector<uint8_t> &rgba, int px, int margin, float sigma)
{
    const int side = px + 2 * margin;
    std::vector<float> a(static_cast<size_t>(side) * side, 0.0f), b(a.size(), 0.0f);
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x)
            a[static_cast<size_t>(y + margin) * side + x + margin] = rgba[(static_cast<size_t>(y) * px + x) * 4 + 3];
    const int radius = static_cast<int>(std::ceil(3.0f * sigma));
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
        sum += kernel[i + radius] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
    for (float &k : kernel)
        k /= sum;
    const auto pass = [&](const std::vector<float> &from, std::vector<float> &to, bool vertical) {
        for (int line = 0; line < side; ++line)
            for (int at = 0; at < side; ++at)
            {
                float value = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                {
                    const int t = at + i;
                    if (t >= 0 && t < side)
                        value += kernel[i + radius] * (vertical ? from[static_cast<size_t>(t) * side + line]
                                                                : from[size_t(line) * side + t]);
                }
                (vertical ? to[static_cast<size_t>(at) * side + line] : to[size_t(line) * side + at]) = value;
            }
    };
    pass(a, b, false);
    pass(b, a, true);
    std::vector<uint8_t> out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = uint8_t(std::lround(std::clamp(a[i], 0.0f, 255.0f)));
    return out;
}

// A raster to place: `side` square, `channels` 1 (mask page), 2 (coverage page) or 4 (colour page, premultiplied), with
// a 1 px transparent border around it in the page so that filtering at its edge reads nothing of a neighbour.
struct Cell
{
    int side = 0, channels = 0;
    std::vector<uint8_t> pixels;
    int x = 0, y = 0, page = 0;
};

struct Page
{
    int width = 0, height = 0, channels = 0;
    std::vector<Cell *> cells;
};

// Shelves, tallest first, `width` wide; the height they take, rounded up to 4.
int Shelve(const std::vector<Cell *> &cells, int width)
{
    std::vector<Cell *> order = cells;
    std::stable_sort(order.begin(), order.end(), [](const Cell *a, const Cell *b) { return a->side > b->side; });
    int x = 0, y = 0, shelf = 0;
    for (Cell *cell : order)
    {
        const int extent = cell->side + 2;
        if (x + extent > width)
            x = 0, y += shelf, shelf = 0;
        cell->x = x + 1, cell->y = y + 1;
        x += extent;
        shelf = std::max(shelf, extent);
    }
    return (y + shelf + 3) / 4 * 4;
}

// The pages of one kind: the narrowest power-of-two width that holds the cells about square, at most kPageLimit, and
// as many pages as that takes, an icon's cells (a group) on one page.
std::vector<Page> Place(int channels, const std::vector<std::vector<Cell *>> &groups)
{
    size_t area = 0;
    for (const auto &group : groups)
        for (const Cell *cell : group)
            area += static_cast<size_t>(cell->side + 2) * (cell->side + 2);
    int width = 128;
    while (width < kPageLimit && static_cast<size_t>(width) * width < area * 5 / 4)
        width *= 2;
    std::vector<Page> pages;
    Page page{width, 0, channels, {}};
    for (const auto &group : groups)
    {
        std::vector<Cell *> more = page.cells;
        more.insert(more.end(), group.begin(), group.end());
        if (Shelve(more, width) <= kPageLimit)
        {
            page.cells = std::move(more);
            continue;
        }
        if (page.cells.empty())
            Fail("an icon's cells do not fit one " + std::to_string(kPageLimit) + " px page");
        pages.push_back(std::move(page));
        page = Page{width, 0, channels, group};
    }
    if (!page.cells.empty())
        pages.push_back(std::move(page));
    for (Page &placed : pages) // the last placement of each page's cells is the one written
        placed.height = Shelve(placed.cells, width);
    return pages;
}

void Write(const Page &page, const std::string &path)
{
    std::vector<uint8_t> image(static_cast<size_t>(page.width) * page.height * page.channels, 0);
    for (const Cell *cell : page.cells)
        for (int y = 0; y < cell->side; ++y)
            for (int x = 0; x < cell->side; ++x)
            {
                const uint8_t *from = &cell->pixels[(static_cast<size_t>(y) * cell->side + x) * cell->channels];
                uint8_t *to = &image[((static_cast<size_t>(cell->y) + y) * page.width + cell->x + x) * page.channels];
                std::copy(from, from + page.channels, to);
                if (page.channels == 4 && to[3] > 0) // the file holds straight alpha, as PNG means it
                    for (int c = 0; c < 3; ++c)
                        to[c] = uint8_t(std::min(255L, std::lround(to[c] * 255.0 / to[3])));
            }
    if (!stbi_write_png(path.c_str(), page.width, page.height, page.channels, image.data(), page.width * page.channels))
        Fail("cannot write " + path);
}

} // namespace

int main(int argc, char **argv)
{
    std::string in, out;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        if (flag == "--in")
            in = argv[i + 1];
        else if (flag == "--out")
            out = argv[i + 1];
        else
            Fail("unknown argument " + flag);
    }
    if (in.empty() || out.empty())
        Fail("usage: storm_ui_icon_bake --in symbols.json --out DIR");
    std::ifstream file(in);
    const Json source = Json::parse(file);
    Pack pack;
    pack.defs = source.value("defs", std::string());
    for (const auto &item : source.at("symbols").items())
        pack.symbols[item.key()] = item.value();

    // The cells of each page kind, a group per icon.
    struct Kind
    {
        const char *name;
        int channels;
        std::vector<std::vector<Cell *>> groups;
    };
    Kind mask{"mask", 1, {}}, coverage{"coverage", 2, {}}, colour{"colour", 4, {}};
    std::vector<std::unique_ptr<Cell>> cells;
    const auto add = [&](Kind &kind, int side, std::vector<uint8_t> pixels) {
        auto cell = std::make_unique<Cell>();
        cell->side = side, cell->channels = kind.channels, cell->pixels = std::move(pixels);
        kind.groups.back().push_back(cell.get());
        cells.push_back(std::move(cell));
        return cells.back().get();
    };
    std::map<std::string, std::map<std::string, std::vector<Cell *>>> table; // icon -> part -> cell per level

    for (const auto &[id, symbol] : pack.symbols)
    {
        if (id.size() > 4 && id.compare(id.size() - 4, 4, "-acc") == 0)
            continue;
        const bool full_colour = id.rfind("c-", 0) == 0 || id.rfind("i-nation-", 0) == 0;
        const bool accent = !full_colour && symbol.value("acc", false);
        auto &parts = table[id];
        mask.groups.emplace_back(), coverage.groups.emplace_back(), colour.groups.emplace_back();
        bool lit = false, shaded = false;
        std::vector<std::vector<uint8_t>> glyphs, light;
        for (const int px : kLevels)
        {
            if (full_colour)
            {
                parts["colour"].push_back(add(colour, px, Render(pack, {id}, Use(id, nullptr), px)));
                continue;
            }
            const std::vector<uint8_t> white = Render(pack, {id}, Use(id, "#ffffff"), px);
            const std::vector<uint8_t> black = Render(pack, {id}, Use(id, "#000000"), px);
            std::vector<uint8_t> glyph(static_cast<size_t>(px) * px * 2), shine(static_cast<size_t>(px) * px);
            for (size_t i = 0; i < static_cast<size_t>(px) * px; ++i)
            {
                const uint8_t *w = &white[i * 4], *b = &black[i * 4];
                if (std::abs(b[0] - b[1]) > 1 || std::abs(b[0] - b[2]) > 1 || std::abs(w[3] - b[3]) > 1)
                    Fail("'" + id + "' is a one-colour icon with a coloured layer: name it c-* or give it an accent");
                glyph[i * 2] = uint8_t(std::max(0, w[0] - b[0]));
                glyph[i * 2 + 1] = w[3];
                shine[i] = b[0];
                lit = lit || b[0] > 0;
                shaded = shaded || glyph[i * 2] != glyph[i * 2 + 1];
            }
            glyphs.push_back(std::move(glyph));
            light.push_back(std::move(shine));
            if (accent)
                parts["accent"].push_back(add(colour, px, Render(pack, {id + "-acc"}, Use(id + "-acc", nullptr), px)));
        }
        for (size_t level = 0; level < glyphs.size(); ++level)
        {
            if (shaded)
            {
                parts["glyph"].push_back(add(coverage, kLevels[level], std::move(glyphs[level])));
                continue;
            }
            std::vector<uint8_t> alpha(glyphs[level].size() / 2); // T = G: coverage alone
            for (size_t i = 0; i < alpha.size(); ++i)
                alpha[i] = glyphs[level][i * 2 + 1];
            parts["glyph"].push_back(add(mask, kLevels[level], std::move(alpha)));
        }
        if (lit)
            for (size_t level = 0; level < light.size(); ++level)
                parts["light"].push_back(add(mask, kLevels[level], std::move(light[level])));
        // What casts the shadow: the whole icon, its accent included.
        const std::vector<std::string> caster =
            accent ? std::vector<std::string>{id, id + "-acc"} : std::vector<std::string>{id};
        const std::string use =
            full_colour ? Use(id, nullptr) : Use(id, "#ffffff") + (accent ? Use(id + "-acc", nullptr) : "");
        // A faded icon's shadow is cut by what covers it (src/icons.cpp), read from its page: the glyph cell itself,
        // the colour cell, or for an accent icon an occluder cell of glyph and accent together.
        Kind &shadows = full_colour ? colour : shaded && !accent ? coverage : mask;
        if (accent)
            for (const int px : kLevels)
            {
                const std::vector<uint8_t> rgba = Render(pack, caster, use, px);
                std::vector<uint8_t> alpha(static_cast<size_t>(px) * px);
                for (size_t i = 0; i < alpha.size(); ++i)
                    alpha[i] = rgba[i * 4 + 3];
                parts["occluder"].push_back(add(mask, px, std::move(alpha)));
            }
        for (const int px : kShadowLevels)
        {
            const std::vector<uint8_t> alpha = Blurred(Render(pack, caster, use, px), px, kShadowMargin, kShadowSigma);
            std::vector<uint8_t> shadow(alpha.size() * static_cast<size_t>(shadows.channels)); // every channel S
            for (size_t i = 0; i < shadow.size(); ++i)
                shadow[i] = alpha[i / static_cast<size_t>(shadows.channels)];
            parts["shadow"].push_back(add(shadows, px + 2 * kShadowMargin, std::move(shadow)));
        }
    }

    Json atlas = {{"digest", source.at("digest")},
                  {"levels", kLevels},
                  {"shadow", {{"levels", kShadowLevels}, {"sigma", kShadowSigma}, {"margin", kShadowMargin}}},
                  {"icons", Json::object()}};
    Json listed = Json::array();
    for (Kind *kind : {&mask, &coverage, &colour})
    {
        kind->groups.erase(std::remove_if(kind->groups.begin(), kind->groups.end(),
                                          [](const std::vector<Cell *> &group) { return group.empty(); }),
                           kind->groups.end());
        int number = 0;
        for (Page &page : Place(kind->channels, kind->groups))
        {
            const std::string name = std::string(kind->name) + "-" + std::to_string(++number) + ".png";
            for (Cell *cell : page.cells)
                cell->page = static_cast<int>(listed.size());
            Write(page, out + "/" + name);
            listed.push_back({{"file", name}, {"size", {page.width, page.height}}, {"channels", page.channels}});
            std::printf("%s: %dx%d, %d channels, %zu cells\n", name.c_str(), page.width, page.height, page.channels,
                        page.cells.size());
        }
    }
    atlas["pages"] = listed;
    for (const auto &[id, parts] : table)
    {
        Json entry = Json::object();
        for (const auto &[part, list] : parts)
            for (const Cell *cell : list)
                entry[part].push_back({cell->x, cell->y, cell->page});
        atlas["icons"][id] = entry;
    }
    std::ofstream(out + "/atlas.json") << atlas.dump() << "\n";
    return 0;
}
