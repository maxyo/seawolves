// Graphics effects (renderer_bgfx/fx) as seen by the engine: options, the scene boundary and the per-frame
// inputs the renderer cannot observe through the D3D9-shaped shim. Game thread only.
//
// Presets: «Низкая» (0, the legacy image: no effect runs and the frame is composed exactly as
// before), «Средняя» (1), «Высокая» (2). The fx_* options are developer overrides (engine.ini), not player settings.
// What the device cannot do is switched off once, when the options are applied, and reported (log +
// EffectsReport::changes); nothing is lowered silently later.
#pragma once

#include "storm/environment/state.hpp"
#include "storm/fx/aggregate_light.hpp"

#include <storm_glm.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::shim
{
struct BaseTexture;
}

namespace storm::render
{

// One option as the engine reads it: from engine.ini (host) or the app's graphics preferences (Android),
// always an integer. Unknown names are an error in the report.
struct EffectsOption
{
    std::string name;
    int value = 0;
};

// Every option name with its meaning; `preset_default` = -1 means "use the preset's value".
struct EffectsOptionInfo
{
    const char *name;
    const char *values; // e.g. "0 off, 1 on" or "1, 2, 4"
};
const std::vector<EffectsOptionInfo> &EffectsOptionNames();

struct EffectsReport
{
    int preset = 0;                    // 0 Низкая, 1 Средняя, 2 Высокая
    std::string summary;               // the effective settings in one line
    std::string support;               // what the device supports (fx::Support::Describe)
    std::vector<std::string> changes;  // options the device cannot honour, switched off (one line each)
    std::vector<std::string> errors;   // unknown option names / values
    std::vector<std::string> disabled; // option names the settings screen must grey out on this device
    // The request does not draw yet: its shader programs are being linked (a few per frame) and its GPU self-test
    // run while the previous settings keep drawing. Becomes false at the Present that switches.
    bool pending = false;
    uint32_t prepared_programs = 0; // while pending: the request's programs prepared so far
};

// Applies `preset` and `options` (overrides). Safe before the device exists: the request is kept and
// constrained when the device is created. On a live device the previous settings keep drawing until the new ones
// are prepared (EffectsReport::pending); the device itself starts on «Низкая» and switches the same way.
// Returns the report of the request.
EffectsReport ConfigureEffects(int preset, const std::vector<EffectsOption> &options);
EffectsReport CurrentEffects();
// The facts of the running game the effects need (core's game profile), before the first ConfigureEffects.
struct EffectsGame
{
    // False for a game that ships its own texture maps: no normal, gloss or height map and no neural ×2 texture is
    // made or requested, whatever the texture_* options say (the settings screen does not offer them).
    bool generates_texture_maps = true;
};
void ConfigureEffectsGame(const EffectsGame &game);
// A one-line notice for the player (Russian) when the GPU self-test switched a stage off on this device; empty
// when there is none. Each notice is returned once.
std::string TakeEffectsNotice();

// The 3D scene of this frame is complete; what follows is the legacy interface (Renderer::MakePostProcess,
// called by every interface before it draws). The post chain runs here, on the scene only.
void EffectsSceneComplete();

// The game's underwater screen wobble of this frame (script Render.SeaEffect, SeaEffectSize, SeaEffectSpeed), set by
// the renderer at the start of every frame. It is the legacy look and runs on every preset.
struct EffectsWobble
{
    bool on = false;
    float amplitude = 0.0f; // wave height, fraction of the picture size
    float phase = 0.0f;     // radians, advanced by the renderer at SeaEffectSpeed radians per second
};
void SetEffectsWobble(const EffectsWobble &wobble);

// The scene environment of this frame, set by the WEATHER entity every frame it runs. A frame without it has no sky
// (interiors): sun effects are off and the scene grade is the interior one. The scene camera is taken from the draws
// themselves (the last perspective draw into the game picture).
void SetSceneEnvironment(const environment::State &state);
// Draws the sky of the frame's environment into the current target with the device's current camera, behind what is
// already drawn: in the scene before its geometry and into the sea's reflection.
void DrawSky();
// The drawn sky's colour at the horizon (linear, on the display's scale), for fog that takes the sky's colour.
glm::vec3 SkyHorizon();

// While CurrentEffectsWorld().lens_flare: how much of the sun the camera sees this frame (0..1: SUNGLOW's traced
// visibility past terrain, buildings and sails, with its fade at the horizon), and how strong the flare ghosts are
// (0..1: the weather's Sun.Flare and SUNGLOW's fade as the sun leaves the view). The fx sun glare is drawn with
// them; a frame without the call has no glare.
void SetEffectsSunGlare(float glare, float ghosts);

// The technique block CTechnique executes (lower case), nullptr when its passes are closed. The per-pixel lighting
// picks the material from it (characters, sails, ...).
void SetEffectsTechnique(const char *name);

// Renderer, whenever its camera moves: the game's Renderer keeps the device camera-relative (the D3D view has no
// translation; every world matrix and D3D light is shifted by minus the camera position, for float precision far
// from the origin). `origin` is that camera position: absolute world = device world + origin. Every effect that meets
// absolute world data (the location's geometry, lamps, dynamic lights, lightmaps) converts with it.
void SetEffectsWorldOrigin(const glm::vec3 &origin);

// The actual controlled character/main ship, in absolute world coordinates: prioritises pending texture generation.
// Separate from the camera (third-person, dialogue and free-camera views must not move the priority anchor).
// `owner` is identity only; clearing an old owner must not clear its replacement's position. On land the main
// character's ship moored in the port also reports: the controlled character outranks it (`role`).
enum class EffectsPlayerRole : uint8_t
{
    Ship,
    Character,
};
void SetEffectsPlayerPosition(const glm::vec3 &position, const void *owner, EffectsPlayerRole role);
void ClearEffectsPlayerPosition(const void *owner);

// The legacy sea (Sea2), every frame it draws, before its draws: the hulls on it this frame, which leave their
// wakes (Settings::sea_wakes) and which the sea's reflections follow.
struct EffectsWaveSource
{
    // The hull's waterline this frame: kHullStations stations evenly spaced from `stern` to `stem` (metres along
    // `forward` from `position`), each with the waterline's centre offset to the side (towards (-forward z, forward x))
    // and its half beam there.
    static constexpr int kHullStations = 8;
    uint64_t identity = 0;                    // stable entity id
    glm::vec3 position{};                     // absolute world: the ship's origin
    std::array<float, 2> forward{0.0f, 1.0f}; // unit heading in x, z
    float half_length = 0.4f;                 // metres: half the waterline's length
    float half_width = 0.4f;                  // the largest half beam at the waterline
    float stern = -0.4f, stem = 0.4f;
    std::array<float, kHullStations> station_side{};
    std::array<float, kHullStations> station_half{};
    float speed = 0.0f; // m/s over the water along `forward`
};
struct EffectsSeaFrame
{
    glm::vec3 vertex_origin{}; // absolute world of the sea vertices' origin (the sea's camera offset)
    glm::vec3 camera{};        // absolute world
    // Absolute world point on the water the camera looks at (at most some tens of metres away): the wakes' target
    // is centred there, so the ship the camera follows stays well inside it.
    glm::vec3 focus{};
    float dt = 0.0f; // game seconds since the previous frame (0 while paused)
    std::span<const EffectsWaveSource> sources;
};
void SetEffectsSeaFrame(const EffectsSeaFrame &frame);

// Point lights of this frame that no baked vertex colours contain (lanterns and torches carried by characters), world
// space, D3DLIGHT9 attenuation. Static location geometry, drawn with its baked colours, is lit only by these; the
// nearest to the camera cast shadows (fx_lamp_moving). A frame without the call has none.
struct EffectsPointLight
{
    glm::vec3 position{};
    glm::vec3 color{};
    float range = 10.0f;
    float att0 = 0.0f, att1 = 0.0f, att2 = 1.0f;
};
void SetEffectsDynamicLights(std::span<const EffectsPointLight> lights);

// A short-lived light at a shot or a blast, `position` absolute world (game thread, at the event).
// The renderer gives it its kind's colour, range and envelope (fx/light_set.hpp FlashProfileOf: a ~30 ms rise, a
// 0.15..0.4 s decay), lights lit and baked draws with it per pixel without a shadow, and keeps the strongest ones near
// the camera up to the preset's cap (Средняя 4, Высокая 8; option fx_flash_lights), fading those it drops. Ignored
// while CurrentEffectsWorld().flash_lights is false («Низкая»: the original game has no such lights).
enum class EffectsFlashKind : uint8_t
{
    Gunshot,       // pistol or musket ("gunfire" particles)
    Cannon,        // a cannon of a ship or a fort
    BallBlast,     // a ball bursting ("blast")
    Explosion,     // powder keg, blown cannon, quest explosion ("blast_inv", "town_explodion")
    ShipExplosion, // a ship blowing up ("ShipExplode")
};
void EffectsFlash(EffectsFlashKind kind, const glm::vec3 &position);

// Raw immutable metadata is shaped by the renderer, using the current settings.
std::shared_ptr<const fx::AggregateSnapshot> ShapeEffectsAggregates(fx::AggregateSnapshot snapshot);
bool EffectsAggregateLightsAvailable();
void SetEffectsLightAggregate(uint32_t index, const fx::AggregateRef &ref);
// Location, every frame it draws: its still lamps (lights.ini at its locators, grouped into fixtures). The ones
// around the camera relight the location per pixel, each with its shadow (fx_lamp_shadows).
void SetEffectsAggregateLamps(std::shared_ptr<const fx::AggregateSnapshot> snapshot);
// Authoritative location components, already grouped before receiver budgets. Keep every original flame
// with its exact component index for fixture self-shadow exclusions; do not reconstruct it from a centroid radius.
struct EffectsLampFlame
{
    glm::vec3 position{};
    uint32_t component = 0;
};

// Location, once it is loaded: its still geometry as it is drawn (world space, triangle list), for everything the
// effects cast or bake from it: the lamps' shadows, the room light, the place's probes (its visibility volume, in an
// interior the ambient-occlusion volume; read from the cache or baked on the bake threads). When leaving: an empty one.
struct EffectsLocationGeometry
{
    std::string_view name; // names it in the log
    std::span<const glm::vec3> positions;
    // One D3DCOLOR per position: the baked lighting (in an interior it is the light its lit models stand in).
    std::span<const uint32_t> colors;
    // The first texture coordinates per position, and the runs of `indices` drawn with one texture: where a texture's
    // alpha cuts holes (foliage, nets, gratings) the shadows and the occlusion have them too. Indices outside every
    // run count as solid.
    std::span<const std::array<float, 2>> uvs;
    std::span<const uint32_t> indices;
    struct Surface
    {
        uint32_t first_index = 0, index_count = 0;
        shim::BaseTexture *texture = nullptr; // alive during the call: the effects hold what they keep of it
    };
    std::span<const Surface> surfaces;
    // What the place is: most of where one walks lies under a roof (measured by the location from its walk patch
    // and collision data; rooms, caves, a ship's hold and cabins, whatever the scripts say of its weather).
    // room_lo..room_hi is then the box one walks in, with room for the walls and the ceiling: the room light and the
    // place's probes are laid over it (the geometry may reach far beyond and only occludes there). What a pixel gets of
    // the sun, the sky and the room's occlusion is decided per pixel by the visibility volume, not by this.
    bool interior = false;
    glm::vec3 room_lo{}, room_hi{};
    // Points on the floor where one walks (from the walk patch, a few hundred): the AO volume is measured against
    // what that floor sees, so a narrow place does not go dark as a whole.
    std::span<const glm::vec3> walk;
};
void SetEffectsLocationGeometry(const EffectsLocationGeometry &geometry);

// SHIP_LIGHTS, every frame at sea, after the ships have moved and before the first of them is drawn: the ships that
// carry lit lanterns and those lanterns (world space, where the ships are drawn in this frame, with this frame's
// flicker; not the flashes of the cannons). The nearest to the camera take the lamp slots of the lights that
// move (fx_lamp_moving). A lantern stands still on its ship, so the shadows of the ship itself (hull, masts, deck)
// are drawn once into faces in the ship's own space and stay while the ship sails; the crew, the sails and other
// ships cast every frame. A frame without the call has none.
struct EffectsShip
{
    uint64_t id = 0;               // names the ship while it lives (not 0)
    std::array<float, 16> world{}; // ship space -> absolute world (row vectors), rigid
    uint64_t signature = 0;        // changes when the ship's still geometry does (a mast falls, the lanterns change)
};
struct EffectsShipLight
{
    EffectsPointLight light;
    uint64_t ship = 0; // an EffectsShip::id of the same call
    glm::vec3 local{}; // the lantern in the ship's space
};
void SetEffectsShipLights(std::span<const EffectsShip> ships, std::span<const EffectsShipLight> lights);
// True when one of the ship's lanterns holds a slot and the effects do not have the ship's still geometry of this
// signature: the ship then reports it (SetEffectsShipGeometry) before the frame draws.
bool EffectsShipGeometryWanted(uint64_t ship, uint64_t signature);
// The ship's still geometry in its own space (triangle list; uvs and surfaces as in EffectsLocationGeometry, the
// textures alive during the call). Empty: it could not be read; the ship casts nothing still.
struct EffectsShipGeometry
{
    std::span<const glm::vec3> positions;
    std::span<const std::array<float, 2>> uvs;
    std::span<const uint32_t> indices;
    std::span<const EffectsLocationGeometry::Surface> surfaces;
};
void SetEffectsShipGeometry(uint64_t ship, uint64_t signature, const EffectsShipGeometry &geometry);
// The ship's model is drawn inside a render::ShadowCasterStill of its id (storm/render/shadow_casters.hpp): it is in
// the cached faces of the ship's own lanterns and casts every frame only for other lights.

// Location, once an outdoor location is loaded: its lightmapped triangles (the still models with a shadow.tga
// lightmap: world positions, lightmap texture coordinates, triangle list, the lightmap of each triangle) and those
// lightmaps (one mip level of each, as stored: DXT1/3/5, and the game's texture of it). On Средняя / Высокая our own
// lightmaps take their place in the draws: the sun's shadows of the hour and the sky's occlusion from the place's
// still geometry (SetEffectsLocationGeometry), in the game lightmaps' tone (effects/town_light.hpp). Interiors and when
// leaving: empty.
struct EffectsLightmap
{
    uint32_t width = 0, height = 0;
    std::array<char, 4> format{}; // the .tx format tag: 'D','X','T','1' / '3' / '5'
    std::vector<uint8_t> data;
    shim::BaseTexture *texture = nullptr; // the game's texture of this lightmap (alive while the location is)
};
void SetEffectsLightmaps(std::string_view name, std::span<const glm::vec3> positions,
                         std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                         std::span<const uint32_t> triangle_lightmap, std::span<const EffectsLightmap> lightmaps);

// The texture's name and the game file it was loaded from (Renderer::TextureLoad, after its levels are filled). Stone,
// wood, plaster and ground textures and grey height ("bump") textures get a normal map for the per-pixel lighting's
// relief (fx_light_normal_map, fx/normal_map.hpp, src/effects/relief_generator.hpp). An empty `file`: a texture that
// is no .tx file, which the relief and the ×2 do not read.
void SetEffectsTextureSource(shim::BaseTexture *texture, const char *path, const std::filesystem::path &file);

// Relief maps (fx/relief_store.hpp): made by PBRify's models in the game process the first time a texture needs one,
// and the height maps the game derives; kept in `directory`. Before the first texture is loaded.
struct ReliefConfig
{
    std::filesystem::path directory;
    std::filesystem::path textures; // the game's RESOURCE/Textures (the generator reads the texture files)
    // Reads a model file by its name (src/libs/relief/model/README.md, relief.hpp kModelFiles: the ncnn graphs). Null:
    // no maps are made (logged); textures without one get no relief.
    std::function<std::vector<uint8_t>(const char *file)> model;
    // The neural ×2 textures (fx/upscale.hpp), derived like the maps: in the cache root too.
    std::filesystem::path upscale_directory;
};
void ConfigureRelief(const ReliefConfig &config);
// The neural ×2 textures have settled (effects/upscale.hpp): with the option on, the last residency plan found every
// texture it chose resident at ×2 (none still being read, uploaded or made). The scene sweep reports it with each
// variant's capture and waits for it when the variant asks (wait=upscale).
bool UpscaleSettled();
// Whether this device can show the neural ×2 textures («Апскейл текстур ×2»): unknown until the renderer read
// the GPU's caps and the upscale directory (the first texture); unsupported without ETC2 or BC textures (the ×2 would
// be held uncompressed).
enum class UpscaleSupport
{
    Unknown,
    Supported,
    Unsupported,
};
UpscaleSupport UpscaleAvailability();
// A bake (town light, visibility, bounce, AO volume, far LOD) is queued or running (fx/bakes.hpp). Reported, and waited
// for on request, by the scene sweep (wait=bakes).
bool BakesBusy();
// Before the renderer goes: stops the generator (the texture in progress is abandoned, its map made next time).
void ShutdownRelief();
// Any thread, the system low on memory (Android onTrimMemory / onLowMemory): the relief's models are released after
// the texture in progress and the GPU's Vulkan instance closed (logged); they load again when work comes.
void TrimRelief();
// Game thread, the system low on memory: releases the render targets nothing draws into now (post-chain passes not
// in use, the interface's idle layers and texture-job targets); they are made again by their next use. Logged with
// the bytes, which it returns.
uint64_t ReleaseIdleRenderTargets();
// Main thread: holds the background work, the relief generation and the ray bakes, while `why` is not null (the
// automatic graphics preset measures the GPU); null releases it. Work already started finishes its piece: a relief
// map in progress is made to its end (BackgroundWorkBusy), a bake stops at its next piece. Logged on change.
void HoldBackgroundWork(const char *why);
// A relief map (or the list of the game's relief textures) is being made now.
bool BackgroundWorkBusy();
// The device's heat as the thermal lines log it (status, headroom now and ahead, battery temperature, the load).
std::string ThermalReading();
// The relief maps of the loaded textures (the "effects relief:" line's counts); a scan of every entry, for the bench.
struct ReliefMaps
{
    uint32_t resident = 0;  // normal maps uploaded
    uint32_t materials = 0; // of them with their material texture (height, gloss)
    uint32_t waiting = 0;   // textures a draw read whose map is still being made or requested
    bool working = false;   // the generator prepares a texture now
    std::string paused;     // why the generator waits (heat, the renderer's hold); empty: it may run
};
ReliefMaps CurrentReliefMaps();

// ---- World passes ----
// The consumer entity (SUNGLOW) asks which parts are on and then either leaves its own drawing to the fx pass or
// keeps it. Changes only at the Present that switches settings (EffectsReport::pending); everything false in «Низкая».
struct EffectsWorld
{
    bool lens_flare = false; // the post chain draws the sun glare: SUNGLOW leaves its glow, overflow and flare
                             // sprites of the sun out and reports the sun's visibility (SetEffectsSunGlare)
    // Shots and explosions light their surroundings through EffectsFlash; ShipLights then leaves its own D3D cannon
    // light out (the original's, lighting only the ships, which «Низкая» keeps).
    bool flash_lights = false;
};
EffectsWorld CurrentEffectsWorld();

// `view` of the world passes is the world -> view matrix as the engine sees it (VRenderer::GetTransform(D3DTS_VIEW)):
// the device itself holds a camera-relative view, because Renderer moves the camera translation into the world matrix.
using EffectsMatrix = std::array<float, 16>;

} // namespace storm::render
