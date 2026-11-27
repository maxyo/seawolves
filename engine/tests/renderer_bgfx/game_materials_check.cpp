// A game's own materials on its real models (the installed game in STORM_GAME_DIR, one that ships material files; not
// run without one, nothing of it is copied anywhere). Each model (STORM_MATERIALS_MODELS: paths under
// Resource/Models without .gm, separated by ';', also for the layered ones; else the first four models whose material
// file names normal and rma maps) is drawn on a ground slab under a day sun and again at night by a lamp, at «Высокая»,
// once by the legacy lit program (no material: as the renderer drew it before) and once with the materials of its
// material file (every object, the default one where the file names none), read through scene_data as the geometry
// reads them. Checked: the material picture shows the model, is not the legacy one, and has no black holes (a NaN or a
// map read as nothing) where the legacy picture has the model. Every layered material of the game (`layer_material`) is
// drawn too, its objects alone by day and at night: its arrays are made, and the layers change the picture of the base
// without black holes. STORM_CHECK_DUMP=<dir> writes every picture as <model>_<light>_<legacy|material|base|layers>.ppm
// there (pictures of game data: local evidence only).
#include "device.hpp"
#include "check_backend.hpp"
#include "effects/layer_arrays.hpp"
#include "rdf.h"
#include "storm/render/effects.hpp"
#include "storm/scene_data/scene_data.hpp"
#include "texture/core/texture_file.hpp"
#include "texture/core/tx_header.hpp"

#include <SDL2/SDL.h>

#include <catch2/catch.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace storm;
using shim::Device;
namespace fs = std::filesystem;

