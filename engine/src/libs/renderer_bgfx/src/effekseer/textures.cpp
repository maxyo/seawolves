#include "effekseer/textures.hpp"

#include "shim/texture_tier.hpp"
#include "storm/render/log.hpp"
#include "texture/core/mips.hpp"
#include "texture/core/service.hpp"

#include <bgfxrenderer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.PngTextureLoader.h>
#include <EffekseerRendererCommon/EffekseerRenderer.TGATextureLoader.h>

#include <bgfx/bgfx.h>

#include <cstring>

namespace storm::render::effekseer
{
namespace
{
namespace tex = storm::texture;

// Seeds of the cache key: the same image makes different files on the two compressed tiers and encoder versions.
constexpr uint64_t kKeySeedBc = 0x45464b4243ull;   // "EFKBC"
constexpr uint64_t kKeySeedEtc2 = 0x45464b4554ull; // "EFKET"

std::string utf8(const char16_t *path)
{
    char buffer[1024];
    Effekseer::ConvertUtf16ToUtf8(buffer, sizeof(buffer), path);
    return buffer;
}

bool is_png(const std::vector<uint8_t> &file)
{
    static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return file.size() >= 24 && std::memcmp(file.data(), kSignature, 8) == 0;
}

// The size in a PNG's IHDR, so that a cached copy is found without decoding the image.
bool png_size(const std::vector<uint8_t> &file, uint32_t &width, uint32_t &height)
{
    if (!is_png(file) || std::memcmp(file.data() + 12, "IHDR", 4) != 0)
        return false;
    const auto be32 = [&](size_t at) {
        return uint32_t(file[at]) << 24 | uint32_t(file[at + 1]) << 16 | uint32_t(file[at + 2]) << 8 | file[at + 3];
    };
    width = be32(16);
    height = be32(20);
    return width > 0 && height > 0;
}

bgfx::TextureHandle from_ktx(const tex::Blob &ktx, const std::string &name)
{
    const bgfx::TextureHandle handle = bgfx::createTexture(bgfx::copy(ktx.data.get(), uint32_t(ktx.size)));
    if (bgfx::isValid(handle))
        bgfx::setName(handle, name.c_str());
    return handle;
}
} // namespace

TextureLoader::TextureLoader(EffekseerRenderer::RendererRef renderer, Effekseer::FileInterfaceRef files)
    : renderer_(std::move(renderer)), files_(std::move(files))
{
}

Effekseer::TextureRef TextureLoader::Load(const char16_t *path, Effekseer::TextureType)
{
    const std::string name = utf8(path);
    const Effekseer::FileReaderRef reader = files_->OpenRead(path);
    if (reader == nullptr)
        return nullptr; // the file interface logged it
    std::vector<uint8_t> file(reader->GetLength());
    if (reader->Read(file.data(), file.size()) != file.size())
    {
        Log().error("effekseer: texture {}: short read", name);
        return nullptr;
    }
    return load(file, name);
}

void TextureLoader::Unload(Effekseer::TextureRef texture)
{
    const Effekseer::Backend::TextureRef backend = texture->GetBackend();
    if (const auto it = bytes_.find(backend.Get()); it != bytes_.end())
    {
        counts_.bytes -= it->second;
        bytes_.erase(it);
    }
    std::erase_if(pending_, [&](const Pending &pending) { return pending.texture == backend; });
}

Effekseer::TextureRef TextureLoader::wrap(uint16_t handle, uint32_t width, uint32_t height, uint32_t levels,
                                          uint64_t bytes)
{
    auto texture = Effekseer::MakeRefPtr<Effekseer::Texture>();
    texture->SetBackend(
        EffekseerRendererBGFX::CreateTextureFromHandle(renderer_, {handle}, int(width), int(height), int(levels)));
    set_bytes(texture->GetBackend(), bytes);
    return texture;
}

void TextureLoader::set_bytes(const Effekseer::Backend::TextureRef &texture, uint64_t bytes)
{
    uint64_t &held = bytes_[texture.Get()];
    counts_.bytes += bytes - held;
    held = bytes;
}

Effekseer::TextureRef TextureLoader::load(const std::vector<uint8_t> &file, const std::string &name)
{
    tex::Service &service = tex::Service::instance();
    // The tier decides the key, the image's alpha the format inside it (the cached KTX says which).
    const std::optional<tex::GpuFormat> opaque_format = shim::compressed_2d_format(false);
    const bool bc = opaque_format == tex::GpuFormat::Bc1;
    const uint64_t key = tex::hash64(file.data(), file.size(), (bc ? kKeySeedBc : kKeySeedEtc2) + tex::kEncoderVersion);
    uint32_t width = 0, height = 0;
    if (opaque_format && png_size(file, width, height))
    {
        const tex::Layout layout{tex::DxtFormat::Bc1, width, height, 1, 1};
        if (const std::optional<tex::Blob> ktx = service.lookup(key, layout))
        {
            const bgfx::TextureHandle handle = from_ktx(*ktx, name);
            if (bgfx::isValid(handle))
            {
                ++counts_.from_cache;
                return wrap(handle.idx, width, height, tex::full_mip_levels(width, height), ktx->size);
            }
            Log().error("effekseer: texture {}: bgfx refused its cached copy; decoding the image", name);
        }
    }
    std::vector<uint8_t> rgba;
    if (is_png(file))
    {
        EffekseerRenderer::PngTextureLoader png;
        if (png.Load(file.data(), int32_t(file.size()), false))
        {
            width = uint32_t(png.GetWidth());
            height = uint32_t(png.GetHeight());
            rgba = std::move(png.GetData());
        }
    }
    else if (name.ends_with(".tga") || name.ends_with(".TGA"))
    {
        EffekseerRenderer::TGATextureLoader tga;
        if (tga.Load(file.data(), int32_t(file.size())))
        {
            width = uint32_t(tga.GetWidth());
            height = uint32_t(tga.GetHeight());
            rgba = std::move(tga.GetData());
        }
    }
    else
    {
        Log().error("effekseer: texture {}: not a PNG or TGA image", name);
        return nullptr;
    }
    if (rgba.empty() || rgba.size() != size_t(width) * height * 4)
    {
        Log().error("effekseer: texture {}: cannot decode it", name);
        return nullptr;
    }
    ++counts_.decoded;
    if (!opaque_format)
        return upload_rgba(rgba, width, height, name);

    bool alpha = false;
    for (size_t i = 3; i < rgba.size() && !alpha; i += 4)
        alpha = rgba[i] != 255;
    const tex::GpuFormat format = *shim::compressed_2d_format(alpha);
    // Drawn at once without mips; the compressed copy with its whole chain replaces it when made.
    const bgfx::TextureHandle interim =
        bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_TEXTURE_NONE, bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(interim))
    {
        Log().error("effekseer: texture {}: bgfx cannot create it ({}x{})", name, width, height);
        return nullptr;
    }
    bgfx::setName(interim, name.c_str());
    Effekseer::TextureRef texture = wrap(interim.idx, width, height, 1, rgba.size());
    auto encoding = std::make_shared<Encoding>();
    service.post([encoding, rgba = std::move(rgba), width, height, format, key, directory = service.cache_directory()] {
        encoding->ktx = tex::encode_rgba_to_ktx(rgba.data(), width, height, format);
        if (!directory.empty())
        {
            const tex::Layout out{tex::DxtFormat::Bc1, width, height, tex::full_mip_levels(width, height), 1};
            tex::write_cache_file(directory, key, out, format, encoding->ktx);
        }
        encoding->done.store(true, std::memory_order_release);
    });
    pending_.push_back({std::move(encoding), texture->GetBackend(), name});
    return texture;
}

