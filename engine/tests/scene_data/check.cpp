// The scene data parsers (src/libs/scene_data) on JSON written here; "[corpus]": every model material file, lighting
// preset, tone mapping, time of day and atmosphere file of the installed game in $STORM_GAME_DIR (not run without
// one).
#include "storm/scene_data/scene_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <catch2/catch.hpp>

using namespace storm::scene_data;

namespace
{
bool contains(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string read(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
} // namespace

TEST_CASE("model materials")
{
    const char *json = R"({
      "models": {
        "Crate": {"reflection": 1, "alpha": 0, "rma_texture": "Resource\\Textures\\Props\\Crate_RMA.dds",
                  "normal_texture": "Resource\\Textures\\Props\\Crate_nom.dds", "path_preset": "Props",
                  "metallic": 0.0, "roughness": 0.5, "sparkle": true},
        "Ground": {"layer_material": 1, "base_tiling": 4, "normal_strength": 1.5,
                   "mask_texture": "Resource\\Textures\\ground_mask.dds",
                   "r_albedo_texture": "a.dds", "r_normal_texture": "an.dds", "r_rma_texture": "ar.dds", "r_tiling": 8,
                   "g_albedo_texture": "b.dds", "g_tiling": 2.5, "b_rma_texture": "cr.dds", "x_tiling": 3}
      },
      "walkCutout": {"Radius": 1.0},
      "lod": {"profile": "SmallProps"}
    })";
    Report report;
    const ModelSidecar sidecar = ParseModelSidecar(json, report);
    REQUIRE(report.ok());
    CHECK(sidecar.lod_profile == "SmallProps");
    REQUIRE(sidecar.models.size() == 2);
    const ModelMaterial &crate = sidecar.models.at("Crate");
    CHECK((crate.reflection && !crate.alpha && crate.path_preset == "Props"));
    CHECK(crate.rma_texture == "Resource\\Textures\\Props\\Crate_RMA.dds");
    CHECK((crate.metallic == 0.0f && crate.roughness == 0.5f && !crate.emission));
    const ModelMaterial &ground = sidecar.models.at("Ground");
    CHECK((ground.layer_material && ground.base_tiling == 4 && ground.normal_strength == 1.5f));
    CHECK((ground.layers[0].albedo == "a.dds" && ground.layers[0].rma == "ar.dds" && ground.layers[0].tiling == 8));
    CHECK((ground.layers[1].tiling == 2.5f && ground.layers[2].rma == "cr.dds" && ground.layers[2].tiling == 1));
    CHECK(contains(report.unknown_keys, "$.models.Crate.sparkle"));
    CHECK(contains(report.unknown_keys, "$.models.Ground.x_tiling"));
    CHECK(contains(report.unknown_keys, "$.walkCutout"));

    Report wrong;
    const ModelSidecar typed = ParseModelSidecar(R"({"models": {"A": {"alpha": 2, "rma_texture": 5}}})", wrong);
    CHECK((wrong.errors.size() == 2 && typed.models.size() == 1 && !typed.models.at("A").alpha));
    Report broken;
    ParseModelSidecar("{\"models\": ", broken);
    CHECK(broken.errors.size() == 1);
}

TEST_CASE("mesh LOD profiles")
{
    Report report;
    const MeshLodProfiles file = ParseMeshLodProfiles(R"({
      "profiles": {
        "Default": {"hysteresis": 0.1, "screen_size": [0.12, 0.06, 0.03], "transition_ms": 256.0},
        "Rising": {"screen_size": [0.1, 0.2]},
        "Empty": {"screen_size": []}
      },
      "schema_version": 1
    })",
                                                      report);
    const MeshLodProfile &profile = file.profiles.at("Default");
    CHECK((profile.screen_size == std::vector<float>{0.12f, 0.06f, 0.03f} && profile.hysteresis == 0.1f &&
           profile.transition_ms == 256.0f));
    // A list that does not fall, or holds no level, is refused with an error rather than half used.
    CHECK((file.profiles.size() == 1 && report.errors.size() == 2));

    Report none;
    const ModelSidecar sidecar = ParseModelSidecar(R"({"lod": null, "models": {}})", none);
    CHECK((none.ok() && none.unknown_keys.empty() && sidecar.lod_profile.empty()));
}

