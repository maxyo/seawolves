// Runtime switches of the graphics effects package.
//
// Every field may be changed between frames; the passes read the struct each
// frame and (re)create GPU resources lazily. `QualityPreset::Low` turns
// every effect off: the post chain degenerates to a plain copy and the legacy
// sea / sky / fixed-function lighting paths are used unchanged.
#pragma once

#include "storm/fx/light_falloff.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace storm::fx
{

// Sampler stages fx binds into the fixed-function emulator's programs (FX_LIGHT_SHADOW, FX_LIGHT_NORMALMAP).
// They are above the emulator's own units (stages 0..4, cube maps 5..7) except 10..12: the shim also maps volume
// textures (8..10, 12) and a stage-3 cube (11) there, which only ported pixel shaders read; the fx lit / baked
// variants never run a port and bind their own textures to those units after the shim, so every combination fits
// in the 16 units of GLES 3.0.
inline constexpr uint8_t kShadowMapStage = 15; // 13 is the renderer's bone palette (vertex shader)
// The location's visibility volume (fx/visibility.hpp): the unit of the stage 0 cube map, which no lit or baked
// variant has (a cube-mapped fixed-function draw keeps vertex lighting: Lighting::prepare).
inline constexpr uint8_t kVisibilityStage = 5;
inline constexpr uint8_t kAoVolumeStage = 12;    // the interior's AO volume (RGBA8 3D, four lobes)
inline constexpr uint8_t kLampDynamicStage = 10; // lamp shadow atlases (characters / location)
inline constexpr uint8_t kLampStaticStage = 11;
inline constexpr uint8_t kRoomLightStage = 8; // the baked colours around every point of an interior (RGBA8 3D)
inline constexpr uint8_t kLampGridStage = 9;  // which lamps reach which cell of the place (RGBA8 3D)
inline constexpr uint8_t kNormalMapStage = 14;
// The material texture (EAC RG11: R the relief's height for the parallax and its cavity, height_map.hpp; G gloss,
// gloss_map.hpp) in the lit and baked programs: unit 7 is the stage-2 cube of the plain fixed-function cube variants,
// which those programs never are (cube-mapped draws keep the fixed-function path).
inline constexpr uint8_t kMaterialStage = 7;
// The point lights' and lamps' aggregate payload (fx_lighting.sh s_fxAggregate): the unit of the stage 1 cube map.
inline constexpr uint8_t kAggregateStage = 6;
// Every unit above, with the emulator's own stages 0..4 (and the renderer's bone palette at 13, shim/device.hpp),
// is claimed once: src/effects/lighting.cpp checks at compile time that no two claims meet (FfUnitsDistinct).
inline constexpr uint8_t kFxStages[] = {kVisibilityStage, kAggregateStage,   kMaterialStage,   kRoomLightStage,
                                        kLampGridStage,   kLampDynamicStage, kLampStaticStage, kAoVolumeStage,
                                        kNormalMapStage,  kShadowMapStage};

enum class QualityPreset : uint8_t
{
    Low,    // «Низкая»: the legacy look, no effect runs (parity with the original renderer); weak phones
    Medium, // «Средняя»: Mali-G52 / Adreno 610 class at 30 fps
    High,   // «Высокая»: Adreno 640+ / Mali-G76+ class
};

// Colour grade, independent of the quality preset.
enum class GradePreset : uint8_t
{
    Original,  // «Оригинал»: identity look-up table
    Caribbean, // «Карибы»: warm, saturated
    Overcast,  // «Пасмурно»: cool, low saturation, softer contrast
};

// Bits understood by shaders/include/fx_lighting.sh (see lighting.hpp).
enum LightingFlags : uint32_t
{
    kLightingNone = 0,
    kLightingPerPixel = 1u << 0,    // FX_LIGHT_PER_PIXEL: sun N·L evaluated per pixel
    kLightingHemisphere = 1u << 1,  // FX_LIGHT_HEMISPHERE: sky/ground ambient instead of a flat one
    kLightingSpecular = 1u << 2,    // FX_LIGHT_SPECULAR: Blinn-Phong highlight
    kLightingTranslucent = 1u << 3, // FX_LIGHT_TRANSLUCENT: back-lit thin surfaces (sails, flags)
    kLightingPoint = 1u << 4,       // FX_LIGHT_POINT: point and spot lights evaluated per pixel
    kLightingNormalMap = 1u << 5,   // FX_LIGHT_NORMALMAP: relief from normal maps derived from the game's textures
    kLightingRim = 1u << 6,         // FX_LIGHT_RIM: rim light on characters and ships seen against the sun
    kLightingLinear = 1u << 7,      // FX_LIGHT_LINEAR: lights are added in linear light instead of gamma space
    kLightingAerial = 1u << 8,      // FX_AERIAL: aerial perspective and height fog instead of the plain fog
    kLightingAll = 0x3ff,
};

// Parallax from the relief's height (Settings::parallax, height_map.hpp) on the classes that have one.
enum class ParallaxQuality : uint8_t
{
    Off,
    Offset,    // one tap: offset mapping with offset limiting
    Occlusion, // parallax occlusion mapping: 8 steps looking straight at a surface, up to kParallaxSteps at grazing
};
inline constexpr int kParallaxSteps = 24;

// The relief of the characters (Settings::character_relief): their clothes, armour and gear, faded out on skin and hair
// by the skin weight of the vertices (the head, neck and hand bones: animation skin_bones.hpp).
enum class CharacterRelief : uint8_t
{
    Off,
    Normals,  // the normal maps only
    Parallax, // and a shallow parallax (Settings::parallax's quality, no self-shadow)
};

enum class ShadowQuality : uint8_t
{
    Off,
    Low,  // 1024² map; the still geometry's layer one hardware-filtered tap, the moving casters' a 3x3 tent (four taps)
    Soft, // 2048² map; four taps on either layer (the moving casters' the same 3x3 tent)
};

// The occlusion of the interior AO volume against its level's own (Settings::ao_gain).
inline constexpr float kAoGain = 1.25f;
// Strengths of the interior AO volume (Settings::ao_volume). The shader darkens by mix(1, pow(ao, contrast), strength),
// ao the baked visibility relative to an open floor or wall.
inline constexpr uint8_t kAoVolumeLevels = 3;
struct AoVolumeLevel
{
    const char *name;
    float strength;
    float contrast;
};
inline constexpr AoVolumeLevel kAoVolumeLevel[kAoVolumeLevels + 1] = {
    {"off", 0.0f, 1.0f},
    {"as the game's data", 0.6f, 1.0f},
    {"moderate contrast", 0.85f, 1.4f},
    {"strong stylised contrast", 1.0f, 1.7f},
};

// The presets' internal scene heights (Settings::render_height): non-decreasing from «Низкая», the fastest.
inline constexpr uint16_t kLowRenderHeight = 900;
inline constexpr uint16_t kMediumRenderHeight = 1000;
inline constexpr uint16_t kHighRenderHeight = 1000;

// The value ranges of the developer options are in src/effects/effects.cpp kOptions.
struct Settings
{
    // ---- post chain ----
    bool tonemap = false;          // AgX tone curve on the luminance (fx_tonemap.sh)
    float exposure = 1.0f;         // linear multiplier before the tone curve
    float tonemap_strength = 0.6f; // 0 = untouched .. 1 = the full AgX curve
    GradePreset grade = GradePreset::Original;
    float grade_strength = 1.0f; // 0 = identity .. 1 = full preset
    // The player's own warmth (-1 cool .. +1 warm) and saturation (1 as it is) over the grade: a game's options.
    float grade_temperature = 0.0f;
    float grade_saturation = 1.0f;
    bool bloom = false;
    uint8_t bloom_downscale = 4;   // 2 = bloom chain starts at half, 4 = at quarter resolution
    uint8_t bloom_levels = 4;      // dual-filter down/up steps (1..6)
    float bloom_threshold = 0.85f; // linear value of the brightest channel above which a pixel glows
    float bloom_knee = 0.12f;      // glow fades in from threshold * (1 - knee) and is full at white
    float bloom_intensity = 0.35f;
    bool smaa = false; // SMAA 1x (post anti-aliasing), where MSAA is unavailable (ConstrainSettings)
    // The 3D scene's internal height on displays taller than this (FSR 1 EASU up to the display, interfaces at the
    // display size); 0 = native everywhere. RenderScale() gives the factor for a display.
    uint16_t render_height = 0;
    // Far LODs (src/libs/geometry/far_lod): the largest error a simplified level may show, in pixels of the target;
    // 0 = every model whole. Not a look: on every preset.
    float far_lod_pixels = 0.f;
    bool sharpen = false;   // FSR 1 RCAS after the EASU upscale
    float sharpness = 0.5f; // 0..1 RCAS strength, 1 = the reference's maximum
    bool sun_shafts = false;
    float sun_shafts_intensity = 0.35f;
    bool dither = false;     // ±0.5/255 ordered noise on the sky and the final image: no banding
    bool lens_flare = false; // procedural sun glare and flare ghosts (the legacy flare sprites are left out)
    float lens_flare_intensity = 0.5f;
    bool scene_grading = false; // PostChain::Frame::scene_grade (mood of the place and hour)

    // ---- renderer state (RendererStateFor) ----
    uint8_t msaa = 1;               // scene target samples: 1 (off), 2 or 4; the primary anti-aliasing on tile GPUs
    bool alpha_to_coverage = false; // smooth edges of alpha-tested geometry; needs msaa > 1
    uint8_t anisotropy = 1;         // 1 (off), 4 or 8: anisotropic filtering of world textures
    bool depth_prepass = false;     // opaque scene draws lay their depth first: each pixel is shaded once

    // ---- world passes ----
    // The game's sea (the ported Sea2 technique, fs_port_sea2.sc); its reflected ray is always clamped to the horizon.
    bool sea_band_limit = false; // the bump detail fades with the pixel footprint: no shimmer at the horizon
    bool sea_soft_edges = false; // the sea fades out where shallow in front of the scene (needs the scene depth)
    bool sea_turbidity = false;  // depth-dependent transmission and water-colour scattering
    bool sea_specular = false;   // the sun's glint from the bumped normal along the game's sun road
    float sea_edge_depth = 1.5f; // metres of water over which a soft edge fades in
    float sea_specular_power = 400.0f;
    float sea_specular_strength = 0.6f;
    // The wakes of the hulls (sea_wakes.hpp): foam astern and at the bow.
    bool sea_wakes = false;
    uint32_t lighting_flags = kLightingNone;
    uint8_t point_lights = 4; // per draw with kLightingPoint: 4 or 8
    uint8_t flash_lights = 0; // shots and explosions lit at once, 0..8 (needs kLightingPoint; at most half a draw's)
    float normal_map_strength = 1.0f; // with kLightingNormalMap: strength of all relief
    float light_saturation = 1.0f;    // saturation of added per-pixel light, preserving luminance
    float sail_light = 1.0f;          // with kLightingTranslucent: sunlight through sails and flags
    uint16_t normal_map_size = 512;   // with kLightingNormalMap: larger side of the resident normal maps
    // With kLightingSpecular: per-texel gloss maps shape the sun's and the lamps' highlights on lit and baked surfaces
    // (gloss_map.hpp). Off: the lit materials' fixed highlight only.
    bool gloss = false;
    // Neural ×2 textures near the camera (upscale.hpp). No preset sets it: «Апскейл текстур ×2» does
    // (ApplyTextureProcessing).
    bool upscale = false;
    uint16_t upscale_budget_mb = 0; // with upscale: their GPU memory, 0 = UpscaleBudgetBytes of the device's RAM
    float gloss_strength = 1.0f;    // with gloss: the share of the gloss maps' highlights (0..3)
    uint16_t gloss_map_size = 256;  // with gloss: larger side of the resident gloss maps
    // With kLightingNormalMap: parallax from the relief's height on the classes that have one, fading out at 10-15 m.
    ParallaxQuality parallax = ParallaxQuality::Off;
    float parallax_strength = 1.0f; // scales every class's depth (height_map.hpp ReliefDepthTexels), 0..3
    bool parallax_shadow = false;   // with ParallaxQuality::Occlusion: the relief's own shadow towards the sun
    bool relief_cavity = false;     // with kLightingNormalMap: the height darkens joints, gaps and recesses
    // With kLightingNormalMap: relief on the characters at character_relief_strength of the relief strength (their
    // painted textures read stronger relief than stone or wood).
    CharacterRelief character_relief = CharacterRelief::Off;
    float character_relief_strength = 0.5f;
    float character_parallax_depth = 1.0f; // with CharacterRelief::Parallax: against their class's depth, 0..10
    bool character_skin_colour = true;     // torso and arm texels in the skin's colour count as skin (skin_tone.hpp)
    ShadowQuality shadows = ShadowQuality::Off; // sun shadows on per-pixel lit surfaces (needs kLightingPerPixel)
    bool town_light = false; // towns relit for the live sun (needs kLightingPerPixel; src/effects/town_light.hpp)
    bool town_walls = false; // with town_light: walls and props without a lightmap relit the same way
    bool visibility = true;  // the location's visibility volume (visibility.hpp) for the lit draws
    uint8_t ao_volume = 0;   // interiors' ambient-occlusion volume: 0 off, else an AoVolumeLevel
    float ao_gain = kAoGain; // what the AO volume takes, times this
    // Lamps that relight a location with their shadows (lamp_shadows.hpp): how many around the camera, 0, 8 or 16.
    // Interiors also light their lit models with the room's baked light.
    uint8_t lamp_shadows = 0;
    // Lights in gamma space, per unit of the receivers' strongest gain:
    float lamp_soft_light = 24.0f / 255.0f;  // with lamp_soft: under it the strongest lamp takes the 3x3 filter
    float lamp_reach_light = 0.5f / 255.0f;  // where a lamp ends (its grid cells and casters; fx::kLampNegligible)
    float lamp_shadow_light = 0.5f / 255.0f; // under it a lamp is not checked against its shadow
    bool lamp_soft = false;       // a 5x5 instead of a 3x3 filter for the strongest lamp's still-geometry shadows
    bool lamp_edges = false;      // the still faces' edge maps: a filter reads them only at a shadow's edge
    uint8_t lamp_per_pixel = 8;   // the most lamps that light one pixel with their shadows (1..kLampGridCapacity)
    uint8_t lamp_filtered = 8;    // of those, how many take filtered shadows; the others one hardware-filtered tap
    uint8_t lamp_moving = 0;      // slots kept for carried lanterns and torches, 0, 1 or 2
    float lamp_gain = 0.5f;       // the lamps' strength on baked surfaces (0..3), near the game's own look
    float lamp_tint = 0.0f;       // the share of a lamp's own colour in what it adds to baked surfaces (0..1)
    float lamp_baked = 0.25f;     // the part of the lamp light the baked colours already hold (0..1)
    float lamp_model_lift = 0.2f; // indoors the lit models stand this much above the room light around them
    // Rain wetness outdoors where the rain reaches (wetness.hpp, ff_wet.sh): 0 off, 1 wet, 2 also puddles in the
    // relief's hollows.
    uint8_t wet_surfaces = 0;
    float wet_force = -1.0f;      // development: 0..1 = this wetness outdoors whatever the weather, < 0 = the rain's
    LightFalloff light_falloff{}; // the one falloff of every light (light_falloff.hpp); the same on every preset
    // A fixed-function draw whose stage setup has a specialised program (shaders/ff_programs.cmake) takes it; the
    // same picture as its general program, without the stage interpreter. Every preset.
    bool ff_specialised = true;

    static Settings FromPreset(QualityPreset preset);

    // True when the post chain would not change a single pixel (given equal input and output size).
    bool PostIsIdentity() const;

    bool operator==(const Settings &) const = default;
};

// The products the renderer makes in the background (worker threads, the GPU between frames) for these settings:
// the one place that says which bake or texture generation runs. Each runs only while the settings read what it makes;
// switching a product off cancels its queued and running work (the renderer's gates, effects/), switching it on
// starts it for what is loaded. «Низкая» needs the far LODs only.
struct BackgroundProducts
{
    bool far_lods = false;     // the simplified far models (far_lod_pixels)
    bool visibility = false;   // the location's visibility volume (lit draws: per-pixel or point lighting)
    bool ao_volume = false;    // an interior's AO volume
    bool town_light = false;   // the town light (place bake, sun relight ratio maps)
    bool town_walls = false;   // the town walls' calibration (with town_light)
    bool lamp_shadows = false; // the lamps' shadow faces of the still geometry
    bool sun_shadows = false;  // the sun's shadow cascade and its caster tiles
    bool relief = false;       // relief maps (PBRify normals) of the textures drawn with them
    bool heights = false;      // with relief: the maps also carry PBRify's height (the draws read it: the parallax, the
                               // cavity, the characters' parallax, the puddles); else the height network does not run
    bool gloss = false;        // gloss maps (PBRify roughness) of the loaded textures
    bool upscale = false;      // neural ×2 textures near the camera

    // "far LODs, visibility volume, ...", or "none".
    std::string Describe() const;
    bool operator==(const BackgroundProducts &) const = default;
};
BackgroundProducts BackgroundProductsFor(const Settings &settings);

// «Качество текстур», the textures' resolution: 0 Низкое, 1 Среднее, 2 Высокое (the game's textures from their second
// mip, their first, their full size), applied at the next start (the textures are loaded at it). Высокое is the
// default.
enum class TextureQuality : int
{
    Low = 0,
    Medium = 1,
    High = 2,
};
inline constexpr int kTextureQualityLevels = 3;
inline constexpr TextureQuality kTextureQualityDefault = TextureQuality::High;
// The top mips the textures skip (the renderer's texture degradation): 2, 1, 0.
inline int TextureDegradation(TextureQuality quality)
{
    return 2 - static_cast<int>(quality);
}

// «Обработка текстур»: the player's three on/off choices of what the phone makes from the game's textures in the
// background, apart from the preset (app and engine.ini keys texture_normal_maps, texture_height_maps,
// texture_upscale). All three are off until the player turns them on, on every preset. The preset decides only how an
// effect that is on looks (the map sizes, the parallax's steps and shadow, the characters' relief).
struct TextureProcessing
{
    bool normal_maps = false; // «Карты нормалей»: PBRify's normal maps light the relief (kLightingNormalMap)
    // «Параллакс»: PBRify's height in the relief map's material texture (the parallax, the relief's cavity and
    // own shadow, the characters' parallax, the puddles in the relief's hollows). It is made with the normal map and
    // drawn through it: without normal_maps it is off whatever it says.
    bool height_maps = false;
    bool upscale = false; // «Апскейл текстур ×2»: the neural ×2 textures near the camera (Settings::upscale)

    bool operator==(const TextureProcessing &) const = default;
};
// Applies the three choices to a preset's settings (Settings::FromPreset): an effect that is off is switched
// off with everything that reads it; one that is on keeps the preset's quality, and on a preset without one
// («Низкая») takes the cheapest the presets have («Средняя»'s: per-pixel sun light for the relief to show, the maps
// from their second mip, the offset parallax, the characters' normals only; the visibility volume stays off, no bake
// runs for it). Gloss is the preset's: its maps of the relief textures ride in the relief map's material texture, so it
// goes off with the normal maps.
void ApplyTextureProcessing(Settings &settings, const TextureProcessing &processing);

enum class LampDepth : uint8_t
{
    None,
    D24,  // 24-bit fixed point
    D32F, // 32-bit float
};
const char *LampDepthName(LampDepth depth);

// What the device and the renderer can do, decided once at start-up (fx::DetectSupport() in capabilities.hpp
// fills the GPU part; the renderer fills `scene_depth`). Show it on the settings screen: an option whose
// requirement is missing must be disabled there, not quietly ignored.
struct Support
{
    bool lut_3d = false;     // 3D RGBA8 texture: the grading table every post effect passes through
    bool shadow_map = false; // D16 texture as a render target: Settings::shadows
    // The depth format of the lamps' shadow faces (lamp_shadows.hpp): a perspective depth over 1 cm..the lamp's
    // radius needs 24 bits or a float (16 bits resolve 15 cm at 10 m). None: Settings::lamp_shadows is switched off.
    LampDepth lamp_depth = LampDepth::None;
    bool scene_depth = false; // the scene's depth attachment can be sampled: Settings::sea_soft_edges, render scale
    bool normal_map = false;  // EAC RG11 textures (the derived maps): kLightingNormalMap, Settings::gloss
    uint8_t msaa_samples = 1; // the highest MSAA sample count (1 = none, 2 or 4) the scene target renders with,
                              // RGBA8 colour and its depth + stencil both (per-format bgfx caps): Settings::msaa
    bool smaa = false;        // R8 and RG8 textures (the SMAA search and area tables): Settings::smaa
    bool msaa_depth = false;  // the scene depth can still be sampled when the scene target is multisampled
                             // (renderer knowledge, like scene_depth): msaa together with sea soft edges / render scale
    bool anisotropy = false; // anisotropic texture filtering
    bool ao_volume = false;  // RGBA8 3D textures with linear filtering: Settings::ao_volume

    std::string Describe() const;
};

// Inconsistent or unsupported requests in `settings`, one message each; empty = every effect will run exactly
// as asked. PostChain, Shadows and the sea's soft edges refuse (and log) what this reports.
std::vector<std::string> ValidateSettings(const Settings &settings, const Support &support);

// The explicit, one-time alternative to a silent fallback: returns `requested` with every option that
// ValidateSettings() objects to switched off or lowered, and appends one line per change to `changes`.
// Call it when settings are loaded or changed, log the lines, and store / display the result.
Settings ConstrainSettings(const Settings &requested, const Support &support, std::vector<std::string> &changes);

// What the renderer has to set up for these settings; fx cannot do it itself because the scene target and
// the samplers of the game's textures belong to the renderer.
struct RendererState
{
    uint8_t msaa_samples = 1;       // scene colour + depth target; resolve before PostChain reads it
    bool alpha_to_coverage = false; // add BGFX_STATE_BLEND_ALPHA_TO_COVERAGE to alpha-tested draw calls and use
                                    // fxCoverageAlpha() (fx_coverage.sh) instead of the alpha test
    uint8_t max_anisotropy = 1; // > 1: BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC on mip-mapped world
                                // textures and BGFX_RESET_MAXANISOTROPY; cap the level at this value
    bool depth_prepass = false; // Settings::depth_prepass
    uint16_t render_height = 0; // Settings::render_height: with RenderScale() < 1 the scene colour + depth target
                                // is that fraction of the display size; PostChain upscales it, the interfaces
                                // draw on the display-size picture
};
RendererState RendererStateFor(const Settings &settings);

// The factor of the display size the 3D scene renders at for Settings::render_height on a display `display_height`
// pixels tall: render_height / display_height, never below kMinRenderScale (FSR 1's range), 1 when the display is
// not taller than render_height or render_height is 0.
inline constexpr float kMinRenderScale = 0.5f;
float RenderScale(uint16_t render_height, uint32_t display_height);

// Names for the settings screen (UTF-8, Russian).
std::string_view NameRu(QualityPreset preset);
std::string_view NameRu(GradePreset preset);

} // namespace storm::fx
