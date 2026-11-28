// Texture files beside the game's .tx, told apart by their first bytes: DirectDraw Surface (parsed by bimg) and TXX
// (a "TXX 1.0 " header and a BC mip chain). Their BC1-3 blocks reach the renderer as D3D DXT1/3/5 levels, the same
// upload, transcode and cache as .tx textures.
#pragma once

#include "formats.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::texture
{
enum class TextureContainer : uint8_t
{
    Tx,  // the game's 24-byte header, no magic (tx_header.hpp)
    Dds, // "DDS "
    Txx, // "TXX 1.0 "
};
// The container of a file from its first bytes (at least 8 to recognise TXX).
TextureContainer DetectTextureContainer(std::span<const uint8_t> head);

// The file of a texture name (relative to the game, backslashes): under resource\textures, without doubled separators,
// with ".tx" appended unless the name already names a texture file (.tx, .dds, .txx).
std::string TextureFilePath(std::string_view name);
// The file of a texture name among the files that exist (`exists`: a path as TextureFilePath gives it): the named file,
// else the same base name (without its .png, .tga, .bmp, .jpg, .dds, .txx, .tx) as .dds, .tga.tx, .tx or .txx, the
// first that exists: a game's models may name x.png or x.tga where it ships x.tga.tx or x.dds; then the same in each
// folder above, up to resource\textures ("buildings\x_AO.dds" shipped as x_AO.tga.tx). nullopt: none exists.
std::optional<std::string> ResolveTextureFile(std::string_view name,
                                              const std::function<bool(const std::string &)> &exists);

// Every texture file of a game by base name, for the names ResolveTextureFile does not find: Caribbean Legend's models
// name textures without the folder the game keeps them in ("dirtK2.tga" shipped as buildings\dirtK2.dds, a ship's
// "Rig1.tga" as Ships\Other\Rig1.tga.tx).
class TextureTree
{
  public:
    // `files`: every file under resource\textures, as TextureFilePath names them.
    explicit TextureTree(const std::vector<std::string> &files);
    struct Found
    {
        std::optional<std::string> file;
        std::vector<std::string> folders; // the folders holding the name when more than one does (no file then)
    };
    // The file of `name` in the one folder that holds a file of its base name (without its folders and extensions),
    // chosen there as ResolveTextureFile chooses; none when no folder or several do.
    Found Find(std::string_view name) const;

  private:
    std::unordered_map<std::string, std::vector<std::string>> by_base_; // lower-case base name -> lower-case files
};

// A whole DDS or TXX file and where its levels are: faces one after another (DDS cube order +X, -X, +Y, -Y, +Z, -Z,
// which is D3DCUBEMAP_FACES order), each face's levels from the full size down, tightly packed whole blocks.
struct TextureFile
{
    Layout layout;
    size_t payload_offset = 0;
    std::string source; // what the file holds, for logs: "DDS BC3 sRGB cube", "TXX BC1"
    std::vector<uint8_t> bytes;

    std::span<const uint8_t> level(uint32_t face, uint32_t level) const;
};

// Why a file cannot be used: a variant the renderer does not read (any format other than BC1-3, volumes, arrays) or a
// damaged file.
struct TextureFileProblem
{
    std::string reason;
    bool unsupported = false;
};
// Each takes the whole file; on success `out` is filled.
std::optional<TextureFileProblem> ParseDdsTexture(std::vector<uint8_t> bytes, TextureFile &out);
std::optional<TextureFileProblem> ParseTxxTexture(std::vector<uint8_t> bytes, TextureFile &out);
} // namespace storm::texture
