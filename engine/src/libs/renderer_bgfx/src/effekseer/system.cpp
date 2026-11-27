#include "storm/render/effekseer.hpp"

#include "effects/effects.hpp"
#include "effekseer/system.hpp"
#include "effekseer/textures.hpp"
#include "shim/device.hpp"
#include "istring.hpp"
#include "storm/render/log.hpp"

#include <bgfxrenderer.h>
#include <Effekseer.h>
#include <Effekseer/Effekseer.CurveLoader.h>
#include <Effekseer/Effekseer.DefaultEffectLoader.h>
#include <Effekseer/Effekseer.EffectNode.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace storm::render::effekseer
{
namespace
{
// Particle instances the manager holds: twice the 3794 of all 24 of Caribbean Legend's effects at their peaks together
// (renderer_bgfx.effekseer_check.corpus).
constexpr int32_t kMaxInstances = 8192;
// Sprites of one batch in the transient vertex buffer: more than three times the 2578 of its largest node (Tornado).
constexpr int32_t kMaxSquares = 8192;

std::string utf8(const char16_t *path)
{
    char buffer[1024];
    Effekseer::ConvertUtf16ToUtf8(buffer, sizeof(buffer), path);
    return buffer;
}

std::u16string utf16(std::string_view path)
{
    std::vector<char16_t> buffer(path.size() + 1);
    Effekseer::ConvertUtf8ToUtf16(buffer.data(), int32_t(buffer.size()), std::string(path).c_str());
    return buffer.data();
}

class MemoryReader final : public Effekseer::FileReader
{
  public:
    explicit MemoryReader(std::vector<uint8_t> data) : data_(std::move(data))
    {
    }
    size_t Read(void *buffer, size_t size) override
    {
        const size_t count = std::min(size, data_.size() - position_);
        std::memcpy(buffer, data_.data() + position_, count);
        position_ += count;
        return count;
    }
    void Seek(int position) override
    {
        position_ = std::min(size_t(std::max(position, 0)), data_.size());
    }
    int GetPosition() const override
    {
        return int(position_);
    }
    size_t GetLength() const override
    {
        return data_.size();
    }

  private:
    std::vector<uint8_t> data_;
    size_t position_ = 0;
};

// Effects, textures, models and curves are read through the engine's file service.
class GameFiles final : public Effekseer::FileInterface
{
  public:
    explicit GameFiles(FileReader reader) : reader_(std::move(reader))
    {
    }
    Effekseer::FileReaderRef OpenRead(const char16_t *path) override
    {
        std::string name = utf8(path);
        std::replace(name.begin(), name.end(), '\\', '/');
        std::optional<std::vector<uint8_t>> data = reader_(name);
        if (!data)
        {
            Log().error("effekseer: cannot read {}", name);
            return nullptr;
        }
        return Effekseer::MakeRefPtr<MemoryReader>(std::move(*data));
    }
    Effekseer::FileWriterRef OpenWrite(const char16_t *path) override
    {
        Log().error("effekseer: an effect asked to write {}", utf8(path));
        return nullptr;
    }

  private:
    FileReader reader_;
};

// efkbgfx has no material shader generator: a material is refused by name and its nodes are not drawn.
class NoMaterials final : public Effekseer::MaterialLoader
{
  public:
    Effekseer::MaterialRef Load(const char16_t *path) override
    {
        Log().error("effekseer: material {} is not supported; its nodes are not drawn", utf8(path));
        return nullptr;
    }
};

struct System
{
    FileReader reader;
    EffekseerRenderer::RendererRef renderer;
    Effekseer::RefPtr<TextureLoader> textures;
    Effekseer::ManagerRef manager;
    std::map<EffectId, Effekseer::EffectRef> effects;
    std::map<std::string, EffectId> by_path;
    std::map<EffectId, std::string> paths;
    std::set<std::string> failed; // paths whose loading failed (reported once)
    EffectId next_id = 1;
    int32_t draw_calls = 0;
};

System &sys()
{
    static System system;
    return system;
}

void renderer_log(const char *message)
{
    Log().error("effekseer: {}", message);
}

// Made on the first load, so that a game without effects pays nothing.
bool ensure_manager()
{
    System &s = sys();
    if (s.manager != nullptr)
        return true;
    if (!shim::Context::instance().initialised())
    {
        Log().error("effekseer: an effect is loaded before the renderer runs");
        return false;
    }
    if (!s.reader)
    {
        Log().error("effekseer: an effect is loaded before the file reader is set");
        return false;
    }
    EffekseerRendererBGFX::InitArgs args{};
    args.squareMaxCount = kMaxSquares;
    args.bgfx = bgfx_get_interface(BGFX_API_VERSION);
    args.mirror_winding = shim::Context::instance().flip_y();
    args.log = renderer_log;
    s.renderer = EffekseerRendererBGFX::CreateRenderer(&args);
    if (s.renderer == nullptr)
    {
        Log().error("effekseer: cannot create the renderer");
        return false;
    }
    s.manager = Effekseer::Manager::Create(kMaxInstances);
    // Effekseer's default right-handed system under the game's left-handed camera, as Caribbean Legend's engine sets
    // it: the game's effects are made to look right so (the rain's cylinders around the eye show their inner faces).
    s.manager->SetSpriteRenderer(s.renderer->CreateSpriteRenderer());
    s.manager->SetRibbonRenderer(s.renderer->CreateRibbonRenderer());
    s.manager->SetRingRenderer(s.renderer->CreateRingRenderer());
    s.manager->SetTrackRenderer(s.renderer->CreateTrackRenderer());
    s.manager->SetModelRenderer(EffekseerRendererBGFX::CreateModelRenderer(s.renderer, &args));
    const auto files = Effekseer::MakeRefPtr<GameFiles>(s.reader);
    s.manager->SetEffectLoader(Effekseer::MakeRefPtr<Effekseer::DefaultEffectLoader>(files));
    s.textures = Effekseer::MakeRefPtr<TextureLoader>(s.renderer, files);
    s.manager->SetTextureLoader(s.textures);
    s.manager->SetModelLoader(s.renderer->CreateModelLoader(files));
    s.manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>(files));
    s.manager->SetMaterialLoader(Effekseer::MakeRefPtr<NoMaterials>());
    Log().info("effekseer: ready ({} instances, {} sprites a batch)", kMaxInstances, kMaxSquares);
    return true;
}

void count_nodes(Effekseer::EffectNode *node, EffectInfo &info)
{
    const Effekseer::EffectNodeType type = node->GetType();
    if (type != Effekseer::EffectNodeType::Root && type != Effekseer::EffectNodeType::NoneType)
    {
        ++info.nodes;
        const Effekseer::EffectBasicRenderParameter param = node->GetBasicRenderParameter();
        if (param.MaterialType == Effekseer::RendererMaterialType::BackDistortion || param.Distortion)
            ++info.distortion_nodes;
        else if (param.MaterialType == Effekseer::RendererMaterialType::File)
            ++info.material_nodes;
        if (param.SoftParticleDistanceFar != 0.0f || param.SoftParticleDistanceNear != 0.0f ||
            param.SoftParticleDistanceNearOffset != 0.0f)
            ++info.soft_particle_nodes;
    }
    if (static_cast<Effekseer::EffectNodeImplemented *>(node)->GpuParticlesResource != nullptr)
        ++info.gpu_particle_nodes;
    for (int i = 0; i < node->GetChildrenCount(); ++i)
        count_nodes(node->GetChild(i), info);
}

EffectInfo inspect(const Effekseer::EffectRef &effect)
{
    EffectInfo info;
    for (int32_t i = 0; i < effect->GetColorImageCount(); ++i)
        info.missing_textures += effect->GetColorImage(i) == nullptr;
    for (int32_t i = 0; i < effect->GetNormalImageCount(); ++i)
        info.missing_textures += effect->GetNormalImage(i) == nullptr;
    for (int32_t i = 0; i < effect->GetDistortionImageCount(); ++i)
        info.missing_textures += effect->GetDistortionImage(i) == nullptr;
    for (int32_t i = 0; i < effect->GetModelCount(); ++i)
        info.missing_models += effect->GetModel(i) == nullptr;
    info.sounds = effect->GetWaveCount();
    count_nodes(effect->GetRoot(), info);
    return info;
}

// What of an effect will not be shown; the rest of it plays.
void report(const std::string &path, const Effekseer::EffectRef &effect)
{
    const EffectInfo info = inspect(effect);
    if (info.missing_textures || info.missing_models)
        Log().error("effekseer: {}: {} texture(s) and {} model(s) are missing (named above)", path,
                    info.missing_textures, info.missing_models);
    if (info.sounds)
        Log().warn("effekseer: {}: {} sound(s) are not played", path, info.sounds);
    if (info.distortion_nodes)
        Log().warn("effekseer: {}: {} distortion node(s) are not drawn", path, info.distortion_nodes);
    if (info.soft_particle_nodes)
        Log().warn("effekseer: {}: {} soft particle node(s) are drawn with hard edges", path, info.soft_particle_nodes);
    if (info.material_nodes)
        Log().error("effekseer: {}: {} material node(s) are not drawn", path, info.material_nodes);
    if (info.gpu_particle_nodes)
        Log().error("effekseer: {}: {} GPU particle node(s) are not drawn", path, info.gpu_particle_nodes);
}

Effekseer::EffectRef find(EffectId id)
{
    const auto it = sys().effects.find(id);
    return it == sys().effects.end() ? nullptr : it->second;
}

Effekseer::Manager *live(InstanceId instance)
{
    Effekseer::Manager *manager = sys().manager.Get();
    return manager && instance >= 0 && manager->Exists(instance) ? manager : nullptr;
}

Effekseer::Vector3D v3(const Vec3 &v)
{
    return {v.x, v.y, v.z};
}
} // namespace