namespace
{
constexpr uint32_t kSize = 256;

void check(HRESULT result, const char *what)
{
    INFO(what);
    REQUIRE(SUCCEEDED(result));
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::vector<uint8_t> ReadAll(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// The game's files by their lower-case path under the game folder (its names mix the case).
class GameFiles
{
  public:
    // The game's resource folder, whatever its case ("Resource", "RESOURCE"); none: no files.
    explicit GameFiles(const fs::path &root)
    {
        std::error_code error;
        for (const auto &top : fs::directory_iterator(root, error))
            if (top.is_directory() && Lower(top.path().filename().string()) == "resource")
                Walk(root, top.path());
    }
    void Walk(const fs::path &root, const fs::path &resource)
    {
        for (const auto &entry : fs::recursive_directory_iterator(resource))
            if (entry.is_regular_file())
                files_[Lower(fs::relative(entry.path(), root).string())] = entry.path();
    }
    std::optional<fs::path> Find(const std::string &relative) const
    {
        const auto found = files_.find(Lower(relative));
        return found == files_.end() ? std::nullopt : std::optional<fs::path>(found->second);
    }
    // The first texture of that file name anywhere under Textures: the scripts give a model its texture folder.
    std::optional<fs::path> FindTexture(const std::string &name) const
    {
        const std::string wanted = "/" + Lower(name);
        for (const auto &[file, path] : files_)
            if (file.rfind("resource/textures/", 0) == 0 && file.ends_with(wanted))
                return path;
        return std::nullopt;
    }
    std::vector<std::string> MaterialFiles() const
    {
        std::vector<std::string> models;
        for (const auto &[name, path] : files_)
            if (name.rfind("resource/models/", 0) == 0 && name.ends_with(".json") && !name.ends_with(".maya.json") &&
                files_.contains(name.substr(0, name.size() - 5) + ".gm"))
                models.push_back(name.substr(16, name.size() - 21));
        return models;
    }

  private:
    std::map<std::string, fs::path> files_;
};

// A model file read with the engine's own records (geometry/src/rdf.h).
struct Model
{
    RDF_HEAD head{};
    std::vector<std::string> textures;
    std::vector<RDF_MATERIAL> materials;
    std::vector<RDF_OBJECT> objects;
    std::vector<std::string> object_names;
    std::vector<uint16_t> indices; // per object, from its first vertex (RDF_OBJECT::svertex)
    struct Buffer
    {
        int32_t type = 0;
        uint32_t stride = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<Buffer> buffers;
};

template <typename T> T Take(const std::vector<uint8_t> &bytes, size_t &at)
{
    T value{};
    REQUIRE(at + sizeof(T) <= bytes.size());
    std::memcpy(&value, bytes.data() + at, sizeof(T));
    at += sizeof(T);
    return value;
}

Model ReadModel(const fs::path &path)
{
    const std::vector<uint8_t> bytes = ReadAll(path);
    Model model;
    size_t at = 0;
    model.head = Take<RDF_HEAD>(bytes, at);
    REQUIRE(RdfVersionSupported(model.head.version));
    const std::string names(reinterpret_cast<const char *>(bytes.data() + at), size_t(model.head.name_size));
    at += model.head.name_size;
    std::vector<int32_t> name_offsets(model.head.names);
    for (int32_t &offset : name_offsets)
        offset = Take<int32_t>(bytes, at);
    for (int32_t t = 0; t < model.head.ntextures; ++t)
        model.textures.emplace_back(names.c_str() + Take<int32_t>(bytes, at));
    for (int32_t m = 0; m < model.head.nmaterials; ++m)
        model.materials.push_back(Take<RDF_MATERIAL>(bytes, at));
    at += sizeof(RDF_LIGHT) * model.head.nlights + sizeof(RDF_LABEL) * model.head.nlabels;
    for (int32_t o = 0; o < model.head.nobjects; ++o)
    {
        model.objects.push_back(Take<RDF_OBJECT>(bytes, at));
        model.object_names.emplace_back(names.c_str() + model.objects.back().name);
    }
    for (int32_t i = 0; i < 3 * model.head.ntriangles; ++i)
        model.indices.push_back(Take<uint16_t>(bytes, at));
    std::vector<RDF_VERTEXBUFF> buffers(model.head.nvrtbuffs);
    for (RDF_VERTEXBUFF &buffer : buffers)
        buffer = Take<RDF_VERTEXBUFF>(bytes, at);
    for (const RDF_VERTEXBUFF &buffer : buffers)
    {
        Model::Buffer &b = model.buffers.emplace_back();
        b.type = buffer.type;
        b.stride = uint32_t(sizeof(RDF_VERTEX0) + (buffer.type & 3) * 8 + (buffer.type >> 2) * 8);
        REQUIRE(at + size_t(buffer.size) <= bytes.size());
        b.bytes.assign(bytes.begin() + long(at), bytes.begin() + long(at + buffer.size));
        at += buffer.size;
    }
    return model;
}

// What the device draws: position, normal, two texture coordinate sets (the second the first where the model has none).
struct Vertex
{
    float pos[3];
    float nrm[3];
    float uv0[2];
    float uv1[2];
};

class Harness
{
  public:
    Harness()
    {
        render::ConfigureEffectsGame({.generates_texture_maps = false});
        render::ConfigureEffects(2, {});
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
        host_check::UseBackendFromEnvironment();
        window_ = SDL_CreateWindow("game_materials_check", 0, 0, kSize, kSize, 0);
        REQUIRE(window_);
        REQUIRE(shim::Context::instance().init(window_, kSize, kSize, false));
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = kSize;
        params.BackBufferHeight = kSize;
        params.EnableAutoDepthStencil = TRUE;
        params.AutoDepthStencilFormat = D3DFMT_D24S8;
        device_ = new Device(window_, params);
        check(
            device_->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback_, nullptr),
            "CreateOffscreenPlainSurface");
        check(device_->GetRenderTarget(0, &back_), "GetRenderTarget");
        ground_ = Uniform(0xFF8C8478);
    }
    ~Harness()
    {
        for (auto &[name, texture] : textures_)
            if (texture)
                texture->Release();
        ground_->Release();
        readback_->Release();
        back_->Release();
        device_->Release();
        shim::Context::instance().shutdown();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        render::ConfigureEffectsGame({});
    }

    Device &device()
    {
        return *device_;
    }

    // A texture file of the game (DDS, TXX or .tx of DXT1/3/5 or A8R8G8B8), once per path; null when it cannot be read.
    shim::Texture2D *Load(const fs::path &path, bool map)
    {
        const std::string key = path.string() + (map ? "#map" : "");
        if (const auto found = textures_.find(key); found != textures_.end())
            return found->second;
        shim::Texture2D *&texture = textures_[key];
        std::vector<uint8_t> bytes = ReadAll(path);
        uint32_t width = 0, height = 0, levels = 0;
        D3DFORMAT format = D3DFMT_UNKNOWN;
        std::vector<std::span<const uint8_t>> data;
        texture::TextureFile file;
        const texture::TextureContainer container = texture::DetectTextureContainer(bytes);
        if (container == texture::TextureContainer::Tx)
        {
            texture::TxHeader header;
            if (bytes.size() < texture::kTxHeaderBytes)
                return nullptr;
            std::memcpy(&header, bytes.data(), sizeof(header));
            if (texture::tx_header_problem(header) || header.cube() ||
                bytes.size() < texture::kTxHeaderBytes + texture::tx_payload_bytes(header))
                return nullptr;
            width = header.width, height = header.height, levels = header.levels;
            format = D3DFORMAT(header.format);
            for (uint32_t level = 0; level < levels; ++level)
            {
                const uint64_t offset = texture::kTxHeaderBytes + texture::tx_level_offset(header.top_bytes, level);
                const uint64_t next = texture::kTxHeaderBytes + texture::tx_level_offset(header.top_bytes, level + 1);
                data.emplace_back(bytes.data() + offset, size_t(next - offset));
            }
        }
        else
        {
            const auto problem = container == texture::TextureContainer::Dds
                                     ? texture::ParseDdsTexture(std::move(bytes), file)
                                     : texture::ParseTxxTexture(std::move(bytes), file);
            if (problem || file.layout.faces != 1)
                return nullptr;
            width = file.layout.width, height = file.layout.height, levels = file.layout.levels;
            format = file.layout.format == texture::DxtFormat::Bc1   ? D3DFMT_DXT1
                     : file.layout.format == texture::DxtFormat::Bc2 ? D3DFMT_DXT3
                                                                     : D3DFMT_DXT5;
            for (uint32_t level = 0; level < levels; ++level)
                data.push_back(file.level(0, level));
        }
        if (format != D3DFMT_DXT1 && format != D3DFMT_DXT3 && format != D3DFMT_DXT5 && format != D3DFMT_A8R8G8B8)
            return nullptr;
        check(device_->CreateTexture(width, height, levels, 0, format, D3DPOOL_MANAGED, &texture, nullptr),
              "CreateTexture");
        for (uint32_t level = 0; level < levels; ++level)
        {
            D3DLOCKED_RECT rect;
            check(texture->LockRect(level, &rect, nullptr, 0), "LockRect");
            std::memcpy(rect.pBits, data[level].data(), data[level].size());
            texture->UnlockRect(level);
        }
        if (map)
            render::DeclareMaterialMap(texture);
        return texture;
    }

    struct Light
    {
        const char *name;
        glm::vec3 to_sun;
        float sun;        // the D3D sun's colour (grey)
        uint32_t ambient; // D3DRS_AMBIENT
        struct
        {
            float sun, ambient, exposure, emission;
        } scene;   // the scene environment's scales over the device's lights
        bool lamp; // a warm point light in front of the model
    };

    // The model on its ground, with its materials or the legacy program; the picture (BGRA).
    std::vector<uint8_t> Draw(const Model &model, const std::vector<shim::Texture2D *> &textures,
                              const std::vector<render::EffectsSurfaceMaterial> *surfaces, const Light &light,
                              bool objects = true)
    {
        environment::State environment;
        environment.light_dir = environment.sun_dir = glm::normalize(light.to_sun);
        environment.light_is_sun = light.to_sun.y > 0.0f && light.sun > 0.2f;
        environment.light_color = glm::vec3(light.sun * light.sun) * light.scene.sun;
        const float fill = float(light.ambient & 255) / 255.0f;
        environment.ambient = glm::vec3(fill * fill) * light.scene.ambient;
        environment.exposure = light.scene.exposure;
        environment.emission = light.scene.emission;
        render::SetSceneEnvironment(environment);
        check(device_->BeginScene(), "BeginScene");
        check(device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF101820, 1.0f, 0), "Clear");
        const glm::vec3 center(model.head.bbox_center.x, model.head.bbox_center.y, model.head.bbox_center.z);
        const float radius = std::max(model.head.radius, 0.1f);
        const glm::vec3 eye = center + glm::normalize(glm::vec3(0.7f, 0.45f, -1.0f)) * radius * 2.4f;
        const glm::mat4 view = glm::lookAtLH(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(45.0f), 1.0f, radius * 0.05f, radius * 10.0f);
        D3DMATRIX world{}, v{}, p{};
        world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1.0f;
        std::memcpy(&v, &view, sizeof(v));
        std::memcpy(&p, &projection, sizeof(p));
        device_->SetTransform(D3DTS_WORLD, &world);
        device_->SetTransform(D3DTS_VIEW, &v);
        device_->SetTransform(D3DTS_PROJECTION, &p);
        device_->SetRenderState(D3DRS_ZENABLE, TRUE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_LIGHTING, TRUE);
        device_->SetRenderState(D3DRS_AMBIENT, 0xFF000000u | light.ambient);
        D3DMATERIAL9 surface{};
        surface.Diffuse = surface.Ambient = {0.8f, 0.8f, 0.8f, 1.0f};
        device_->SetMaterial(&surface);
        D3DLIGHT9 sun{};
        sun.Type = D3DLIGHT_DIRECTIONAL;
        sun.Diffuse = {light.sun, light.sun, light.sun, 1.0f};
        const glm::vec3 direction = -glm::normalize(light.to_sun);
        sun.Direction = {direction.x, direction.y, direction.z};
        device_->SetLight(0, &sun);
        device_->LightEnable(0, TRUE);
        D3DLIGHT9 lamp{};
        lamp.Type = D3DLIGHT_POINT;
        lamp.Diffuse = {0.95f, 0.75f, 0.55f, 1.0f};
        const glm::vec3 at =
            center + glm::normalize(eye - center) * radius * 0.9f + glm::vec3(0.0f, radius * 0.3f, 0.0f);
        lamp.Position = {at.x, at.y, at.z};
        lamp.Range = radius * 3.0f;
        lamp.Attenuation0 = 1.0f;
        device_->SetLight(1, &lamp);
        device_->LightEnable(1, light.lamp);
        device_->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2);
        // The ground: a slab under the model, the default material where the model has its own.
        // The ground under the lowest vertex drawn (a lamp's bracket reaches below its header's box).
        float floor = model.head.bbox_center.y + 0.5f * model.head.bbox_size.y;
        for (const RDF_OBJECT &object : model.objects)
            for (int32_t t = 0; t < 3 * object.ntriangles; ++t)
                floor =
                    std::min(floor, Unpack(model.buffers[size_t(object.vertex_buff)],
                                           uint32_t(object.svertex) + model.indices[size_t(object.striangle) * 3 + t])
                                        .pos[1]);
        std::vector<Vertex> slab;
        const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
        for (const auto &c : corners)
            slab.push_back({{center.x + c[0] * radius * 3.0f, floor, center.z + c[1] * radius * 3.0f},
                            {0.0f, 1.0f, 0.0f},
                            {c[0], c[1]},
                            {c[0], c[1]}});
        Stages(ground_, nullptr);
        const render::EffectsSurfaceMaterial ground_material{};
        render::SetEffectsSurfaceMaterial(surfaces ? &ground_material : nullptr);
        check(device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, slab.data(), sizeof(Vertex)), "draw ground");
        for (size_t o = 0; objects && o < model.objects.size(); ++o)
        {
            const RDF_OBJECT &object = model.objects[o];
            const RDF_MATERIAL &material = model.materials[size_t(object.material)];
            const auto stage = [&](int s) {
                return material.texture_type[s] != TEXTURE_NONE && material.texture[s] >= 0
                           ? textures[size_t(material.texture[s])]
                           : nullptr;
            };
            Stages(stage(0), stage(1));
            render::SetEffectsSurfaceMaterial(surfaces ? &(*surfaces)[o] : nullptr);
            const Model::Buffer &buffer = model.buffers[size_t(object.vertex_buff)];
            std::vector<Vertex> vertices;
            for (int32_t t = 0; t < object.ntriangles; ++t)
                for (int k = 0; k < 3; ++k)
                    vertices.push_back(
                        Unpack(buffer, uint32_t(object.svertex) + model.indices[size_t(object.striangle + t) * 3 + k]));
            if (!vertices.empty())
                check(device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(vertices.size() / 3), vertices.data(),
                                               sizeof(Vertex)),
                      "draw object");
        }
        render::SetEffectsSurfaceMaterial(nullptr);
        check(device_->EndScene(), "EndScene");
        check(device_->GetRenderTargetData(back_, readback_), "GetRenderTargetData");
        D3DLOCKED_RECT locked;
        check(readback_->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect(readback)");
        std::vector<uint8_t> pixels(kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
            std::memcpy(&pixels[y * kSize * 4], static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch, kSize * 4);
        readback_->UnlockRect();
        check(device_->Present(), "Present");
        return pixels;
    }

    // Until the preset is prepared and the layer arrays the draws asked for are made (a 2048 array takes seconds).
    void Settle(const Model &model, const std::vector<shim::Texture2D *> &textures,
                const std::vector<render::EffectsSurfaceMaterial> &surfaces, const Light &light)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        // Quiet for 30 frames: a preset's switch asks for the normal arrays only once it is active.
        for (int quiet = 0;; ++quiet)
        {
            Draw(model, textures, &surfaces, light);
            if (render::CurrentEffects().pending || effects::LayerArraysPending())
                quiet = 0;
            if (quiet > 30)
                return;
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            SDL_Delay(5);
        }
    }