TEST_CASE("model material textures and the keys the renderer does not draw")
{
    CHECK(MaterialTextureName("Resource\\Textures\\Props\\Box_RMA.dds") == "Props\\Box_RMA.dds");
    CHECK(MaterialTextureName("resource/textures/base_nom.tga.tx") == "base_nom.tga.tx");
    CHECK_FALSE(MaterialTextureName("Resource\\Models\\Box.gm"));
    CHECK_FALSE(MaterialTextureName("Resource\\Textures\\"));
    CHECK(MaterialTextureName("Props\\MarketFrame2_nom.dds") == "Props\\MarketFrame2_nom.dds");
    CHECK(MaterialTextureName("buildings/Residence3_1_rma.dds") == "buildings\\Residence3_1_rma.dds");
    CHECK_FALSE(MaterialTextureName(""));
    ModelMaterial material;
    material.rma_texture = "Resource\\Textures\\a_RMA.dds";
    material.alpha = true;
    CHECK(UndrawnMaterialKeys(material).empty());
    material.animation = "plant"; // drawn: the wind bends it
    CHECK(UndrawnMaterialKeys(material).empty());
    material.animation = "river";
    material.layer_material = true;
    material.reflection = true;
    const std::vector<std::string> undrawn = UndrawnMaterialKeys(material);
    CHECK(undrawn.size() == 2);
    CHECK(contains(undrawn, "animation"));
    CHECK(contains(undrawn, "reflection"));
}

TEST_CASE("lighting presets")
{
    const char *json = R"({
      "schemaVersion": 2, "presetName": "Test", "weatherType": "rain", "sourcePresetName": "Base", "grass_preset": "grass1", "locationId": ["A_town", "B_port"],
      "sun_azimuth": 120, "sun_height": 35.5,
      "good_rays": {"enabled": true, "density": 0.5, "intensity": 1, "length": 2, "quality": 3},
      "hours": {
        "0": {"ambient": {"color": [0.1, 0.2, 0.3], "intensity": 0.5},
              "sun": {"color": [1, 1, 1], "intensity": 0.01},
              "bloom": {"intensity": 2.0, "treshold": 1.0, "soft_treshold": 0.5},
              "emission": 1.0, "exposure": 1.5, "hdr_texture": "resource/lighting/Night.hdr",
              "shadow": {"ao": 0.8},
              "fog": {"color": [0.5, 0.5, 0.6], "density": 0.02},
              "point": {"torch": {"color": [1, 0.5, 0.2], "intensity": 3}, "candle": {"color": [1, 1, 0], "intensity": 1}},
              "sea": {"color": [0, 0.1, 0.2], "params": [1, 2, 3], "sky": {"color": [0.3, 0.3, 0.4]}}},
        "13": {"exposure": 2, "rain": 1, "ambient": {"color": [1, 2]}},
        "24": {}
      }
    })";
    Report report;
    const LightingPreset preset = ParseLightingPreset(json, report);
    CHECK(report.errors.size() == 2); // hour "24", a colour of two numbers
    CHECK(contains(report.unknown_keys, "$.hours.13.rain"));
    CHECK((preset.schema_version == 2 && preset.preset_name == "Test" && preset.location_ids.size() == 2));
    CHECK((preset.weather_type == "rain" && preset.source_preset_name == "Base"));
    CHECK((preset.sun_azimuth == 120.0f && preset.god_rays && preset.god_rays->quality == 3));
    CHECK((preset.hours[0] && preset.hours[13] && !preset.hours[1]));
    const LightingHour &night = *preset.hours[0];
    CHECK((night.ambient_color[2] == 0.3f && night.ambient_intensity == 0.5f && night.sun_intensity == 0.01f));
    CHECK((night.bloom_intensity == 2 && night.bloom_threshold == 1 && night.bloom_soft_threshold == 0.5f));
    CHECK((night.exposure == 1.5f && night.shadow_ao == 0.8f && night.hdr_texture == "resource/lighting/Night.hdr"));
    CHECK((night.fog && night.fog->density == 0.02f));
    CHECK((night.point_lights.size() == 2 && night.point_lights.at("torch").intensity == 3));
    CHECK((night.sea && night.sea->params[2] == 3 && night.sea->sky_color[2] == 0.4f && !night.sea->deep_color));
    CHECK((preset.hours[13]->exposure == 2 && !preset.hours[13]->fog));
}

