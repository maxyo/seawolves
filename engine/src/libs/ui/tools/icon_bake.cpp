// The icon atlas of a UI pack, baked on the developer's machine (app/mobile-ui/design/bake_icons.py runs it):
//
//   storm_ui_icon_bake --in symbols.json --out DIR
//
// symbols.json: {"digest", "defs", "symbols": {id: {svg, deps?, acc?}}} as the design build writes it. Every icon is
// rasterised with lunasvg at a fixed chain of sizes (kLevels, px); the runtime (src/icons.cpp) draws the smallest
// level at least as large as the icon and scales it down, so nothing is rasterised on the phone. Writes DIR/atlas.json
// (the cells, the levels and the digest the design build checks) and two pages:
//
//   coverage.png  grey + alpha (RG8 at run time), drawn by the coverage shader as (colour.rgb * R, colour.a * G):
//                 one-colour glyphs: R = how much of the colour shows (the glyph's dark shading lowers it), G =
//                 coverage; their white shading: R = the light it adds, G = 0 (a second quad, untinted); shadows: the
//                 coverage blurred, R = G, tinted by the shadow colour.
//   colour.png    RGBA (straight alpha in the file): full-colour icons (c-*, nation-*) and the accent layers.
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
// GLES 3.0 guarantees 2048; every GLES 3.0 GPU family (Adreno 300+, Mali-T600+, PowerVR Series6+, Xclipse) samples
// 4096 or more, and the backend checks the device's limit.
constexpr int kPageWidth = 4096;

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

// A raster to place: `side` square, `channels` 2 (coverage page) or 4 (colour page, premultiplied), with a 1 px
// transparent border around it in the page so that filtering at its edge reads nothing of a neighbour.
struct Cell
{
    int side = 0, channels = 0;
    std::vector<uint8_t> pixels;
    int x = 0, y = 0;
};

struct Page
{
    int width = 0, height = 0, channels = 0;
    std::vector<Cell *> cells;
};

// Shelves, tallest first, on the narrowest power-of-two page that holds them about square.
void Place(Page &page)
{
    size_t area = 0;
    for (const Cell *cell : page.cells)
        area += static_cast<size_t>(cell->side + 2) * (cell->side + 2);
    page.width = 128;
    while (page.width < kPageWidth && static_cast<size_t>(page.width) * page.width < area * 5 / 4)
        page.width *= 2;
    std::vector<Cell *> order = page.cells;
    std::stable_sort(order.begin(), order.end(), [](const Cell *a, const Cell *b) { return a->side > b->side; });
    int x = 0, y = 0, shelf = 0;
    for (Cell *cell : order)
    {
        const int extent = cell->side + 2;
        if (x + extent > page.width)
            x = 0, y += shelf, shelf = 0;
        cell->x = x + 1, cell->y = y + 1;
        x += extent;
        shelf = std::max(shelf, extent);
    }
    page.height = (y + shelf + 3) / 4 * 4;
    if (page.height > kPageWidth)
        Fail("the icons do not fit one " + std::to_string(kPageWidth) + " px page");
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

    std::vector<std::unique_ptr<Cell>> cells;
    Page coverage, colour;
    coverage.channels = 2, colour.channels = 4;
    const auto add = [&](Page &page, int side, std::vector<uint8_t> pixels) {
        auto cell = std::make_unique<Cell>();
        cell->side = side, cell->channels = page.channels, cell->pixels = std::move(pixels);
        page.cells.push_back(cell.get());
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
        bool lit = false;
        std::vector<std::vector<uint8_t>> light;
        for (const int px : kLevels)
        {
            if (full_colour)
            {
                parts["colour"].push_back(add(colour, px, Render(pack, {id}, Use(id, nullptr), px)));
                continue;
            }
            const std::vector<uint8_t> white = Render(pack, {id}, Use(id, "#ffffff"), px);
            const std::vector<uint8_t> black = Render(pack, {id}, Use(id, "#000000"), px);
            std::vector<uint8_t> glyph(static_cast<size_t>(px) * px * 2), shine(glyph.size(), 0);
            for (size_t i = 0; i < static_cast<size_t>(px) * px; ++i)
            {
                const uint8_t *w = &white[i * 4], *b = &black[i * 4];
                if (std::abs(b[0] - b[1]) > 1 || std::abs(b[0] - b[2]) > 1 || std::abs(w[3] - b[3]) > 1)
                    Fail("'" + id + "' is a one-colour icon with a coloured layer: name it c-* or give it an accent");
                glyph[i * 2] = uint8_t(std::max(0, w[0] - b[0]));
                glyph[i * 2 + 1] = w[3];
                shine[i * 2] = b[0];
                lit = lit || b[0] > 0;
            }
            parts["glyph"].push_back(add(coverage, px, std::move(glyph)));
            light.push_back(std::move(shine));
            if (accent)
                parts["accent"].push_back(add(colour, px, Render(pack, {id + "-acc"}, Use(id + "-acc", nullptr), px)));
        }
        if (lit)
            for (size_t level = 0; level < light.size(); ++level)
                parts["light"].push_back(add(coverage, kLevels[level], std::move(light[level])));
        for (const int px : kShadowLevels)
        {
            // What casts the shadow: the whole icon, its accent included.
            const std::string use =
                full_colour ? Use(id, nullptr) : Use(id, "#ffffff") + (accent ? Use(id + "-acc", nullptr) : "");
            const std::vector<uint8_t> alpha =
                Blurred(Render(pack, accent ? std::vector<std::string>{id, id + "-acc"} : std::vector<std::string>{id},
                               use, px),
                        px, kShadowMargin, kShadowSigma);
            std::vector<uint8_t> shadow(alpha.size() * 2);
            for (size_t i = 0; i < alpha.size(); ++i)
                shadow[i * 2] = shadow[i * 2 + 1] = alpha[i];
            parts["shadow"].push_back(add(coverage, px + 2 * kShadowMargin, std::move(shadow)));
        }
    }

    Json atlas = {{"digest", source.at("digest")},
                  {"levels", kLevels},
                  {"shadow", {{"levels", kShadowLevels}, {"sigma", kShadowSigma}, {"margin", kShadowMargin}}},
                  {"icons", Json::object()}};
    std::vector<std::pair<Page *, std::string>> pages = {{&coverage, "coverage.png"}, {&colour, "colour.png"}};
    Json listed = Json::array();
    for (auto &[page, name] : pages)
    {
        if (page->cells.empty())
            continue;
        Place(*page);
        Write(*page, out + "/" + name);
        listed.push_back({{"file", name}, {"size", {page->width, page->height}}, {"channels", page->channels}});
        std::printf("%s: %dx%d, %d channels, %zu cells\n", name.c_str(), page->width, page->height, page->channels,
                    page->cells.size());
    }
    atlas["pages"] = listed;
    for (const auto &[id, parts] : table)
    {
        Json entry = Json::object();
        for (const auto &[part, list] : parts)
            for (const Cell *cell : list)
                entry[part].push_back({cell->x, cell->y});
        atlas["icons"][id] = entry;
    }
    std::ofstream(out + "/atlas.json") << atlas.dump() << "\n";
    return 0;
}