  private:
    // As the geometry sets a model's stages (GEOM_SERVICE_R::SetMaterial).
    void Stages(shim::Texture2D *base, shim::Texture2D *second)
    {
        device_->SetTexture(0, base);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TEXTURE);
        device_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        device_->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_CURRENT);
        device_->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        if (second)
        {
            device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
            device_->SetTexture(1, second);
            device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
            device_->SetTexture(2, nullptr);
            device_->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        }
        else
        {
            device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
            device_->SetTexture(1, nullptr);
            device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device_->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        }
    }

    static Vertex Unpack(const Model::Buffer &buffer, uint32_t index)
    {
        const uint8_t *at = buffer.bytes.data() + size_t(index) * buffer.stride;
        Vertex v{};
        if (buffer.type & 4)
        {
            const auto *a = reinterpret_cast<const RDF_AVERTEX0 *>(at);
            v = {{a->pos.x, a->pos.y, a->pos.z}, {a->norm.x, a->norm.y, a->norm.z}, {a->tu0, a->tv0}, {a->tu0, a->tv0}};
            return v;
        }
        const auto *r = reinterpret_cast<const RDF_VERTEX1 *>(at);
        v = {{r->pos.x, r->pos.y, r->pos.z}, {r->norm.x, r->norm.y, r->norm.z}, {r->tu0, r->tv0}, {r->tu0, r->tv0}};
        if (buffer.type & 3)
            v.uv1[0] = r->tu1, v.uv1[1] = r->tv1;
        return v;
    }

    shim::Texture2D *Uniform(uint32_t argb)
    {
        shim::Texture2D *texture = nullptr;
        check(device_->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
        D3DLOCKED_RECT rect;
        check(texture->LockRect(0, &rect, nullptr, 0), "LockRect");
        for (uint32_t i = 0; i < 16; ++i)
            *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rect.pBits) + (i / 4) * rect.Pitch + (i % 4) * 4) =
                argb;
        texture->UnlockRect(0);
        return texture;
    }

    SDL_Window *window_ = nullptr;
    Device *device_ = nullptr;
    shim::Surface *readback_ = nullptr, *back_ = nullptr;
    shim::Texture2D *ground_ = nullptr;
    std::map<std::string, shim::Texture2D *> textures_;
};

