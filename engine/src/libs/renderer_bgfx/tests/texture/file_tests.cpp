// The texture files beside .tx (texture_file.hpp) on synthetic DDS and TXX files, and the texture name -> file rule.
// --corpus: every DDS and TXX file of the installed game in $STORM_GAME_DIR (77, not run, without it); a damaged file
// fails, a variant the renderer does not read is listed; every texture name resolves to the file the name rule gives.
#include "texture_file.hpp"
#include "../check.hpp"

#include <algorithm>
#include <set>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string_view>

using namespace storm::texture;

namespace
{
void put32(std::vector<uint8_t> &out, size_t offset, uint32_t value)
{
    std::memcpy(out.data() + offset, &value, sizeof(value));
}

uint32_t fourcc(const char *code)
{
    return uint32_t(uint8_t(code[0])) | uint32_t(uint8_t(code[1])) << 8 | uint32_t(uint8_t(code[2])) << 16 |
           uint32_t(uint8_t(code[3])) << 24;
}

struct DdsSpec
{
    uint32_t width = 8, height = 8, levels = 1;
    const char *fourcc = "DXT1";
    uint32_t dxgi = 0; // with fourcc "DX10"
    bool cube = false;
    uint32_t block_bytes = 8; // 0: uncompressed 32-bit A8R8G8B8
};

// A DDS file whose payload bytes count up, so that every level is told apart by its first byte.
std::vector<uint8_t> make_dds(const DdsSpec &spec)
{
    const bool dx10 = spec.dxgi != 0;
    std::vector<uint8_t> out(4 + 124 + (dx10 ? 20 : 0));
    std::memcpy(out.data(), "DDS ", 4);
    put32(out, 4, 124);
    put32(out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000); // caps, height, width, pixel format, mip count
    put32(out, 12, spec.height);
    put32(out, 16, spec.width);
    put32(out, 28, spec.levels);
    put32(out, 76, 32);
    if (spec.block_bytes)
    {
        put32(out, 80, 0x4); // DDPF_FOURCC
        put32(out, 84, fourcc(spec.fourcc));
    }
    else
    {
        put32(out, 80, 0x41); // DDPF_RGB | DDPF_ALPHAPIXELS
        put32(out, 88, 32);
        put32(out, 92, 0x00ff0000);
        put32(out, 96, 0x0000ff00);
        put32(out, 100, 0x000000ff);
        put32(out, 104, 0xff000000);
    }
    put32(out, 108, 0x1000 | (spec.levels > 1 ? 0x400008 : 0)); // texture, mipmap | complex
    put32(out, 112, spec.cube ? 0x200 | 0xfc00 : 0);            // cube map, all six faces
    if (dx10)
    {
        put32(out, 128, spec.dxgi);
        put32(out, 132, 3); // TEXTURE2D
        put32(out, 136, spec.cube ? 4 : 0);
        put32(out, 140, 1);
    }
    uint8_t counter = 0;
    for (uint32_t face = 0; face < (spec.cube ? 6u : 1u); ++face)
        for (uint32_t level = 0; level < spec.levels; ++level)
        {
            const uint32_t w = std::max(1u, spec.width >> level), h = std::max(1u, spec.height >> level);
            const size_t size =
                spec.block_bytes ? size_t((w + 3) / 4) * ((h + 3) / 4) * spec.block_bytes : size_t(w) * h * 4;
            for (size_t i = 0; i < size; ++i)
                out.push_back(counter++);
        }
    return out;
}

std::vector<uint8_t> make_txx(int32_t format, int32_t size, int32_t levels, int32_t bits, int32_t block_bytes)
{
    std::vector<uint8_t> out(44);
    std::memcpy(out.data(), "TXX 1.0 ", 8);
    const int32_t fields[9] = {1, format, size, size, 1, levels, size * bits / 8, bits, 0};
    std::memcpy(out.data() + 8, fields, sizeof(fields));
    for (int32_t level = 0; level < levels; ++level)
    {
        const int32_t blocks = (std::max(1, size >> level) + 3) / 4;
        out.resize(out.size() + size_t(blocks) * blocks * block_bytes, uint8_t(level + 1));
    }
    return out;
}

bool unsupported(const std::optional<TextureFileProblem> &problem)
{
    return problem && problem->unsupported;
}
bool damaged(const std::optional<TextureFileProblem> &problem)
{
    return problem && !problem->unsupported;
}

void test_container()
{
    const std::vector<uint8_t> tx(24, 0); // a .tx header starts with its flags
    CHECK(DetectTextureContainer(tx) == TextureContainer::Tx);
    CHECK(DetectTextureContainer(make_dds({})) == TextureContainer::Dds);
    CHECK(DetectTextureContainer(make_txx(9, 4, 1, 4, 8)) == TextureContainer::Txx);
}

void test_dds()
{
    TextureFile file;
    const std::vector<uint8_t> bytes = make_dds({.width = 16, .height = 8, .levels = 5});
    CHECK(!ParseDdsTexture(bytes, file));
    CHECK(file.layout.format == DxtFormat::Bc1 && file.layout.width == 16 && file.layout.height == 8 &&
          file.layout.levels == 5 && file.layout.faces == 1);
    // 16x8: 4x2 blocks, then 2x1, then one block for 4x2, 2x1 and 1x1.
    const size_t sizes[5] = {64, 16, 8, 8, 8};
    size_t offset = 128;
    for (uint32_t level = 0; level < 5; ++level)
    {
        const std::span<const uint8_t> data = file.level(0, level);
        CHECK(data.size() == sizes[level]);
        CHECK(data.data() == file.bytes.data() + offset && data[0] == bytes[offset]);
        offset += sizes[level];
    }
    CHECK(!ParseDdsTexture(make_dds({.fourcc = "DXT3", .block_bytes = 16}), file) &&
          file.layout.format == DxtFormat::Bc2);
    CHECK(!ParseDdsTexture(make_dds({.fourcc = "DXT5", .block_bytes = 16}), file) &&
          file.layout.format == DxtFormat::Bc3);
    // DX10 headers: BC1_UNORM 71, BC2_UNORM_SRGB 75, BC3_UNORM_SRGB 78 as a cube map with three levels.
    CHECK(!ParseDdsTexture(make_dds({.fourcc = "DX10", .dxgi = 71}), file) && file.layout.format == DxtFormat::Bc1);
    CHECK(!ParseDdsTexture(make_dds({.fourcc = "DX10", .dxgi = 75, .block_bytes = 16}), file) &&
          file.layout.format == DxtFormat::Bc2);
    CHECK(
        !ParseDdsTexture(make_dds({.levels = 3, .fourcc = "DX10", .dxgi = 78, .cube = true, .block_bytes = 16}), file));
    CHECK(file.layout.format == DxtFormat::Bc3 && file.layout.faces == 6 && file.layout.levels == 3);
    // Face 1 starts after the 4 + 1 + 1 blocks of face 0.
    CHECK(file.level(1, 0).data() == file.level(0, 0).data() + 6 * 16 &&
          file.level(5, 2).data() + 16 == file.bytes.data() + file.bytes.size());
}

void test_dds_refused()
{
    TextureFile file;
    std::vector<uint8_t> truncated = make_dds({.width = 64, .height = 64, .levels = 4});
    truncated.pop_back();
    CHECK(damaged(ParseDdsTexture(truncated, file)));
    CHECK(damaged(ParseDdsTexture({'D', 'D', 'S', ' ', 1, 2}, file)));
    CHECK(damaged(ParseDdsTexture(make_dds({.width = 8, .height = 4, .cube = true}), file)));
    CHECK(damaged(ParseDdsTexture(make_dds({.width = 0, .height = 4}), file)));
    // BC4 (DXGI 80) and uncompressed A8R8G8B8 are DDS the renderer does not read.
    CHECK(unsupported(ParseDdsTexture(make_dds({.fourcc = "DX10", .dxgi = 80}), file)));
    CHECK(unsupported(ParseDdsTexture(make_dds({.width = 4, .height = 2, .block_bytes = 0}), file)));
}

void test_txx()
{
    TextureFile file;
    const std::vector<uint8_t> bc3 = make_txx(11, 64, 7, 8, 16);
    CHECK(!ParseTxxTexture(bc3, file));
    CHECK(file.layout.format == DxtFormat::Bc3 && file.layout.width == 64 && file.layout.levels == 7);
    CHECK(file.level(0, 0).data() == file.bytes.data() + 44 && file.level(0, 0).size() == 16 * 16 * 16);
    CHECK(file.level(0, 6).size() == 16 && file.level(0, 6)[0] == 7);
    CHECK(!ParseTxxTexture(make_txx(9, 32, 6, 4, 8), file) && file.layout.format == DxtFormat::Bc1);

    std::vector<uint8_t> truncated = bc3;
    truncated.pop_back();
    CHECK(damaged(ParseTxxTexture(truncated, file)));
    std::vector<uint8_t> longer = bc3;
    longer.push_back(0);
    CHECK(damaged(ParseTxxTexture(longer, file)));
    std::vector<uint8_t> fields = bc3;
    put32(fields, 8, 2);
    CHECK(damaged(ParseTxxTexture(fields, file)));
    CHECK(unsupported(ParseTxxTexture(make_txx(9, 32, 6, 8, 8), file))); // BC1 is 4 bits per pixel
    CHECK(damaged(ParseTxxTexture(make_txx(9, 32, 7, 4, 8), file)));     // a level below 1x1
    std::vector<uint8_t> pitch = make_txx(9, 32, 6, 4, 8);
    put32(pitch, 8 + 6 * 4, 32);
    CHECK(damaged(ParseTxxTexture(pitch, file)));
    CHECK(unsupported(ParseTxxTexture(make_txx(5, 32, 1, 16, 512), file))); // a 16-bit format of unknown layout
}

void test_names()
{
    CHECK(TextureFilePath("LOCATIONS\\WALL.TGA") == "resource\\textures\\LOCATIONS\\WALL.TGA.tx");
    CHECK(TextureFilePath("weather/sun/flare.tga.tx") == "resource\\textures\\weather/sun/flare.tga.tx");
    CHECK(TextureFilePath("Resource\\Textures\\\\Props//Box.dds") == "Resource\\Textures\\Props/Box.dds");
    CHECK(TextureFilePath("Particles_New.TXX") == "resource\\textures\\Particles_New.TXX");
    CHECK(TextureFilePath("ships\\deck.dds.bak") == "resource\\textures\\ships\\deck.dds.bak.tx");

    // The file a name resolves to among the files that exist (lower case here, as a case-blind index answers).
    const std::set<std::string> files{"resource\\textures\\mainmenu\\plantscollection1.tga.tx",
                                      "resource\\textures\\props\\box.dds", "resource\\textures\\wall.tga.tx",
                                      "resource\\textures\\wall.dds"};
    const auto exists = [&files](const std::string &path) {
        std::string lower = path;
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return files.contains(lower);
    };
    const auto resolved = [&](const char *name) { return ResolveTextureFile(name, exists).value_or("none"); };
    CHECK(resolved("MainMenu\\plantscollection1.png") == "resource\\textures\\MainMenu\\plantscollection1.tga.tx");
    CHECK(resolved("Props\\Box.tga") == "resource\\textures\\Props\\Box.dds");
    CHECK(resolved("props\\box.tga.tx") == "resource\\textures\\props\\box.dds");
    // The named file wins over another that exists: what resolved before resolves the same.
    CHECK(resolved("WALL.TGA") == "resource\\textures\\WALL.TGA.tx");
    CHECK(resolved("wall.dds") == "resource\\textures\\wall.dds");
    CHECK(resolved("Props\\Crate.png") == "none");
    // Both under other extensions: the .dds, which the game's own engine tries first.
    CHECK(resolved("wall.png") == "resource\\textures\\wall.dds");
}

int corpus()
{
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::printf("NOT_RUN: set STORM_GAME_DIR to an installed game\n");
        return 77;
    }
    std::map<std::string, int> parsed, refused;
    int files = 0, bad = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        std::string extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!entry.is_regular_file() || (extension != ".dds" && extension != ".txx"))
            continue;
        ++files;
        std::vector<uint8_t> bytes(entry.file_size());
        std::ifstream(entry.path(), std::ios::binary)
            .read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        TextureFile file;
        const auto problem = DetectTextureContainer(bytes) == TextureContainer::Txx ? ParseTxxTexture(bytes, file)
                                                                                    : ParseDdsTexture(bytes, file);
        if (!problem)
            ++parsed[file.source];
        else if (problem->unsupported)
            ++refused[problem->reason];
        else
        {
            ++bad;
            std::printf("DAMAGED %s: %s\n", entry.path().c_str(), problem->reason.c_str());
        }
    }
    for (const auto &[source, count] : parsed)
        std::printf("  parsed     %6d  %s\n", count, source.c_str());
    for (const auto &[reason, count] : refused)
        std::printf("  not read   %6d  %s\n", count, reason.c_str());
    std::printf("%s: %d DDS and TXX files, %d damaged\n", root, files, bad);
    // Every texture file under resource\textures resolves from its own name to itself, and from the name its models
    // give (x.tga for x.tga.tx) to the file the name rule alone gives: the extensions the game ships take nothing
    // from a name that resolved before.
    std::set<std::string> index;
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        std::string relative = std::filesystem::relative(entry.path(), root).string();
        std::ranges::replace(relative, '/', '\\');
        std::string lower = relative;
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        index.insert(lower);
        if (lower.starts_with("resource\\textures\\") && (lower.ends_with(".tx") || lower.ends_with(".dds")))
            names.push_back(relative.substr(18));
    }
    const auto exists = [&index](const std::string &path) {
        std::string lower = path;
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return index.contains(lower);
    };
    int moved = 0;
    for (const std::string &name : names)
        for (const std::string &given : {name, name.ends_with(".tx") ? name.substr(0, name.size() - 3) : name})
            if (const auto file = ResolveTextureFile(given, exists); !file || *file != TextureFilePath(given))
            {
                ++moved;
                std::printf("RESOLVED ELSEWHERE %s: %s\n", given.c_str(), file ? file->c_str() : "none");
            }
    std::printf("%zu texture names, %d resolved otherwise than by the name rule\n", names.size(), moved);
    return bad || moved ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--corpus")
        return corpus();
    test_container();
    test_dds();
    test_dds_refused();
    test_txx();
    test_names();
    if (failures)
        std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