TEST_CASE("tone mapping")
{
    Report report;
    const ToneMapping tone = ParseToneMapping(
        R"({"type": "aces", "version": 1, "exposure_ev": -0.5, "contrast": 1.1, "lift": [0, 0, 0.1], "vignette": 1})",
        report);
    CHECK(report.ok());
    CHECK((tone.type == "aces" && tone.version == 1 && tone.exposure_ev == -0.5f && tone.contrast == 1.1f));
    CHECK((tone.lift[2] == 0.1f && tone.gain[0] == 1.0f));
    CHECK(contains(report.unknown_keys, "$.vignette"));
}

namespace
{
// A period of time_of_day.json with every key; `exposure` is its base_exposure.
std::string Period(const char *light, float exposure)
{
    return std::string(R"({"light_color": )") + light + R"(, "light_intensity": 2, "sky_day_radiance": [1, 2, 3],
        "sky_night_radiance": [0.01, 0.02, 0.03], "sky_fill_color": [1, 1, 1], "sky_fill_intensity": 1,
        "diffuse_sky_tint": [1, 1, 1], "ground_color": [0.9, 0.8, 0.6], "ground_intensity": 0.5,
        "base_exposure": )" +
           std::to_string(exposure) + R"(, "camera": {"type": "aces", "version": 1, "contrast": 0.7}})";
}

std::string TimeOfDayJson(const std::string &transitions, float night_exposure)
{
    return R"({"version": 1, "enabled": true, "transition_hours": )" + transitions + R"(,
        "celestial": {"moon_overlap_hours": 2, "handoff_hours": 0.5, "twilight_hours": 0.4, "shadow_morning_hour": 6.25,
                      "shadow_evening_hour": 20, "shadow_morning_fade_minutes": 1, "shadow_evening_fade_minutes": 1},
        "presets": {"dawn": )" +
           Period("[1, 0.7, 0.5]", 2) + R"(, "day": )" + Period("[1, 1, 1]", 2) + R"(, "sunset": )" +
           Period("[1, 0.6, 0.4]", 1) + R"(, "night": )" + Period("[0.1, 0.2, 0.6]", night_exposure) + "}}";
}
} // namespace

TEST_CASE("time of day")
{
    Report report;
    const TimeOfDay day = ParseTimeOfDay(TimeOfDayJson("[5, 7, 8, 10, 17, 18, 19, 20]", 1), report);
    CHECK(report.ok());
    CHECK((day.enabled && day.version == 1 && day.transition_hours[7] == 20 && day.celestial.handoff_hours == 0.5f));
    CHECK((day.periods[kDawn].light_color[1] == 0.7f && day.periods[kNight].light_color[2] == 0.6f));
    CHECK((day.periods[kDay].sky_day_radiance[2] == 3 && day.periods[kSunset].camera.contrast == 0.7f));

    Report backwards;
    ParseTimeOfDay(TimeOfDayJson("[5, 7, 8, 10, 17, 18, 21, 20]", 1), backwards);
    CHECK(backwards.errors.size() == 1);
    Report dark;
    ParseTimeOfDay(TimeOfDayJson("[5, 7, 8, 10, 17, 18, 19, 20]", 0), dark); // log2 of 0
    CHECK(dark.errors.size() == 1);
    Report missing;
    ParseTimeOfDay(R"({"version": 1, "enabled": true, "transition_hours": [1, 2, 3, 4, 5, 6, 7, 8]})", missing);
    CHECK((missing.errors.size() == 2 && contains(missing.errors, "$.presets: missing")));
}