void Dump(const std::vector<uint8_t> &picture, const std::string &name)
{
    const char *dir = std::getenv("STORM_CHECK_DUMP");
    if (!dir)
        return;
    std::ofstream out(fs::path(dir) / (name + ".ppm"), std::ios::binary);
    out << "P6 " << kSize << " " << kSize << " 255\n";
    for (size_t i = 0; i < picture.size(); i += 4)
        out.put(char(picture[i + 2])).put(char(picture[i + 1])).put(char(picture[i]));
}
// The model's stage textures: under Textures as named, else in the folder the scripts would give the model (the
// first texture of the name); .tga names are the .tga.tx files.
std::vector<shim::Texture2D *> LoadTextures(Harness &h, const GameFiles &files, const Model &model,
                                            const std::string &name)
{
    std::vector<shim::Texture2D *> textures;
    for (const std::string &texture : model.textures)
    {
        const std::string file = Lower(texture).ends_with(".tga") ? texture + ".tx" : texture;
        std::optional<fs::path> path = files.Find("resource/textures/" + file);
        if (!path)
            path = files.FindTexture(fs::path(Lower(file)).filename().string());
        textures.push_back(path ? h.Load(*path, false) : nullptr);
        if (!textures.back())
            std::printf("%s: texture %s not loaded\n", name.c_str(), texture.c_str());
    }
    return textures;
}

