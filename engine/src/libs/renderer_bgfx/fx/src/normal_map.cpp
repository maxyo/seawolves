#include "storm/fx/normal_map.hpp"

#include "image_field.hpp"
#include "name_rules.hpp"
#include "storm/render/log.hpp"
#include "fnv_hash.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace storm::fx
{
namespace
{
using namespace img;

const char *const kClassNames[] = {"none",    "height map", "stone", "wood",     "building",
                                   "plaster", "metal",      "prop",  "character"};

} // namespace

const char *ReliefClassName(ReliefClass relief)
{
    return kClassNames[static_cast<int>(relief)];
}

NormalMapOptions HeightMapOptions()
{
    NormalMapOptions options;
    options.strength = 2.0f;
    return options;
}

uint64_t NormalMapCacheKey(const NormalMapOptions &options)
{
    uint32_t strength;
    std::memcpy(&strength, &options.strength, sizeof(strength));
    uint64_t key = storm::hash::kFnvOffset;
    for (uint64_t part : {uint64_t{kNormalMapVersion}, uint64_t{strength}, uint64_t{options.wrap},
                          static_cast<uint64_t>(options.pre_blur), static_cast<uint64_t>(options.max_size)})
        key = storm::hash::fnvRound(key, part);
    return key;
}

void NormalMapSize(int width, int height, const NormalMapOptions &options, int &out_width, int &out_height)
{
    out_width = width;
    out_height = height;
    while (std::max(out_width, out_height) > std::max(options.max_size, 1))
    {
        out_width = std::max(out_width / 2, 1);
        out_height = std::max(out_height / 2, 1);
    }
}

size_t NormalMapLevelBytes(NormalMapFormat format, int width, int height)
{
    const size_t blocks = static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4),
                 texels = static_cast<size_t>(width) * height;
    switch (format)
    {
    case NormalMapFormat::Rg8:
        return texels * 2;
    case NormalMapFormat::EacRg11:
        return blocks * 16;
    }
    return 0;
}

size_t NormalMapBytes(int width, int height, NormalMapFormat format)
{
    size_t bytes = 0;
    for (;;)
    {
        bytes += NormalMapLevelBytes(format, width, height);
        if (width == 1 && height == 1)
            return bytes;
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
    }
}

NormalMap DeriveNormalMap(std::span<const uint8_t> rgba, int width, int height, const NormalMapOptions &options)
{
    NormalMap map;
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return map;
    Field field{width, height, options.wrap, {}};
    field.values.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < field.values.size(); ++i)
        field.values[i] = rgba[i * 4] / 255.0f;
    int top_width, top_height;
    NormalMapSize(width, height, options, top_width, top_height);
    // The strength is given at kNormalMapReferenceSize: a map derived smaller (a lower preset) has the same relief,
    // only less detail.
    int reference_width, reference_height;
    NormalMapOptions reference = options;
    reference.max_size = kNormalMapReferenceSize;
    NormalMapSize(width, height, reference, reference_width, reference_height);
    const float texel_ratio = static_cast<float>(top_width) / static_cast<float>(reference_width);
    while (field.width != top_width || field.height != top_height)
        field = Halve(field);
    for (int pass = 0; pass < options.pre_blur; ++pass)
        field = Blur(field);
    map.width = field.width;
    map.height = field.height;
    map.rg.reserve(NormalMapBytes(map.width, map.height));

    // Slope scale of the top mip; each smaller mip has texels twice as large, so the same world slope is
    // half as many height units per texel.
    float scale = options.strength * texel_ratio;
    for (;;)
    {
        ++map.mip_count;
        const std::vector<int> xs = field.Indices(field.width, 1), ys = field.Indices(field.height, 1);
        const float *v = field.values.data();
        const size_t w = static_cast<size_t>(field.width);
        for (int y = 0; y < field.height; ++y)
        {
            const float *above = v + ys[static_cast<size_t>(y)] * w, *row = v + ys[static_cast<size_t>(y) + 1] * w,
                        *below = v + ys[static_cast<size_t>(y) + 2] * w;
            for (int x = 0; x < field.width; ++x)
            {
                // Scharr: [-1 0 1] derivative, [3 10 3] / 16 smoothing across.
                const int l = xs[static_cast<size_t>(x)], c = xs[static_cast<size_t>(x) + 1],
                          r = xs[static_cast<size_t>(x) + 2];
                const float du =
                    (3.0f * (above[r] - above[l]) + 10.0f * (row[r] - row[l]) + 3.0f * (below[r] - below[l])) / 32.0f;
                const float dv =
                    (3.0f * (below[l] - above[l]) + 10.0f * (below[c] - above[c]) + 3.0f * (below[r] - above[r])) /
                    32.0f;
                float nx = -du * scale, ny = -dv * scale;
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
                nx *= inv;
                ny *= inv;
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * nx, 0.0f, 1.0f) * 255.0f)));
                map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * ny, 0.0f, 1.0f) * 255.0f)));
            }
        }
        if (field.width == 1 && field.height == 1)
            break;
        field = Halve(field);
        scale *= 0.5f;
    }
    return map;
}

