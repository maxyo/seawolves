#include "texture_file.hpp"

#include "mips.hpp"

#include <bx/error.h>
#include <format>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace storm::texture
{
namespace
{
constexpr char kDdsMagic[4] = {'D', 'D', 'S', ' '};
constexpr char kTxxMagic[8] = {'T', 'X', 'X', ' ', '1', '.', '0', ' '};
// "TXX 1.0 ", then nine int32: [1, format, width, height, 1, levels, row pitch, bits per pixel, 0], then the levels.
constexpr size_t kTxxHeaderBytes = sizeof(kTxxMagic) + 9 * sizeof(int32_t);

} // namespace

TextureContainer DetectTextureContainer(std::span<const uint8_t> head)
{
    if (head.size() >= sizeof(kDdsMagic) && std::memcmp(head.data(), kDdsMagic, sizeof(kDdsMagic)) == 0)
        return TextureContainer::Dds;
    if (head.size() >= sizeof(kTxxMagic) && std::memcmp(head.data(), kTxxMagic, sizeof(kTxxMagic)) == 0)
        return TextureContainer::Txx;
    return TextureContainer::Tx;
}

std::string TextureFilePath(std::string_view name)
{
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string path = lower.starts_with("resource\\textures\\") ? "" : "resource\\textures\\";
    path += name;
    if (!lower.ends_with(".tx") && !lower.ends_with(".dds") && !lower.ends_with(".txx"))
        path += ".tx";
    const auto separator = [](char c) { return c == '\\' || c == '/'; };
    std::string file;
    for (const char c : path)
        if (!separator(c) || file.empty() || !separator(file.back()))
            file.push_back(c);
    return file;
}

namespace
{
std::optional<std::string> ResolveInFolder(std::string_view name,
                                           const std::function<bool(const std::string &)> &exists)
{
    std::string file = TextureFilePath(name);
    if (exists(file))
        return file;
    std::string base(name);
    const auto strip = [&base](std::string_view extension) {
        const bool has = base.size() > extension.size() &&
                         std::equal(extension.rbegin(), extension.rend(), base.rbegin(),
                                    [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
        if (has)
            base.resize(base.size() - extension.size());
        return has;
    };
    strip(".tx");
    for (const std::string_view image : {".png", ".tga", ".bmp", ".jpg", ".dds", ".txx"})
        if (strip(image))
            break;
    // .dds first, as Caribbean Legend's own engine tries it after the named file; the others for the names it would
    // not find (a .png name shipped as .tga.tx).
    for (const std::string_view extension : {".dds", ".tga.tx", ".tx", ".txx"})
        if (file = TextureFilePath(base + std::string(extension)); exists(file))
            return file;
    return std::nullopt;
}
} // namespace

std::optional<std::string> ResolveTextureFile(std::string_view name,
                                              const std::function<bool(const std::string &)> &exists)
{
    // Caribbean Legend's models name maps in folders that it ships in a folder above ("buildings\x_AO.dds" for the
    // root's x_AO.tga.tx): each folder up to resource\textures is tried in turn.
    std::string path(name);
    while (true)
    {
        if (std::optional<std::string> file = ResolveInFolder(path, exists))
            return file;
        const size_t file_start = path.find_last_of("\\/");
        if (file_start == std::string::npos)
            return std::nullopt;
        std::string folder = path.substr(0, file_start);
        std::ranges::transform(folder, folder.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (folder == "resource\\textures" || folder == "resource/textures")
            return std::nullopt;
        const size_t folder_start = folder.find_last_of("\\/");
        path =
            (folder_start == std::string::npos ? "" : path.substr(0, folder_start + 1)) + path.substr(file_start + 1);
    }
}

std::span<const uint8_t> TextureFile::level(uint32_t face, uint32_t level) const
{
    size_t offset = payload_offset + face * (dxt_payload_size(layout) / layout.faces);
    for (uint32_t l = 0; l < level; ++l)
        offset += dxt_level_size(layout.format, layout.level_width(l), layout.level_height(l));
    return {bytes.data() + offset,
            dxt_level_size(layout.format, layout.level_width(level), layout.level_height(level))};
}

std::optional<TextureFileProblem> ParseDdsTexture(std::vector<uint8_t> bytes, TextureFile &out)
{
    if (DetectTextureContainer(bytes) != TextureContainer::Dds)
        return TextureFileProblem{"no DDS magic"};
    if (bytes.size() > UINT32_MAX)
        return TextureFileProblem{std::format("{} bytes: larger than bimg reads", bytes.size())};
    bimg::ImageContainer image{};
    bx::Error error;
    if (!bimg::imageParse(image, bytes.data(), static_cast<uint32_t>(bytes.size()), &error) || !error.isOk())
        return TextureFileProblem{
            std::format("bimg cannot parse the header: {}",
                        std::string_view(error.getMessage().getCPtr(), error.getMessage().getLength()))};
    if (image.m_depth > 1)
        return TextureFileProblem{"a volume texture", true};
    if (image.m_numLayers > 1)
        return TextureFileProblem{std::format("a texture array of {} layers", image.m_numLayers), true};
    TextureFile file;
    switch (image.m_format)
    {
    case bimg::TextureFormat::BC1:
        file.layout.format = DxtFormat::Bc1;
        break;
    case bimg::TextureFormat::BC2:
        file.layout.format = DxtFormat::Bc2;
        break;
    case bimg::TextureFormat::BC3:
        file.layout.format = DxtFormat::Bc3;
        break;
    default:
        return TextureFileProblem{
            std::format("format {}: only BC1, BC2 and BC3 are read", bimg::getName(image.m_format)), true};
    }
    file.layout.width = image.m_width;
    file.layout.height = image.m_height;
    file.layout.levels = image.m_numMips;
    file.layout.faces = image.m_cubeMap ? 6 : 1;
    if (file.layout.width == 0 || file.layout.height == 0 || file.layout.levels == 0)
        return TextureFileProblem{
            std::format("{}x{} with {} levels", file.layout.width, file.layout.height, file.layout.levels)};
    if (image.m_cubeMap && file.layout.width != file.layout.height)
        return TextureFileProblem{std::format("a cube map of {}x{} faces", file.layout.width, file.layout.height)};
    file.source = std::format("DDS {}{}{}", bimg::getName(image.m_format), image.m_srgb ? " sRGB" : "",
                              image.m_cubeMap ? " cube" : "");
    file.payload_offset = image.m_offset;
    if (const uint64_t end = uint64_t(file.payload_offset) + dxt_payload_size(file.layout); end > bytes.size())
        return TextureFileProblem{std::format("the levels need {} bytes, the file has {}", end, bytes.size())};
    file.bytes = std::move(bytes);
    out = std::move(file);
    return std::nullopt;
}

std::optional<TextureFileProblem> ParseTxxTexture(std::vector<uint8_t> bytes, TextureFile &out)
{
    if (bytes.size() < kTxxHeaderBytes || DetectTextureContainer(bytes) != TextureContainer::Txx)
        return TextureFileProblem{"no TXX 1.0 header"};
    int32_t field[9];
    std::memcpy(field, bytes.data() + sizeof(kTxxMagic), sizeof(field));
    const int32_t format = field[1], width = field[2], height = field[3], levels = field[5], pitch = field[6],
                  bits = field[7];
    if (field[0] != 1 || field[4] != 1 || field[8] != 0)
        return TextureFileProblem{
            std::format("header fields {}, {}, {} where 1, 1, 0 are known", field[0], field[4], field[8])};
    // The formats seen in files, by their bits per pixel: 9 BC1 (4), 11 BC3 (8).
    TextureFile file;
    if (format == 9 && bits == 4)
        file.layout.format = DxtFormat::Bc1;
    else if (format == 11 && bits == 8)
        file.layout.format = DxtFormat::Bc3;
    else
        return TextureFileProblem{
            std::format("format {} with {} bits per pixel: only 9 (BC1, 4) and 11 (BC3, 8) are read", format, bits),
            true};
    if (width <= 0 || height <= 0 || levels <= 0 ||
        static_cast<uint32_t>(levels) > full_mip_levels(static_cast<uint32_t>(width), static_cast<uint32_t>(height)))
        return TextureFileProblem{std::format("{}x{} with {} levels", width, height, levels)};
    if (int64_t(pitch) * 8 != int64_t(width) * bits)
        return TextureFileProblem{std::format("row pitch {} for {} texels of {} bits", pitch, width, bits)};
    file.layout.width = static_cast<uint32_t>(width);
    file.layout.height = static_cast<uint32_t>(height);
    file.layout.levels = static_cast<uint32_t>(levels);
    file.payload_offset = kTxxHeaderBytes;
    const uint64_t expected = kTxxHeaderBytes + dxt_payload_size(file.layout);
    if (expected != bytes.size())
        return TextureFileProblem{std::format("the mip chain needs {} bytes, the file has {}", expected, bytes.size())};
    file.source = std::format("TXX {}", file.layout.format == DxtFormat::Bc1 ? "BC1" : "BC3");
    file.bytes = std::move(bytes);
    out = std::move(file);
    return std::nullopt;
}
} // namespace storm::texture