// A material file's texture (a path under Resource\Textures) as the game file, if the game has it.
std::optional<fs::path> MaterialFile(const GameFiles &files, const std::string &path)
{
    const std::optional<std::string> texture = scene_data::MaterialTextureName(path);
    if (!texture)
        return std::nullopt;
    return files.Find("resource/textures/" + (Lower(*texture).ends_with(".tga") ? *texture + ".tx" : *texture));
}

scene_data::ModelSidecar ReadSidecar(const GameFiles &files, const std::string &name)
{
    scene_data::Report report;
    const std::vector<uint8_t> json = ReadAll(*files.Find("resource/models/" + name + ".json"));
    scene_data::ModelSidecar sidecar = scene_data::ParseModelSidecar(
        std::string_view(reinterpret_cast<const char *>(json.data()), json.size()), report);
    CHECK(report.errors.empty());
    return sidecar;
}
} // namespace

TEST_CASE("a game's own materials on its models")
{
    const char *game = std::getenv("STORM_GAME_DIR");
    if (!game)
    {
        std::printf("NOT_RUN: STORM_GAME_DIR names no game folder\n");
        std::exit(77);
    }
    const GameFiles files{fs::path(game)};
    if (files.MaterialFiles().empty())
    {
        std::printf("NOT_RUN: the game in STORM_GAME_DIR ships no material files\n");
        std::exit(77);
    }
    std::vector<std::string> models;
    if (const char *list = std::getenv("STORM_MATERIALS_MODELS"))
    {
        std::string rest = list;
        for (size_t end; !rest.empty(); rest = end == std::string::npos ? "" : rest.substr(end + 1))
            if (const std::string item = rest.substr(0, end = rest.find(';')); !item.empty())
                models.push_back(item);
    }
    else
        for (const std::string &candidate : files.MaterialFiles())
        {
            scene_data::Report report;
            const std::vector<uint8_t> json = ReadAll(*files.Find("resource/models/" + candidate + ".json"));
            const auto file = scene_data::ParseModelSidecar(
                std::string_view(reinterpret_cast<const char *>(json.data()), json.size()), report);
            if (std::any_of(file.models.begin(), file.models.end(),
                            [](const auto &entry) {
                                return !entry.second.normal_texture.empty() && !entry.second.rma_texture.empty();
                            }) &&
                models.size() < 4)
                models.push_back(candidate);
        }
    REQUIRE_FALSE(models.empty());
    Harness h;
    const std::array<Harness::Light, 2> lights{{
        {"day",
         {-0.5f, 0.8f, -0.4f},
         1.0f,
         0x606060,
         {.sun = 4.0f, .ambient = 1.5f, .exposure = 2.0f, .emission = 0.0f},
         false},
        {"night",
         {0.3f, 0.6f, 0.5f},
         0.55f,
         0x303848,
         {.sun = 0.16f, .ambient = 0.3f, .exposure = 1.0f, .emission = 1.0f},
         true},
    }};
    for (const std::string &name : models)
    {
        INFO(name);
        const std::optional<fs::path> gm = files.Find("resource/models/" + name + ".gm");
        REQUIRE(gm);
        const Model model = ReadModel(*gm);
        const std::vector<shim::Texture2D *> textures = LoadTextures(h, files, model, name);
        const scene_data::ModelSidecar file = ReadSidecar(files, name);
        const auto map = [&](const std::string &path, bool data) -> shim::Texture2D * {
            const std::optional<fs::path> found = MaterialFile(files, path);
            return found ? h.Load(*found, data) : nullptr;
        };
        std::vector<render::EffectsSurfaceMaterial> surfaces(model.objects.size());
        int with_maps = 0;
        for (size_t o = 0; o < model.objects.size(); ++o)
        {
            const RDF_MATERIAL &material = model.materials[size_t(model.objects[o].material)];
            surfaces[o].occlusion_uv1 = material.texture_type[1] != TEXTURE_NONE && material.texture[1] >= 0;
            const auto entry = file.models.find(model.object_names[o]);
            if (entry == file.models.end())
                continue;
            surfaces[o].normal = map(entry->second.normal_texture, true);
            surfaces[o].rma = map(entry->second.rma_texture, true);
            surfaces[o].emission = map(entry->second.emission_texture, false);
            surfaces[o].roughness = entry->second.roughness.value_or(1.0f);
            surfaces[o].metallic = entry->second.metallic.value_or(0.0f);
            surfaces[o].emission_strength = entry->second.emission.value_or(0.0f);
            with_maps += surfaces[o].normal && surfaces[o].rma;
        }
        std::string tag = name;
        std::replace(tag.begin(), tag.end(), '/', '_');
        for (const Harness::Light &light : lights)
        {
            INFO(light.name);
            h.Settle(model, textures, surfaces, light);
            const std::vector<uint8_t> legacy = h.Draw(model, textures, nullptr, light);
            const std::vector<uint8_t> material = h.Draw(model, textures, &surfaces, light);
            Dump(legacy, tag + "_" + light.name + "_legacy");
            Dump(material, tag + "_" + light.name + "_material");
            const std::vector<uint8_t> background = h.Draw(model, textures, nullptr, light, false);
            size_t covered = 0, black = 0, differs = 0;
            for (size_t i = 0; i < legacy.size(); i += 4)
            {
                const bool shown = legacy[i] != background[i] || legacy[i + 1] != background[i + 1] ||
                                   legacy[i + 2] != background[i + 2];
                if (!shown)
                    continue;
                ++covered;
                black += material[i] == 0 && material[i + 1] == 0 && material[i + 2] == 0;
                differs += std::abs(int(material[i + 1]) - int(legacy[i + 1])) > 4;
            }
            std::printf("%s %s: %d objects with maps, %zu pixels drawn, %zu black with materials, %zu differ\n",
                        name.c_str(), light.name, with_maps, covered, black, differs);
            CHECK(covered > kSize * kSize / 40);
            CHECK(black * 50 < covered);
            CHECK(differs * 4 > covered);
        }
    }
}