void DropTopMips(NormalMap &map, int max_size)
{
    size_t offset = 0;
    while (map.mip_count > 1 && std::max(map.width, map.height) > max_size)
    {
        offset += NormalMapLevelBytes(map.format, map.width, map.height);
        map.width = std::max(map.width / 2, 1);
        map.height = std::max(map.height / 2, 1);
        map.depth *= 0.5f;
        --map.mip_count;
    }
    map.rg.erase(map.rg.begin(), map.rg.begin() + static_cast<std::ptrdiff_t>(offset));
    if (!map.material.empty())
        map.material.erase(map.material.begin(), map.material.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::string NormalizeTexturePath(std::string_view path)
{
    std::string out;
    for (char c : path)
        if (!std::isspace(static_cast<unsigned char>(c)) || !out.empty())
            out.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // A leading separator ("\\WorldMap\\sea\\ani\\wave_0000": how the world map names its textures) is no part of the
    // path: left in, no folder rule matched and the world map's sea frames and clouds fell to the last rule (a prop).
    out.erase(0, out.find_first_not_of('/') == std::string::npos ? out.size() : out.find_first_not_of('/'));
    for (std::string_view prefix : {"resource/textures/", "textures/"})
        if (out.compare(0, prefix.size(), prefix) == 0)
            out.erase(0, prefix.size());
    for (std::string_view extension : {".tx", ".tga"})
        if (out.size() > extension.size() &&
            out.compare(out.size() - extension.size(), extension.size(), extension) == 0)
            out.resize(out.size() - extension.size());
    return out;
}

bool NameSuggestsHeightMap(std::string_view normalized_path)
{
    const size_t slash = normalized_path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? normalized_path : normalized_path.substr(slash + 1);
    return name.find("bump") != std::string_view::npos;
}

bool IsGrayscale(std::span<const uint8_t> rgba)
{
    if (rgba.size() < 4)
        return false;
    // DXT stores colours as 5:6:5, so a grey image comes back with channel differences of a few steps.
    size_t far_off = 0;
    uint64_t total = 0;
    const size_t pixels = rgba.size() / 4;
    for (size_t i = 0; i < pixels; ++i)
    {
        const int r = rgba[4 * i], g = rgba[4 * i + 1], b = rgba[4 * i + 2];
        const int spread = std::max({r, g, b}) - std::min({r, g, b});
        total += static_cast<uint64_t>(spread);
        far_off += spread > 24;
    }
    return total <= pixels * 8 && far_off * 100 <= pixels;
}

ReliefVerdict ClassifyTextureName(std::string_view path)
{
    for (const rules::Rule &rule : rules::kRules)
        if (rule.relief && rules::Matches(path, rule.folder, rule.match, rule.pattern) &&
            (!rule.accept || rule.accept(path)))
            return {*rule.relief, rule.text, rule.match == rules::Match::Folder};
    return {};
}

ReliefVerdict ReliefClassifier::Classify(std::string_view path, std::span<const uint8_t> rgba)
{
    const std::string name = NormalizeTexturePath(path);
    ReliefVerdict verdict = ClassifyTextureName(name);
    // A grey "bump" texture is a height map, except in the folders without relief (the sea's animated bump frames).
    if (!(verdict.by_folder && verdict.relief == ReliefClass::None) && NameSuggestsHeightMap(name) && IsGrayscale(rgba))
        verdict = {ReliefClass::HeightMap, "bump + grey", false};
    if (std::find(logged_.begin(), logged_.end(), name) == logged_.end())
    {
        logged_.push_back(name);
        if (verdict.rule)
            render::Log().debug("fx: texture {}: relief {} (rule {})", name, ReliefClassName(verdict.relief),
                                verdict.rule);
        else
            render::Log().debug("fx: texture {}: no relief (no rule knows it)", name);
    }
    return verdict;
}

} // namespace storm::fx