// The RGBA8 tier (a GPU with neither BC nor ETC2, or texture_compression=rgba): the image with its chain.
Effekseer::TextureRef TextureLoader::upload_rgba(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height,
                                                 const std::string &name)
{
    const uint32_t levels = tex::full_mip_levels(width, height);
    const auto chain = tex::build_mip_chain(rgba.data(), width, height, levels, false, tex::TexelSpace::Colour);
    size_t bytes = rgba.size();
    for (const auto &level : chain)
        bytes += level.size();
    const bgfx::Memory *memory = bgfx::alloc(uint32_t(bytes));
    uint8_t *out = memory->data;
    std::memcpy(out, rgba.data(), rgba.size());
    out += rgba.size();
    for (const auto &level : chain)
    {
        std::memcpy(out, level.data(), level.size());
        out += level.size();
    }
    const bgfx::TextureHandle handle = bgfx::createTexture2D(uint16_t(width), uint16_t(height), true, 1,
                                                             bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, memory);
    if (!bgfx::isValid(handle))
    {
        Log().error("effekseer: texture {}: bgfx cannot create it ({}x{})", name, width, height);
        return nullptr;
    }
    bgfx::setName(handle, name.c_str());
    return wrap(handle.idx, width, height, levels, bytes);
}

void TextureLoader::Poll()
{
    std::erase_if(pending_, [this](const Pending &pending) {
        if (!pending.encoding->done.load(std::memory_order_acquire))
            return false;
        const bgfx::TextureHandle handle = from_ktx(pending.encoding->ktx, pending.name);
        if (bgfx::isValid(handle))
        {
            EffekseerRendererBGFX::ReplaceTextureHandle(pending.texture, {handle.idx});
            set_bytes(pending.texture, pending.encoding->ktx.size);
        }
        else
            Log().error("effekseer: texture {}: bgfx refused its compressed copy; it stays uncompressed", pending.name);
        return true;
    });
}

TextureCounts TextureLoader::Counts() const
{
    TextureCounts counts = counts_;
    counts.encoding = uint32_t(pending_.size());
    counts.resident = uint32_t(bytes_.size());
    return counts;
}

} // namespace storm::render::effekseer