TEST_CASE("every layered material of the game: its arrays are made and its layers draw")
{
    const char *game = std::getenv("STORM_GAME_DIR");
    if (!game)
    {
        std::printf("NOT_RUN: STORM_GAME_DIR names no game folder\n");
        std::exit(77);
    }
    const GameFiles files{fs::path(game)};
    std::vector<std::pair<std::string, scene_data::ModelSidecar>> layered;
    const char *only = std::getenv("STORM_MATERIALS_MODELS");
    for (const std::string &name : files.MaterialFiles())
        if (scene_data::ModelSidecar sidecar = ReadSidecar(files, name);
            (!only || std::string(only).find(name) != std::string::npos) &&
            std::any_of(sidecar.models.begin(), sidecar.models.end(),
                        [](const auto &entry) { return entry.second.layer_material; }))
            layered.emplace_back(name, std::move(sidecar));
    if (layered.empty())
    {
        std::printf("NOT_RUN: the game in STORM_GAME_DIR has no layered materials\n");
        std::exit(77);
    }
    Harness h;
    const Harness::Light day{
        "day", {-0.5f, 0.8f, -0.4f}, 1.0f, 0x606060, {.sun = 4.0f, .ambient = 1.5f, .exposure = 2.0f, .emission = 0.0f},
        false};
    const Harness::Light night{"night",
                               {0.3f, 0.6f, 0.5f},
                               0.55f,
                               0x303848,
                               {.sun = 0.16f, .ambient = 0.3f, .exposure = 1.0f, .emission = 1.0f},
                               true};
    std::vector<std::shared_ptr<render::EffectsLayerSet>> sets;
    for (const auto &[name, sidecar] : layered)
    {
        INFO(name);
        const std::optional<fs::path> gm = files.Find("resource/models/" + name + ".gm");
        if (!gm)
            continue; // a material file of a model the game no longer ships
        Model all = ReadModel(*gm);
        // The layered objects alone, framed by their spheres.
        Model model = all;
        model.objects.clear();
        model.object_names.clear();
        glm::vec3 lo(1e30f), hi(-1e30f);
        std::vector<const scene_data::ModelMaterial *> entries;
        for (size_t o = 0; o < all.objects.size(); ++o)
            if (const auto entry = sidecar.models.find(all.object_names[o]);
                entry != sidecar.models.end() && entry->second.layer_material && all.objects[o].ntriangles)
            {
                model.objects.push_back(all.objects[o]);
                model.object_names.push_back(all.object_names[o]);
                entries.push_back(&entry->second);
                const RDF_OBJECT &object = all.objects[o];
                const glm::vec3 c(object.center.x, object.center.y, object.center.z);
                lo = glm::min(lo, c - glm::vec3(object.radius));
                hi = glm::max(hi, c + glm::vec3(object.radius));
            }
        if (model.objects.empty())
            continue;
        const glm::vec3 center = 0.5f * (lo + hi), size = hi - lo;
        model.head.bbox_center = {center.x, center.y, center.z};
        model.head.bbox_size = {size.x, size.y, size.z};
        model.head.radius = 0.5f * glm::length(size) / 1.7f;
        const std::vector<shim::Texture2D *> textures = LoadTextures(h, files, model, name);
        std::vector<render::EffectsSurfaceMaterial> base(model.objects.size()), layers;
        size_t present = 0;
        for (size_t o = 0; o < model.objects.size(); ++o)
        {
            const scene_data::ModelMaterial &entry = *entries[o];
            const auto map = [&](const std::string &path, bool data) -> shim::Texture2D * {
                const std::optional<fs::path> found = MaterialFile(files, path);
                return found ? h.Load(*found, data) : nullptr;
            };
            base[o].normal = map(entry.normal_texture, true);
            base[o].rma = map(entry.rma_texture, true);
            base[o].roughness = entry.roughness.value_or(1.0f);
            base[o].metallic = entry.metallic.value_or(0.0f);
            const RDF_MATERIAL &material = model.materials[size_t(model.objects[o].material)];
            base[o].occlusion_uv1 = material.texture_type[1] != TEXTURE_NONE && material.texture[1] >= 0;
            render::EffectsLayerFiles names;
            for (size_t layer = 0; layer < 3; ++layer)
            {
                const scene_data::LayerTextures &l = entry.layers[layer];
                const std::string *kinds[3] = {&l.albedo, &l.rma, &l.normal};
                for (size_t kind = 0; kind < 3; ++kind)
                    if (const std::optional<fs::path> found = MaterialFile(files, *kinds[kind]))
                    {
                        names[kind][layer] = found->string();
                        present += kind == 0;
                    }
            }
            sets.push_back(render::MakeEffectsLayerSet(names, 0));
        }
        layers = base;
        for (size_t o = 0; o < model.objects.size(); ++o)
        {
            const scene_data::ModelMaterial &entry = *entries[o];
            const std::optional<fs::path> mask = MaterialFile(files, entry.mask_texture);
            layers[o].layer_mask = mask ? h.Load(*mask, true) : nullptr;
            layers[o].layers = sets[sets.size() - model.objects.size() + o].get();
            layers[o].layer_tiling = {entry.base_tiling, entry.layers[0].tiling, entry.layers[1].tiling,
                                      entry.layers[2].tiling};
            layers[o].normal_strength = entry.normal_strength;
            if (!mask)
                std::printf("%s %s: no mask %s\n", name.c_str(), model.object_names[o].c_str(),
                            entry.mask_texture.c_str());
        }
        const auto started = std::chrono::steady_clock::now();
        h.Settle(model, textures, layers, day);
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        const std::vector<uint8_t> plain = h.Draw(model, textures, &base, day);
        const std::vector<uint8_t> layered_picture = h.Draw(model, textures, &layers, day);
        const std::vector<uint8_t> background = h.Draw(model, textures, &base, day, false);
        std::string tag = name;
        std::replace(tag.begin(), tag.end(), '/', '_');
        Dump(plain, tag + "_day_base");
        Dump(layered_picture, tag + "_day_layers");
        size_t covered = 0, black = 0, differs = 0;
        for (size_t i = 0; i < plain.size(); i += 4)
        {
            if (plain[i] == background[i] && plain[i + 1] == background[i + 1] && plain[i + 2] == background[i + 2])
                continue;
            ++covered;
            black += layered_picture[i] == 0 && layered_picture[i + 1] == 0 && layered_picture[i + 2] == 0;
            differs += std::abs(int(layered_picture[i + 1]) - int(plain[i + 1])) > 4;
        }
        std::printf("%s: %zu layered objects, %zu layer albedo files, settled in %lld ms, %zu pixels drawn, %zu black, "
                    "%zu differ\n",
                    name.c_str(), model.objects.size(), present, static_cast<long long>(ms), covered, black, differs);
        CHECK(covered > kSize * kSize / 40);
        CHECK(black * 50 < covered);
        if (present)
            CHECK(differs * 10 > covered);
        // At night by a lamp: the same arrays, no black holes.
        h.Settle(model, textures, layers, night);
        const std::vector<uint8_t> dark = h.Draw(model, textures, &layers, night);
        Dump(h.Draw(model, textures, &base, night), tag + "_night_base");
        Dump(dark, tag + "_night_layers");
        size_t dark_black = 0;
        for (size_t i = 0; i < dark.size(); i += 4)
            dark_black += dark[i] == 0 && dark[i + 1] == 0 && dark[i + 2] == 0;
        CHECK(dark_black * 50 < dark.size() / 4);
    }
}
