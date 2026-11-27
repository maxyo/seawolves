// Parsers of the scene data a game may keep in JSON files beside its models and places (no rendering):
//  * a model's materials per object      <model>.json beside <model>.gm     {"models": {...}}
//  * lighting presets, one per place      Lighting/*.json                    {"hours": {"0".."23": ...}}
//  * the camera's tone mapping and grade  INI/aces.json                      {type, exposure_ev, contrast, ...}
//  * the periods of the day               INI/time_of_day.json               {transition_hours, presets, celestial}
//  * horizon haze, height fog, clouds     INI/atmosphere.json                {horizon_haze, height_fog,
//  textured_clouds}
//  * the cloud layers' textures           texture_library.json               {cloud_preset: {<variant>: ...}}
// Every parser returns its result with a Report: `errors` are malformed JSON or values of the wrong type (the entry
// is skipped), `unknown_keys` the keys it does not model (JSON-path style), so nothing in a file is dropped unsaid.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace storm::scene_data
{

struct Report
{
    std::vector<std::string> errors;
    std::vector<std::string> unknown_keys;
    bool ok() const
    {
        return errors.empty();
    }
};

using Rgb = std::array<float, 3>;

// ---- model sidecar ----------------------------------------------------------------------------

struct LayerTextures
{
    std::string albedo, normal, rma;
    float tiling = 1.0f;
};

struct ModelMaterial
{
    bool reflection = false;
    bool alpha = false;
    bool alpha_ccw = false; // alpha blended, counter-clockwise culling
    bool shadow_cw = false;
    bool point_shadow = false;
    bool point_light = false; // "PointLight"
    std::string rma_texture, normal_texture,
        emission_texture;    // as written, e.g. "Resource\\Textures\\Props\\Box_RMA.dds"
    std::string path_preset; // texture search preset: NONE, Props, Location, Vegetation, WorldMap, ShipOther
    std::string animation;   // vertex animation preset: plant, tent, river
    std::optional<float> emission, metallic, roughness;
    // Terrain-style layered material: three PBR layers blended over the base by an RGB mask.
    bool layer_material = false;
    float base_tiling = 1.0f;
    float normal_strength = 1.0f;
    std::string mask_texture;
    std::array<LayerTextures, 3> layers; // r, g, b
};

struct ModelSidecar
{
    std::map<std::string, ModelMaterial> models; // keyed by the .gm object name
};

ModelSidecar ParseModelSidecar(std::string_view json, Report &report);
// A texture path of a material file ("Resource\\Textures\\Props\\Box_RMA.dds") relative to resource\textures, as the
// renderer names textures ("Props\\Box_RMA.dds"); nullopt for a path outside resource\textures.
std::optional<std::string> MaterialTextureName(std::string_view path);
// The keys of a material that the renderer's material programs do not draw (renderer_bgfx README, "Materials").
std::vector<std::string> UndrawnMaterialKeys(const ModelMaterial &material);

// ---- lighting preset --------------------------------------------------------------------------

struct PointLightGroup
{
    Rgb color{};
    float intensity = 0.0f;
};

struct Fog
{
    Rgb color{};
    float density = 0.0f;
};

struct SeaLighting
{
    Rgb color{};
    std::array<float, 3> params{};
    Rgb sky_color{};
    std::optional<Rgb> deep_color, shallow_color;
};

struct LightingHour
{
    // Colours are linear 0..1; an intensity scales its colour.
    Rgb ambient_color{};
    float ambient_intensity = 0.0f;
    Rgb sun_color{};
    float sun_intensity = 0.0f;
    float bloom_intensity = 0.0f, bloom_threshold = 0.0f, bloom_soft_threshold = 0.0f;
    float emission = 0.0f;
    float exposure = 1.0f;
    float shadow_ao = 1.0f;
    std::optional<Fog> fog;
    std::string hdr_texture;   // e.g. "resource/lighting/Day_Jungle.hdr"
    std::string night_texture; // the hour's other image-based light
    std::map<std::string, PointLightGroup> point_lights;
    std::optional<SeaLighting> sea;
};

struct GodRays
{
    bool enabled = false;
    float density = 0.0f, intensity = 0.0f, length = 0.0f;
    int quality = 0;
};

struct LightingPreset
{
    int schema_version = 0;
    std::string preset_name, description, grass_preset;
    std::string weather_type;       // "rain": an override of a few values of the place's preset
    std::string source_preset_name; // the preset an editor made this one from
    std::vector<std::string> location_ids;
    std::optional<float> sun_azimuth, sun_height;
    std::optional<GodRays> god_rays;
    std::array<std::optional<LightingHour>, 24> hours;
};

LightingPreset ParseLightingPreset(std::string_view json, Report &report);

// ---- tone mapping -----------------------------------------------------------------------------

struct ToneMapping
{
    std::string type; // "aces"
    int version = 0;
    float exposure_ev = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f, tint = 0.0f;
    Rgb color_filter{1.0f, 1.0f, 1.0f};
    Rgb lift{0.0f, 0.0f, 0.0f};
    Rgb gamma{1.0f, 1.0f, 1.0f};
    Rgb gain{1.0f, 1.0f, 1.0f};
};

ToneMapping ParseToneMapping(std::string_view json, Report &report);

// ---- time of day and atmosphere ---------------------------------------------------------------

// The four periods of a day, in the order every file's period table is kept here.
enum Period : int
{
    kDawn,
    kDay,
    kSunset,
    kNight,
    kPeriods
};

// Hours 0..24, increasing: [0, 1] night -> dawn, [2, 3] dawn -> day, [4, 5] day -> sunset, [6, 7] sunset -> night.
using TransitionHours = std::array<float, 8>;

struct TimeOfDayPeriod
{
    Rgb light_color{};
    float light_intensity = 0.0f;
    Rgb sky_day_radiance{}, sky_night_radiance{};
    Rgb sky_fill_color{};
    float sky_fill_intensity = 0.0f;
    Rgb diffuse_sky_tint{};
    Rgb ground_color{};
    float ground_intensity = 0.0f;
    float base_exposure = 1.0f; // > 0
    ToneMapping camera;
};

struct CelestialTimes
{
    float moon_overlap_hours = 0.0f, handoff_hours = 0.0f, twilight_hours = 0.0f;
    float shadow_morning_hour = 0.0f, shadow_evening_hour = 0.0f;
    float shadow_morning_fade_minutes = 0.0f, shadow_evening_fade_minutes = 0.0f;
};

struct TimeOfDay
{
    int version = 0;
    bool enabled = false;
    TransitionHours transition_hours{};
    std::string source_preset;
    CelestialTimes celestial;
    std::array<TimeOfDayPeriod, kPeriods> periods{};
};

// Every key is required; transition hours that do not increase are an error.
TimeOfDay ParseTimeOfDay(std::string_view json, Report &report);

struct HazeColor
{
    Rgb tint{}, radiance{};
    float brightness = 0.0f;
};

struct HorizonHaze
{
    bool enabled = false;
    bool sky_color = false; // the band takes the sky's colour near the horizon, tinted, instead of `radiance`
    float intensity = 0.0f;
    float sea_width_degrees = 0.0f, sky_width_degrees = 0.0f, edge_fade = 0.0f;
    float sea_start = 0.0f, sea_fade = 0.0f, sample_elevation_degrees = 0.0f;
    std::array<HazeColor, kPeriods> colors{};
    TransitionHours transition_hours{};
};

struct HeightFogPeriod
{
    float attenuation_distance = 0.0f; // metres; the density is its inverse
    float base_height = 0.0f, maximum_height = 0.0f;
    float start_distance = 0.0f, max_distance = 0.0f, max_opacity = 0.0f;
    float mip_near = 0.0f, mip_far = 0.0f, mip_max = 0.0f;
    Rgb color{}, tint{};
    bool sky_color = false;
};

struct HeightFogProfile
{
    bool enabled = false;
    std::array<HeightFogPeriod, kPeriods> periods{};
    TransitionHours transition_hours{};
};

// How the textured cloud layers look and are lit in one period (atmosphere.json textured_clouds.periods).
struct CloudPeriod
{
    float intensity = 0.0f;              // the clouds' radiance multiplier
    float opacity = 0.0f;                // how much of the base and upper layers shows, 0..1
    float edge_lighting_strength = 1.0f; // the thin edges' light multiplier
    float reflection_strength = 0.0f;
    float moon_intensity = 0.0f;
    float sun_transmission_strength = 0.0f, moon_transmission_strength = 0.0f;
    float sun_transmission_radius = 0.0f, moon_transmission_radius = 0.0f; // degrees
    float sun_intensity = 0.0f, ambient_intensity = 0.0f, sky_tint = 0.0f, lighting_contrast = 1.0f;
    float twilight_sun_multiplier = 0.0f, twilight_base_lighting = 0.0f, twilight_warmth = 0.0f;
    float twilight_edge_color = 0.0f, twilight_opposite_strength = 0.0f;
    float twilight_transition_width = 0.0f; // degrees
    float twilight_ambient_intensity = 0.0f;
    Rgb twilight_tint{};
};

// The periods blend by the time of day's transition hours.
struct TexturedClouds
{
    bool enabled = false;
    std::array<float, 3> rotation_speeds{}; // the upper, base and rain layers'; 0 holds a layer, below 0 reverses it
    std::array<CloudPeriod, kPeriods> periods{};
};

struct Atmosphere
{
    int version = 0;
    std::optional<HorizonHaze> horizon_haze;
    bool height_fog = false;                   // height_fog.enabled
    std::optional<HeightFogProfile> land, sea; // height_fog.profiles.Land / Sea
    std::optional<TexturedClouds> textured_clouds;
};

Atmosphere ParseAtmosphere(std::string_view json, Report &report);

// ---- texture library --------------------------------------------------------------------------

// One cloud layer's cube textures and how their texels read (texture_library.json cloud_preset).
struct CloudLayerFiles
{
    // As written ("resource/Textures/WEATHER/Cloud/cumulus_1/CloudA.dds"; MaterialTextureName gives the renderer's
    // name): the response to light from +x, +y, +z with the opacity in alpha, and to light from -x, -y, -z with the
    // ambient response in alpha.
    std::string albedo_texture, lighting_texture;
    float radiance_scale = 1.0f;
    bool premultiplied_alpha = false;
    float ambient_scale = 1.0f;
    bool has_ambient = false; // the lighting texture's alpha is the ambient response
};

struct CloudPreset
{
    CloudLayerFiles base, upper, rain; // the preset's own files are the base layer's
    float ambient_strength = 0.0f;
};

// Resource/texture_library.json: the cloud presets by variant name. The keys only the textured clouds' lighting
// without time-of-day periods reads are reported as unknown.
struct TextureLibrary
{
    std::map<std::string, CloudPreset> clouds;
};
TextureLibrary ParseTextureLibrary(std::string_view json, Report &report);

} // namespace storm::scene_data