TEST_CASE("atmosphere")
{
    const auto fog = [](float attenuation, float top) {
        return R"({"attenuation_distance": )" + std::to_string(attenuation) +
               R"(, "base_height": 0, "maximum_height": )" + std::to_string(top) +
               R"(, "start_distance": 10, "max_distance": 1e5, "max_opacity": 0.9, "mip_near": 0, "mip_far": 100,
                  "mip_max": 0, "color": [0.6, 0.3, 0.2], "tint": [1, 1, 1], "sky_color": false})";
    };
    const auto json = [&](float night_attenuation) {
        const std::string periods = R"({"Morning": )" + fog(120, 30) + R"(, "Day": )" + fog(1000, 33) +
                                    R"(, "Evening": )" + fog(400, 96) + R"(, "Night": )" + fog(night_attenuation, 20) +
                                    "}";
        const char *haze_color = R"({"tint": [1, 1, 1], "radiance": [0.5, 0.7, 0.9], "brightness": 0.9})";
        const auto cloud = [](float opacity) {
            return R"({"intensity": 2, "opacity": )" + std::to_string(opacity) +
                   R"(, "edge_lighting_strength": 1, "reflection_strength": 0.2, "moon_intensity": 1,
                "sun_transmission_strength": 2, "moon_transmission_strength": 5, "sun_transmission_radius": 19,
                "moon_transmission_radius": 15, "sun_intensity": 1, "ambient_intensity": 3, "sky_tint": 0.6,
                "lighting_contrast": 1.6, "twilight_sun_multiplier": 1, "twilight_base_lighting": 2, "twilight_warmth": 1,
                "twilight_edge_color": 0.2, "twilight_opposite_strength": 0.8, "twilight_transition_width": 131,
                "twilight_ambient_intensity": 1.4, "twilight_tint": [0.4, 0.5, 0.7]})";
        };
        const std::string clouds = R"({"version": 1, "enabled": true, "rotation_speeds": [0.1, 0.3, 1],
            "periods": {"dawn": )" +
                                   cloud(0.67f) + R"(, "day": )" + cloud(0.28f) + R"(, "sunset": )" + cloud(0.53f) +
                                   R"(, "night": )" + cloud(1.0f) + "}}";
        return R"({"version": 1, "textured_clouds": )" + clouds + R"(,
            "horizon_haze": {"enabled": true, "sky_color": true, "intensity": 1, "sea_width_degrees": 30,
                "sky_width_degrees": 8.6, "edge_fade": 1, "sea_start": 0, "sea_fade": 2000, "sample_elevation_degrees": 1.4,
                "transition_hours": [5, 7, 8, 10, 17, 18, 19, 20],
                "colors": {"Dawn": )" +
               std::string(haze_color) + R"(, "Day": )" + haze_color + R"(, "Sunset": )" + haze_color +
               R"(, "Night": )" + haze_color + R"(}},
            "height_fog": {"enabled": true, "profiles": {"Land": {"enabled": true,
                "transition_hours": [5, 7, 8, 10, 17, 18, 19, 20], "periods": )" +
               periods + "}}}}";
    };
    Report report;
    const Atmosphere atmosphere = ParseAtmosphere(json(30), report);
    CHECK(report.ok());
    REQUIRE((atmosphere.horizon_haze && atmosphere.land && !atmosphere.sea && atmosphere.height_fog &&
             atmosphere.textured_clouds));
    CHECK((atmosphere.textured_clouds->enabled && atmosphere.textured_clouds->rotation_speeds[1] == 0.3f &&
           atmosphere.textured_clouds->periods[kDay].opacity == 0.28f &&
           atmosphere.textured_clouds->periods[kNight].twilight_tint[2] == 0.7f));
    CHECK((atmosphere.horizon_haze->sky_width_degrees == 8.6f &&
           atmosphere.horizon_haze->colors[kNight].brightness == 0.9f));
    CHECK((atmosphere.land->periods[kNight].attenuation_distance == 30 &&
           atmosphere.land->periods[kSunset].maximum_height == 96));
    Report zero;
    ParseAtmosphere(json(0), zero); // a density of 1 / 0
    CHECK(zero.errors.size() == 1);
}

TEST_CASE("texture library")
{
    const char *json = R"({"grass_preset": {}, "cloud_preset": {"default": {
        "albedo_texture": "resource/Textures/WEATHER/Cloud/cumulus_1/CloudA.dds",
        "lighting_texture": "resource/Textures/WEATHER/Cloud/cumulus_1/CloudB.dds",
        "radiance_scale": 4.0, "premultiplied_alpha": true, "ambient_scale": 1.0, "ambient_strength": 0.15,
        "has_ambient": false, "day_sun_intensity": 1.05,
        "rain_layer": {"albedo_texture": "a.dds", "lighting_texture": "b.dds", "radiance_scale": 2.0,
                       "premultiplied_alpha": true, "has_ambient": true, "ambient_scale": 0.5}}}})";
    Report report;
    const TextureLibrary library = ParseTextureLibrary(json, report);
    CHECK(report.ok());
    // Only the cloud presets; the keys of the lighting without the time of day's periods are not used.
    CHECK((contains(report.unknown_keys, "$.grass_preset") &&
           contains(report.unknown_keys, "$.cloud_preset.default.day_sun_intensity")));
    REQUIRE(library.clouds.contains("default"));
    const CloudPreset &clouds = library.clouds.at("default");
    CHECK((clouds.base.radiance_scale == 4.0f && clouds.base.premultiplied_alpha && clouds.ambient_strength == 0.15f));
    CHECK(MaterialTextureName(clouds.base.albedo_texture) == "WEATHER\\Cloud\\cumulus_1\\CloudA.dds");
    CHECK((clouds.rain.radiance_scale == 2.0f && clouds.rain.has_ambient && clouds.rain.ambient_scale == 0.5f &&
           clouds.upper.albedo_texture.empty()));
    Report missing;
    ParseTextureLibrary(R"({"cloud_preset": {"default": {"albedo_texture": "x.dds"}}})", missing);
    CHECK(!missing.ok());
}

