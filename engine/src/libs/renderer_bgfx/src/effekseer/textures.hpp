// Effect textures through the engine's texture cache (effekseer/README.md, "Textures").
#pragma once

#include "texture/core/cache.hpp"

#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::render::effekseer
{
struct TextureCounts
{
    uint32_t from_cache = 0; // compressed copy read from the cache, nothing decoded
    uint32_t decoded = 0;
    uint32_t encoding = 0; // drawn uncompressed until their compressed copy is made
    uint32_t resident = 0; // loaded and not yet released
    uint64_t bytes = 0;    // their GPU memory
};

// The manager's texture loader: PNG and TGA images, compressed for the GPU's tier on the texture workers and kept in
// the texture cache. It lives as long as the manager that holds it.
class TextureLoader final : public Effekseer::TextureLoader
{
  public:
    TextureLoader(EffekseerRenderer::RendererRef renderer, Effekseer::FileInterfaceRef files);
    Effekseer::TextureRef Load(const char16_t *path, Effekseer::TextureType type) override;
    void Unload(Effekseer::TextureRef texture) override;
    // Game thread, once a frame: swaps the finished compressed copies in.
    void Poll();
    TextureCounts Counts() const;

  private:
    struct Encoding
    {
        std::atomic<bool> done{false};
        texture::Blob ktx; // valid once done
    };
    struct Pending
    {
        std::shared_ptr<Encoding> encoding;
        Effekseer::Backend::TextureRef texture;
        std::string name;
    };

    Effekseer::TextureRef load(const std::vector<uint8_t> &file, const std::string &name);
    Effekseer::TextureRef upload_rgba(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height,
                                      const std::string &name);
    Effekseer::TextureRef wrap(uint16_t handle, uint32_t width, uint32_t height, uint32_t levels, uint64_t bytes);
    void set_bytes(const Effekseer::Backend::TextureRef &texture, uint64_t bytes);

    EffekseerRenderer::RendererRef renderer_;
    Effekseer::FileInterfaceRef files_;
    std::vector<Pending> pending_;
    std::unordered_map<const Effekseer::Backend::Texture *, uint64_t> bytes_; // of every resident texture
    TextureCounts counts_;
};
} // namespace storm::render::effekseer