void SetFileReader(FileReader reader)
{
    sys().reader = std::move(reader);
}

EffectId FindEffect(std::string_view path)
{
    const auto it = sys().by_path.find(ResourceKey(std::string(path)));
    return it == sys().by_path.end() ? 0 : it->second;
}

EffectId LoadEffect(std::string_view path)
{
    if (const EffectId loaded = FindEffect(path))
        return loaded;
    System &s = sys();
    const std::string key = ResourceKey(std::string(path));
    if (s.failed.contains(key) || !ensure_manager())
        return 0;
    const std::u16string path16 = utf16(path);
    Effekseer::EffectRef effect = Effekseer::Effect::Create(s.manager, path16.c_str());
    if (effect == nullptr)
    {
        Log().error("effekseer: cannot load {}", path);
        s.failed.insert(key);
        return 0;
    }
    report(std::string(path), effect);
    const EffectId id = s.next_id++;
    s.by_path[key] = id;
    s.paths[id] = std::string(path);
    s.effects[id] = std::move(effect);
    return id;
}

bool ReloadEffect(EffectId id)
{
    const Effekseer::EffectRef effect = find(id);
    if (effect == nullptr)
        return false;
    System &s = sys();
    const std::string &path = s.paths[id];
    const std::u16string path16 = utf16(path);
    if (!effect->Reload(&s.manager, 1, path16.c_str()))
    {
        Log().error("effekseer: cannot reload {}", path);
        return false;
    }
    report(path, effect);
    return true;
}