TEST_CASE("scene data of an installed game", "[.corpus]")
{
    namespace fs = std::filesystem;
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    struct Count
    {
        int files = 0, failed = 0, entries = 0;
        std::map<std::string, int> unknown;
    };
    std::map<std::string, Count> counts;
    const auto record = [&](const std::string &kind, const fs::path &path, const Report &report, int entries) {
        Count &count = counts[kind];
        ++count.files;
        count.entries += entries;
        if (!report.ok())
        {
            ++count.failed;
            std::printf("FAILED %s: %s\n", path.c_str(), report.errors.front().c_str());
        }
        for (std::string key : report.unknown_keys)
        {
            // $.models.<object>.key and $.hours.<hour>.key are counted per key.
            for (const std::string_view prefix : {"$.models.", "$.hours."})
                if (key.starts_with(prefix))
                    if (const size_t dot = key.find('.', prefix.size()); dot != std::string::npos)
                        key = std::string(prefix) + "*" + key.substr(dot);
            ++count.unknown[key];
        }
    };
    for (const auto &entry : fs::recursive_directory_iterator(root))
    {
        const std::string name = lower(entry.path().filename().string());
        if (!entry.is_regular_file() || !name.ends_with(".json"))
            continue;
        const std::string folders = lower(fs::relative(entry.path().parent_path(), root).generic_string()) + "/";
        // <model>.gm.maya.json and <model>.gm.walkcut.json beside a model are an editor's, not materials.
        if (folders.find("models/") != std::string::npos && name.find(".gm.") == std::string::npos)
        {
            Report report;
            const ModelSidecar sidecar = ParseModelSidecar(read(entry.path()), report);
            record("model materials", entry.path(), report, int(sidecar.models.size()));
        }
        else if (folders.find("lighting/") != std::string::npos || folders == "resource/weather/")
        {
            Report report;
            const LightingPreset preset = ParseLightingPreset(read(entry.path()), report);
            record("lighting preset", entry.path(), report,
                   int(std::ranges::count_if(preset.hours, [](const auto &hour) { return hour.has_value(); })));
        }
        else if (name == "aces.json")
        {
            Report report;
            ParseToneMapping(read(entry.path()), report);
            record("tone mapping", entry.path(), report, 1);
        }
        else if (name == "time_of_day.json")
        {
            Report report;
            ParseTimeOfDay(read(entry.path()), report);
            record("time of day", entry.path(), report, kPeriods);
        }
        else if (name == "atmosphere.json")
        {
            Report report;
            const Atmosphere atmosphere = ParseAtmosphere(read(entry.path()), report);
            record("atmosphere", entry.path(), report,
                   int(atmosphere.horizon_haze.has_value()) + int(atmosphere.land.has_value()) +
                       int(atmosphere.sea.has_value()));
        }
    }
    int failed = 0;
    for (const auto &[kind, count] : counts)
    {
        std::printf("%-16s files %5d  failed %3d  entries %6d\n", kind.c_str(), count.files, count.failed,
                    count.entries);
        for (const auto &[key, n] : count.unknown)
            std::printf("    not modelled %-40s %6d\n", key.c_str(), n);
        failed += count.failed;
    }
    REQUIRE(failed == 0);
}
