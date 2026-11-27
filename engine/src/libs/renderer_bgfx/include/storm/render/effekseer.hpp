// Effekseer effects (.efkefc) played and drawn by the renderer; design in renderer_bgfx/effekseer/README.md. Game
// thread only. Coordinates follow the engine: left-handed, D3D row-vector matrices, angles in radians.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::render::effekseer
{

// A whole game file (case-insensitive game path, '/' separators), or nothing when it is missing or unreadable.
using FileReader = std::function<std::optional<std::vector<uint8_t>>(const std::string &path)>;
// The engine's file service; set before the first effect is loaded.
void SetFileReader(FileReader reader);

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

using EffectId = uint32_t;  // 0: none
using InstanceId = int32_t; // Effekseer's handle
inline constexpr InstanceId kNoInstance = -1;

// Loads an effect file (a game path such as "resource/effects/CLTorchFire.efkefc") with its textures, models and
// curves, or returns the one already loaded. 0 when it cannot be loaded: the reason is logged once per path.
EffectId LoadEffect(std::string_view path);
EffectId FindEffect(std::string_view path);
// Reads the effect and its resources again; running instances keep playing. False (logged) when it fails.
bool ReloadEffect(EffectId effect);
// Removes its instances and releases it.
void UnloadEffect(EffectId effect);
void UnloadAllEffects();

// Plays from `start_frame` (Effekseer's frames, 60 a second), turned by `rotation` and scaled by `scale`.
InstanceId Play(EffectId effect, const Vec3 &position, const Vec3 &rotation = {}, const Vec3 &scale = {1, 1, 1},
                int32_t start_frame = 0);
bool Exists(InstanceId instance);
void Stop(InstanceId instance);     // removes the instance at once
void StopRoot(InstanceId instance); // stops spawning; what lives finishes
void StopAll();
void SetLocation(InstanceId instance, const Vec3 &position);
Vec3 GetLocation(InstanceId instance);
void AddLocation(InstanceId instance, const Vec3 &offset);
void SetRotation(InstanceId instance, const Vec3 &angles); // Effekseer's XYZ order
void SetScale(InstanceId instance, const Vec3 &scale);
void SetTargetLocation(InstanceId instance, const Vec3 &target);
void SetSpeed(InstanceId instance, float speed);
float GetSpeed(InstanceId instance);
void SetPaused(InstanceId instance, bool paused);
void SetPausedAll(bool paused);
void SetColor(InstanceId instance, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void SetDynamicInput(InstanceId instance, int32_t index, float value);
float GetDynamicInput(InstanceId instance, int32_t index);
void SendTrigger(InstanceId instance, int32_t index);

// Advances every instance (Effekseer counts 60 frames a second) and swaps finished texture transcodes in.
void Update(float seconds);
// Draws every instance into the game's current render target and viewport, after what the game drew so far, with the
// D3D camera (`view`, `projection`: row-vector matrices).
void Draw(const float view[16], const float projection[16]);

// What an effect uses that this renderer does not show (the rest of it is drawn).
struct EffectInfo
{
    int32_t nodes = 0;            // drawable nodes
    int32_t missing_textures = 0; // referenced, not loaded
    int32_t missing_models = 0;
    int32_t sounds = 0;              // not played: no sound backend
    int32_t distortion_nodes = 0;    // not drawn: no copy of the scene behind them
    int32_t soft_particle_nodes = 0; // drawn with hard edges: no copy of the scene depth
    int32_t material_nodes = 0;      // not drawn: .efkmat materials are not supported
    int32_t gpu_particle_nodes = 0;  // not drawn: no GPU particle backend
};
std::optional<EffectInfo> GetEffectInfo(EffectId effect);

struct Stats
{
    uint32_t effects = 0;       // loaded
    int32_t instances = 0;      // live particle instances
    int32_t draw_calls = 0;     // of the last Draw
    uint32_t textures = 0;      // loaded
    uint64_t texture_bytes = 0; // their GPU memory
    uint32_t textures_from_cache = 0;
    uint32_t textures_decoded = 0;
    uint32_t textures_encoding = 0; // shown uncompressed until their compressed copy is made
};
Stats GetStats();

} // namespace storm::render::effekseer