void UnloadEffect(EffectId id)
{
    const Effekseer::EffectRef effect = find(id);
    if (effect == nullptr)
        return;
    System &s = sys();
    s.manager->StopRoot(effect);
    s.by_path.erase(ResourceKey(s.paths[id]));
    s.paths.erase(id);
    s.effects.erase(id);
}

void UnloadAllEffects()
{
    System &s = sys();
    if (s.manager != nullptr)
        s.manager->StopAllEffects();
    s.effects.clear();
    s.by_path.clear();
    s.paths.clear();
    s.failed.clear();
}

InstanceId Play(EffectId id, const Vec3 &position, const Vec3 &rotation, const Vec3 &scale, int32_t start_frame)
{
    Effekseer::Manager::PlayParameter parameter;
    parameter.Effect = find(id);
    if (parameter.Effect == nullptr)
    {
        Log().error("effekseer: Play: no effect {}", id);
        return kNoInstance;
    }
    parameter.Position = v3(position);
    parameter.Rotation = v3(rotation);
    parameter.Scale = v3(scale);
    parameter.StartFrame = start_frame;
    return sys().manager->Play(parameter);
}

bool Exists(InstanceId instance)
{
    return live(instance) != nullptr;
}

void Stop(InstanceId instance)
{
    if (Effekseer::Manager *m = live(instance))
        m->StopEffect(instance);
}

void StopRoot(InstanceId instance)
{
    if (Effekseer::Manager *m = live(instance))
        m->StopRoot(instance);
}

void StopAll()
{
    if (sys().manager != nullptr)
        sys().manager->StopAllEffects();
}

void SetLocation(InstanceId instance, const Vec3 &position)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetLocation(instance, v3(position));
}

Vec3 GetLocation(InstanceId instance)
{
    Effekseer::Manager *m = live(instance);
    if (!m)
        return {};
    const Effekseer::Vector3D v = m->GetLocation(instance);
    return {v.X, v.Y, v.Z};
}

void AddLocation(InstanceId instance, const Vec3 &offset)
{
    if (Effekseer::Manager *m = live(instance))
        m->AddLocation(instance, v3(offset));
}

void SetRotation(InstanceId instance, const Vec3 &angles)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetRotation(instance, angles.x, angles.y, angles.z);
}

