// The whole runtime without a GL context, shared by the runtime checks: a renderer that
// draws nothing and file helpers for the synthetic packs they derive from tests/pack.
#pragma once

#include "storm_ui/render_backend.h"

#include <RmlUi/Core/RenderInterface.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

class NoRenderer final : public Rml::RenderInterface
{
  public:
    // Counts the meshes in the runtime's danger red (the crossed "missing icon" marker).
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int>) override
    {
        if (std::any_of(vertices.begin(), vertices.end(), [](const Rml::Vertex &vertex) {
                return vertex.colour == Rml::ColourbPremultiplied(0xc4, 0x47, 0x3a, 0xff);
            }))
            ++marker_meshes;
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle texture) override
    {
        if (texture)
            drawn_textures.insert(texture);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override
    {
    }
    // Every pack image "loads" as a pixel: what is drawn does not matter, only that it exists and which it is.
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        dimensions = {1, 1};
        loaded.push_back(source);
        return ++next_texture_;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
    {
        ++generated;
        return ++next_texture_;
    }
    void ReleaseTexture(Rml::TextureHandle) override
    {
    }
    void EnableScissorRegion(bool) override
    {
    }
    void SetScissorRegion(Rml::Rectanglei) override
    {
    }
    // Layers, filters and shaders as every backend has them (RmlUi's box shadows and the
    // runtime's filtered-layer cache need a saved layer): handles that are never drawn.
    Rml::LayerHandle PushLayer() override
    {
        return ++layers_;
    }
    void PopLayer() override
    {
        --layers_;
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return 1;
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        return 1;
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String &, const Rml::Dictionary &) override
    {
        return 1;
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String &, const Rml::Dictionary &) override
    {
        return 1;
    }

    int marker_meshes = 0;
    int generated = 0;               // textures made from pixels (fonts, layers)
    std::vector<std::string> loaded; // texture sources loaded
    std::set<Rml::TextureHandle> drawn_textures;

  private:
    Rml::TextureHandle next_texture_ = 0;
    Rml::LayerHandle layers_ = 0;
};

class NoBackend final : public storm::ui::RenderBackend
{
  public:
    Rml::RenderInterface &Interface() override
    {
        return renderer_;
    }
    void SetViewport(int, int) override
    {
    }
    void BeginFrame(bool) override
    {
    }
    void EndFrame() override
    {
    }
    PictureState AcquireGamePicture(const std::string &) override
    {
        return PictureState::Unavailable; // no game files in the checks
    }
    void Upload(Rml::TextureHandle) override
    {
        // No GPU copies in the checks.
    }
    std::string RefusedTexture(int width, int height, storm::ui::PackFormat) const override
    {
        return std::max(width, height) > max_texture
                   ? "larger than the device's texture limit of " + std::to_string(max_texture) + " px"
                   : std::string();
    }
    NoRenderer &Renderer()
    {
        return renderer_;
    }

    int max_texture = 2048; // the device limit the checks fake

  private:
    NoRenderer renderer_;
};

inline std::string Read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

inline void Write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary) << text;
}