void SetScale(InstanceId instance, const Vec3 &scale)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetScale(instance, scale.x, scale.y, scale.z);
}

void SetTargetLocation(InstanceId instance, const Vec3 &target)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetTargetLocation(instance, v3(target));
}

void SetSpeed(InstanceId instance, float speed)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetSpeed(instance, speed);
}

float GetSpeed(InstanceId instance)
{
    Effekseer::Manager *m = live(instance);
    return m ? m->GetSpeed(instance) : 0.0f;
}

void SetPaused(InstanceId instance, bool paused)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetPaused(instance, paused);
}

void SetPausedAll(bool paused)
{
    if (sys().manager != nullptr)
        sys().manager->SetPausedToAllEffects(paused);
}

void SetColor(InstanceId instance, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetAllColor(instance, Effekseer::Color(r, g, b, a));
}

void SetDynamicInput(InstanceId instance, int32_t index, float value)
{
    if (Effekseer::Manager *m = live(instance))
        m->SetDynamicInput(instance, index, value);
}

float GetDynamicInput(InstanceId instance, int32_t index)
{
    Effekseer::Manager *m = live(instance);
    return m ? m->GetDynamicInput(instance, index) : 0.0f;
}

void SendTrigger(InstanceId instance, int32_t index)
{
    if (Effekseer::Manager *m = live(instance))
        m->SendTrigger(instance, index);
}

void Update(float seconds)
{
    if (sys().manager == nullptr)
        return;
    sys().textures->Poll();
    sys().manager->Update(std::max(0.0f, seconds) * 60.0f);
}

void Draw(const float view[16], const float projection[16])
{
    System &s = sys();
    s.draw_calls = 0;
    shim::Device *device = shim::Device::current;
    if (s.manager == nullptr || s.manager->GetTotalInstanceCount() == 0 || !device ||
        !shim::Context::instance().ready())
        return;
    D3DMATRIX d3d_projection;
    std::memcpy(&d3d_projection, projection, sizeof(d3d_projection));
    const std::array<float, 16> backend = effects::BackendProjection(*device, d3d_projection);
    Effekseer::Matrix44 camera, clip;
    std::memcpy(camera.Values, view, sizeof(camera.Values));
    std::memcpy(clip.Values, backend.data(), sizeof(clip.Values));
    s.renderer->SetCameraMatrix(camera);
    s.renderer->SetProjectionMatrix(clip);
    // The camera position: -t * R^T of the row-vector view matrix; it looks along the view's third column.
    Effekseer::Manager::DrawParameter parameter;
    parameter.ViewProjectionMatrix = s.renderer->GetCameraProjectionMatrix();
    parameter.CameraPosition = {-(view[12] * view[0] + view[13] * view[1] + view[14] * view[2]),
                                -(view[12] * view[4] + view[13] * view[5] + view[14] * view[6]),
                                -(view[12] * view[8] + view[13] * view[9] + view[14] * view[10])};
    parameter.CameraFrontDirection = {view[2], view[6], view[10]};
    EffekseerRendererBGFX::SetViewId(s.renderer, device->effects_view(true));
    s.renderer->ResetDrawCallCount();
    s.renderer->BeginRendering();
    s.manager->Draw(parameter);
    s.renderer->EndRendering();
    s.draw_calls = s.renderer->GetDrawCallCount();
}

std::optional<EffectInfo> GetEffectInfo(EffectId id)
{
    const Effekseer::EffectRef effect = find(id);
    if (effect == nullptr)
        return std::nullopt;
    return inspect(effect);
}

Stats GetStats()
{
    const System &s = sys();
    Stats stats;
    stats.effects = uint32_t(s.effects.size());
    stats.instances = s.manager != nullptr ? s.manager->GetTotalInstanceCount() : 0;
    stats.draw_calls = s.draw_calls;
    const TextureCounts textures = s.textures != nullptr ? s.textures->Counts() : TextureCounts{};
    stats.textures = textures.resident;
    stats.texture_bytes = textures.bytes;
    stats.textures_from_cache = textures.from_cache;
    stats.textures_decoded = textures.decoded;
    stats.textures_encoding = textures.encoding;
    return stats;
}

void ShutdownDevice()
{
    System &s = sys();
    if (s.manager == nullptr)
        return;
    Log().info("effekseer: the device goes: {} effect(s) released", s.effects.size());
    s.manager->StopAllEffects();
    s.effects.clear();
    s.by_path.clear();
    s.paths.clear();
    s.failed.clear();
    s.manager.Reset();
    s.textures.Reset();
    s.renderer.Reset();
}

} // namespace storm::render::effekseer
