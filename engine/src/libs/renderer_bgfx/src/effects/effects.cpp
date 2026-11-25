#include "storm/fx/cpu_admission.hpp"
#include "effects/scene_reveal.hpp"
#include "storm/render/pipelines.hpp"
#include "effects/effects.hpp"
#include "storm/render/far_lod.hpp"
#include "effects/thermal.hpp"
#include "platform/frame_work.hpp"
#include "effects/upscale.hpp"
#include "effects/relief_generator.hpp"

#include "effects/casters.hpp"
#include "effects/texture_pixels.hpp"
#include "storm/fx/skin_tone.hpp"
#include "shim/device.hpp"
#include "storm/fx/bakes.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/cutout.hpp"
#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/render/log.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/render/program_cache.hpp"
#include "texture/core/formats.hpp"
#include "shim/util.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cctype>
#include <cmath>
#include <cstring>

// patches/bgfx-anisotropy-level.patch: the level cap of each backend (read by the render thread every frame).
namespace bgfx::gl
{
extern std::atomic<float> g_stormMaxAnisotropy;
}
namespace bgfx::vk
{
extern std::atomic<float> g_stormMaxAnisotropy;
}

namespace storm::effects
{
namespace
{
// One engine option: an integer from engine.ini / the app's graphics preferences. -1 keeps the preset's value.
struct Option
{
    const char *name;
    const char *values;
    void (*apply)(fx::Settings &, int);
};

bool on(int value)
{
    return value != 0;
}

void set_flag(fx::Settings &s, uint32_t flag, int value)
{
    if (on(value))
        s.lighting_flags |= flag;
    else
        s.lighting_flags &= ~flag;
}

const Option kOptions[] = {
    {"fx_tonemap", "0 off, 1 on", [](fx::Settings &s, int v) { s.tonemap = on(v); }},
    {"fx_bloom", "0 off, 1 on", [](fx::Settings &s, int v) { s.bloom = on(v); }},
    {"fx_smaa", "0 off, 1 on", [](fx::Settings &s, int v) { s.smaa = on(v); }},
    {"fx_render_height", "the 3D scene's height in pixels on taller displays, upscaled by FSR 1: 360..4320, 0 = native",
     [](fx::Settings &s, int v) { s.render_height = static_cast<uint16_t>(std::clamp(v, 0, 4320)); }},
    {"fx_sharpen", "0 off, 1 on (FSR 1 RCAS)", [](fx::Settings &s, int v) { s.sharpen = on(v); }},
    {"fx_far_lod", "tenths of a pixel the far LODs may differ by: 0 off (every model whole), 5..40",
     [](fx::Settings &s, int v) { s.far_lod_pixels = v <= 0 ? 0.f : static_cast<float>(std::clamp(v, 5, 40)) / 10.f; }},
    {"fx_dither", "0 off, 1 on", [](fx::Settings &s, int v) { s.dither = on(v); }},
    {"fx_grade", "0 Оригинал, 1 Карибы, 2 Пасмурно",
     [](fx::Settings &s, int v) { s.grade = static_cast<fx::GradePreset>(std::clamp(v, 0, 2)); }},
    {"fx_msaa", "1, 2, 4", [](fx::Settings &s, int v) { s.msaa = static_cast<uint8_t>(std::clamp(v, 1, 4)); }},
    {"fx_alpha_to_coverage", "0 off, 1 on", [](fx::Settings &s, int v) { s.alpha_to_coverage = on(v); }},
    {"fx_depth_prepass", "0 off, 1 opaque scene draws lay their depth first",
     [](fx::Settings &s, int v) { s.depth_prepass = on(v); }},
    {"fx_anisotropy", "1, 4, 8",
     [](fx::Settings &s, int v) { s.anisotropy = static_cast<uint8_t>(std::clamp(v, 1, 16)); }},
    {"fx_sea_band_limit", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_band_limit = on(v); }},
    {"fx_sea_soft_edges", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_soft_edges = on(v); }},
    {"fx_sea_specular", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_specular = on(v); }},
    {"fx_sea_wakes", "0 off, 1 on: the wakes of hulls", [](fx::Settings &s, int v) { s.sea_wakes = on(v); }},
    {"fx_lamp_shadows", "lamps that relight a location with their shadows, 0, 8 or 16",
     [](fx::Settings &s, int v) { s.lamp_shadows = static_cast<uint8_t>(std::clamp(v, 0, 16)); }},
    {"fx_lamp_soft", "0 3x3, 1 5x5 filter of the strongest lamp's shadows",
     [](fx::Settings &s, int v) { s.lamp_soft = on(v); }},
    {"fx_lamp_edges", "0 off, 1 the lamps' still shadows are filtered only at their edges (same picture, fewer reads)",
     [](fx::Settings &s, int v) { s.lamp_edges = on(v); }},
    {"fx_lamp_per_pixel", "1..8: the most lamps that light one pixel with their shadows",
     [](fx::Settings &s, int v) { s.lamp_per_pixel = static_cast<uint8_t>(std::clamp(v, 1, fx::kLampGridCapacity)); }},
    {"fx_lamp_filtered", "1..8: of those, how many take filtered shadows",
     [](fx::Settings &s, int v) { s.lamp_filtered = static_cast<uint8_t>(std::clamp(v, 1, fx::kLampGridCapacity)); }},
    {"fx_lamp_moving", "0, 1, 2: lamp slots for carried lanterns and torches (their shadows move with them)",
     [](fx::Settings &s, int v) { s.lamp_moving = static_cast<uint8_t>(std::clamp(v, 0, 2)); }},
    {"fx_lamp_tint", "0..100 %: the share of a lamp's own colour in the light it adds to baked surfaces",
     [](fx::Settings &s, int v) { s.lamp_tint = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_light_reach", "100..400 %: how much further than the game's own lights every light reaches",
     [](fx::Settings &s, int v) { s.light_falloff.reach = static_cast<float>(std::clamp(v, 100, 400)) / 100.0f; }},
    {"fx_light_core", "1..200 cm: the soft core of every light (larger: less hot next to the flame)",
     [](fx::Settings &s, int v) { s.light_falloff.core = static_cast<float>(std::clamp(v, 1, 200)) / 100.0f; }},
    {"fx_light_level", "10..200 %: every light's strength in the middle of its reach against the game's",
     [](fx::Settings &s, int v) { s.light_falloff.level = static_cast<float>(std::clamp(v, 10, 200)) / 100.0f; }},
    {"fx_lamp_model_lift", "0..100 %: lit models of an interior brighter than the room light around them",
     [](fx::Settings &s, int v) { s.lamp_model_lift = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_wet_surfaces", "rain wetness outdoors: 0 off, 1 wet surfaces, 2 also puddles in the relief's hollows",
     [](fx::Settings &s, int v) { s.wet_surfaces = static_cast<uint8_t>(std::clamp(v, 0, 2)); }},
    {"fx_wet_force", "0..100 %: this wetness outdoors whatever the rain (review); -1 = the rain's",
     [](fx::Settings &s, int v) { s.wet_force = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_ao_volume", "interiors: 0 off, 1 as the game's data, 2 moderate, 3 strong",
     [](fx::Settings &s, int v) { s.ao_volume = static_cast<uint8_t>(std::clamp(v, 0, 3)); }},
    {"fx_ao_gain", "50..200 %: how much the interiors' occlusion volume takes against its level (125 by default)",
     [](fx::Settings &s, int v) { s.ao_gain = static_cast<float>(std::clamp(v, 50, 200)) / 100.0f; }},
    {"fx_shadows", "0 off, 1 low, 2 soft",
     [](fx::Settings &s, int v) { s.shadows = static_cast<fx::ShadowQuality>(std::clamp(v, 0, 2)); }},
    {"fx_town_light", "0 the game's town lightmaps, 1 relit for the live sun (a light ratio over them)",
     [](fx::Settings &s, int v) { s.town_light = on(v); }},
    {"fx_town_walls", "0 the game's baked walls in towns, 1 relit by our sun and sky (with fx_town_light)",
     [](fx::Settings &s, int v) { s.town_walls = on(v); }},
    {"fx_visibility", "0 off, 1 on: the location's visibility volume (sky visibility, rain shelter)",
     [](fx::Settings &s, int v) { s.visibility = on(v); }},
    {"fx_flash_lights", "flash lights of shots and explosions at once, 0..8",
     [](fx::Settings &s, int v) { s.flash_lights = static_cast<uint8_t>(std::clamp(v, 0, 8)); }},
    {"fx_light_per_pixel", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPerPixel, v); }},
    {"fx_light_specular", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingSpecular, v); }},
    {"fx_sail_light", "0..300 % sunlight through sails",
     [](fx::Settings &s, int v) { s.sail_light = static_cast<float>(std::clamp(v, 0, 300)) / 100.0f; }},
    {"fx_light_point", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPoint, v); }},
    {"fx_light_normal_map", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingNormalMap, v); }},
    {"fx_gloss", "0 off, 1 on: gloss maps shape the sun's and the lamps' highlights",
     [](fx::Settings &s, int v) { s.gloss = on(v); }},
    {"fx_upscale_budget", "MB of GPU memory for the ×2 textures, 0 from the device's RAM (measurements)",
     [](fx::Settings &s, int v) { s.upscale_budget_mb = static_cast<uint16_t>(std::clamp(v, 0, 4096)); }},
    {"fx_gloss_strength", "10..300 % of the gloss maps' highlights",
     [](fx::Settings &s, int v) { s.gloss_strength = static_cast<float>(std::clamp(v, 10, 300)) / 100.0f; }},
    {"fx_parallax", "0 off, 1 offset, 2 occlusion (with the normal maps and their height)",
     [](fx::Settings &s, int v) { s.parallax = static_cast<fx::ParallaxQuality>(std::clamp(v, 0, 2)); }},
    {"fx_parallax_shadow", "0 off, 1 on: the occlusion parallax's own shadow towards the sun",
     [](fx::Settings &s, int v) { s.parallax_shadow = on(v); }},
    {"fx_character_relief", "0 off, 1 the characters' normal maps, 2 and their shallow parallax (faded out on skin)",
     [](fx::Settings &s, int v) { s.character_relief = static_cast<fx::CharacterRelief>(std::clamp(v, 0, 2)); }},
    {"fx_character_relief_strength", "0..100 % of the relief strength on the characters",
     [](fx::Settings &s, int v) { s.character_relief_strength = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_character_parallax_depth", "0..1000 % of the characters' parallax depth (2 texels of the 512 map at 100 %)",
     [](fx::Settings &s, int v) { s.character_parallax_depth = static_cast<float>(std::clamp(v, 0, 1000)) / 100.0f; }},
    {"fx_character_skin_colour",
     "0 off, 1 the characters' torso and arms lose their relief where they have their skin colour",
     [](fx::Settings &s, int v) { s.character_skin_colour = on(v); }},
    {"fx_light_linear", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingLinear, v); }},
    {"fx_aerial", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingAerial, v); }},
    {"fx_ff_specialised", "0 every fixed-function draw takes its general program, 1 the program of its stage setup",
     [](fx::Settings &s, int v) { s.ff_specialised = on(v); }},
};

const Option *find_option(std::string_view name)
{
    for (const Option &option : kOptions)
        if (name == option.name)
            return &option;
    return nullptr;
}

// The shadow casters of the last frame for the profile line: per shadow view its caster draws and triangles, then
// the frame's candidates and what the volumes took of them.
std::string caster_counts(const ShadowCasters::FrameCount &count)
{
    std::string views;
    for (const ShadowCasters::ViewCount &v : count.views)
        views += (views.empty() ? "" : ",") + std::to_string(v.view) + ":" + std::to_string(v.draws) + "/" +
                 std::to_string(v.triangles);
    return "caster_views=" + (views.empty() ? "none" : views) +
           " caster_candidates=" + std::to_string(count.candidates) + " casters=" + std::to_string(count.casters) +
           " casters_offscreen=" + std::to_string(count.offscreen) +
           " casters_skinned=" + std::to_string(count.skinned) +
           " casters_unbounded=" + std::to_string(count.unbounded);
}

std::string describe(const fx::Settings &s)
{
    char text[512];
    snprintf(text, sizeof(text),
             "tonemap=%d bloom=%d(1/%u,%u) shafts=%d smaa=%d scene_height=%u sharpen=%d(%.2f) dither=%d flare=%d "
             "scene_grading=%d grade=%d msaa=%u a2c=%d aniso=%u prepass=%d sea=%d%d%d turbidity=%d wakes=%d shadows=%d "
             "lighting=0x%x parallax=%d(%.2f, shadow %d, cavity %d) characters=%d(%.2f) wet=%u flashes=%u",
             s.tonemap, s.bloom, s.bloom_downscale, s.bloom_levels, s.sun_shafts, s.smaa, s.render_height, s.sharpen,
             static_cast<double>(s.sharpness), s.dither, s.lens_flare, s.scene_grading, static_cast<int>(s.grade),
             s.msaa, s.alpha_to_coverage, s.anisotropy, s.depth_prepass, s.sea_band_limit, s.sea_soft_edges,
             s.sea_specular, s.sea_turbidity, s.sea_wakes, static_cast<int>(s.shadows), s.lighting_flags,
             static_cast<int>(s.parallax), static_cast<double>(s.parallax_strength), s.parallax_shadow, s.relief_cavity,
             static_cast<int>(s.character_relief), static_cast<double>(s.character_relief_strength), s.wet_surfaces,
             s.flash_lights);
    return text;
}
} // namespace

Host &Host::instance()
{
    static Host host;
    return host;
}

void Host::on_device_created()
{
    fx::SetBakeGate(&background_wait_reason); // Ray bakes wait as the relief does: a renderer hold, heat (thermal.hpp).
    fx::RendererFacts facts;
    // The game's offscreen depth is a BGFX_TEXTURE_RT texture (TexStore::create_gpu): sampleable. With MSAA
    // the pinned bgfx GL backend blits the depth into that texture too when the frame buffer is resolved
    // (FrameBufferGL::resolve, GL_DEPTH_BUFFER_BIT for readable depth attachments).
    facts.scene_depth = true;
    facts.msaa_depth = true;
    // bgfx has no capability bit for it; the backends apply it only where the device has it.
    const shim::DeviceInfo &device = shim::Context::instance().device_info();
    facts.anisotropy = device.anisotropy;
    if (facts.anisotropy)
        device_max_anisotropy_ = device.max_anisotropy;
    // bgfx's GL backend never sets BGFX_CAPS_FORMAT_TEXTURE_VERTEX: the device's own limit decides.
    support_device_ = fx::DetectSupport(facts);
    device_ready_ = true;
    // Nothing is drawn yet: the device starts on the plain legacy path (no effect, no MSAA, not even «Низкая»'s
    // upscale) and switches to the request like any later change (advance_switch), once its programs exist and the
    // self-test passed. Which programs draw the fixed-function stages is no effect: the request's choice from the
    // start.
    fx::Settings start;
    start.ff_specialised = requested_.ff_specialised;
    activate(start, fx::Support{});
    apply();
    renderer_state_changed_ = false; // the scene target is created after this, with the active state
}

void Host::shutdown()
{
    post_.Shutdown();
    self_test_.Shutdown();
    lighting_.shutdown();
    sky_.Shutdown();
    sea_depth_.Shutdown();
    sea_wakes_.Shutdown();
    sea_wakes_ready_ = false;
    sea_wakes_frame_ = UINT32_MAX;
    for (bgfx::UniformHandle *uniform : {&u_sea_fx_, &s_sea_depth_, &s_sea_sun_road_, &u_sea_wake_, &s_sea_wake_})
        if (bgfx::isValid(*uniform))
        {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    if (bgfx::isValid(sea_no_sun_road_))
    {
        bgfx::destroy(sea_no_sun_road_);
        sea_no_sun_road_ = BGFX_INVALID_HANDLE;
    }
    sea_depth_held_ = {};
    release_sea_backdrop();
    place_.shutdown();
    ao_bound_ = nullptr;
    lighting_.set_ao_volume(nullptr);
    lighting_.set_visibility(nullptr);
    fx::ShutdownBakes(); // the bakes above were let go of: cancelled, the bake thread joined (at once, also while hot)
    device_ready_ = false;
}

render::EffectsReport Host::configure(int preset, const std::vector<render::EffectsOption> &options)
{
    InvalidateSceneFrames();
    preset_ = std::clamp(preset, 0, 2);
    options_ = options;
    apply();
    return report_;
}

void Host::apply()
{
    report_ = {};
    report_.preset = preset_;
    requested_ = fx::Settings::FromPreset(static_cast<fx::QualityPreset>(preset_));
    // «Обработка текстур» (the texture_* keys; a key left out is off), before the developer fx_* keys.
    fx::TextureProcessing processing;
    for (const auto &option : options_)
        if (option.value >= 0 && (option.name == "texture_normal_maps" || option.name == "texture_height_maps" ||
                                  option.name == "texture_upscale"))
            (option.name == "texture_normal_maps"   ? processing.normal_maps
             : option.name == "texture_height_maps" ? processing.height_maps
                                                    : processing.upscale) = option.value != 0;
    fx::ApplyTextureProcessing(requested_, processing);
    bool profile = false;
    caps_profile_ = fx::CapsProfile::Device;
    for (const auto &option : options_)
    {
        if (option.value < 0)
            continue; // the preset's value
        else if (option.name == "fx_gpu_profile")
            profile = option.value != 0;
        else if (option.name == "texture_normal_maps" || option.name == "texture_height_maps" ||
                 option.name == "texture_upscale")
            continue; // applied above
        else if (option.name == "fx_caps_profile")
            caps_profile_ =
                static_cast<fx::CapsProfile>(std::clamp(option.value, 0, static_cast<int>(fx::CapsProfile::Count) - 1));
        else if (const Option *known = find_option(option.name))
            known->apply(requested_, option.value);
        else
            report_.errors.push_back("unknown graphics option '" + option.name + "'");
    }
    for (const auto &error : report_.errors)
        render::Log().error("effects: {}", error);
    if (device_ready_ && profile != profile_)
        render::Log().info("effects profile: {} (fx_gpu_profile)", profile ? "on" : "off");
    profile_ = device_ready_ && profile;

    fx::Settings next = requested_;
    if (device_ready_)
    {
        // What the device can do, narrowed by a forced GPU-family profile (fx_caps_profile, development) and by
        // the stages the self-test found broken on this device.
        target_support_ = fx::RestrictSupport(support_device_, caps_profile_);
        if (caps_profile_ != fx::CapsProfile::Device)
            report_.changes.push_back(std::string("caps profile forced: ") + fx::CapsProfileName(caps_profile_));
        if (msaa_broken_)
        {
            target_support_.msaa_samples = 1;
            target_support_.msaa_depth = false;
        }
        if (post_broken_)
            target_support_.lut_3d = false;
        next = fx::ConstrainSettings(next, target_support_, report_.changes);
        report_.support = target_support_.Describe();
        // Options the device can never honour, for the settings screen.
        fx::Settings all = fx::Settings::FromPreset(fx::QualityPreset::High);
        all.smaa = true;
        std::vector<std::string> unused;
        const fx::Settings possible = fx::ConstrainSettings(all, target_support_, unused);
        if (possible.msaa <= 1)
            report_.disabled.push_back("fx_msaa");
        if (possible.anisotropy <= 1)
            report_.disabled.push_back("fx_anisotropy");
        if (!possible.smaa && !possible.PostIsIdentity())
            report_.disabled.push_back("fx_smaa");
        if (possible.render_height == 0) // no post chain: no upscale, no RCAS
            for (const char *name : {"fx_render_height", "fx_sharpen", "fx_sharpness"})
                report_.disabled.push_back(name);
        if (possible.PostIsIdentity())
            for (const char *name : {"fx_tonemap", "fx_bloom", "fx_sun_shafts", "fx_smaa", "fx_dither", "fx_lens_flare",
                                     "fx_scene_grading", "fx_grade"})
                report_.disabled.push_back(name);
    }
    const bool changed = !(next == target_) || !device_ready_;
    const fx::RendererState state = fx::RendererStateFor(next);
    if (device_ready_ && state.max_anisotropy > 1 && device_max_anisotropy_ < state.max_anisotropy)
        report_.changes.push_back("anisotropic filtering " + std::to_string(state.max_anisotropy) +
                                  "x: the device allows " + std::to_string(static_cast<int>(device_max_anisotropy_)) +
                                  "x");
    report_.summary = describe(next);
    target_ = next;
    if (!device_ready_)
        activate(next, target_support_); // nothing is drawn before the device exists
    else if (!(target_ == settings_) && !switching_)
    {
        // The current settings keep drawing until the new ones are ready (advance_switch).
        switching_ = true;
        switch_started_ = std::chrono::steady_clock::now();
        switch_frames_ = 0;
        switch_programs_ = 0;
        report_.prepared_programs = 0;
    }
    else if (target_ == settings_)
    {
        switching_ = false;
        support_ = target_support_; // the same settings, also valid on the new support (ConstrainSettings left them)
    }
    report_.pending = switching_;
    if (changed)
    {
        render::Log().info(
            "effects: preset {} ({}){}, texture processing: normal maps {}, height maps {}{}, upscale x2 {}: {}",
            preset_, std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))),
            switching_ ? " requested, prepared while the current settings draw" : "",
            processing.normal_maps ? "on" : "off", processing.height_maps ? "on" : "off",
            processing.height_maps && !processing.normal_maps ? " (not drawn without the normal maps)" : "",
            processing.upscale ? "on" : "off", report_.summary);
        for (const auto &line : report_.changes)
            render::Log().info("effects: preset {} on this device: {}", preset_, line);
    }
}

void Host::activate(const fx::Settings &settings, const fx::Support &support)
{
    settings_ = settings;
    render::far_lod::pixels = settings_.far_lod_pixels;
    support_ = support;
    fx::RendererState state = fx::RendererStateFor(settings_);
    renderer_state_changed_ =
        renderer_state_changed_ || (device_ready_ && (state.msaa_samples != renderer_state_.msaa_samples ||
                                                      state.render_height != renderer_state_.render_height ||
                                                      state.max_anisotropy != renderer_state_.max_anisotropy));
    renderer_state_ = state;
    // The level of every anisotropic sampler (patches/bgfx-anisotropy-level.patch: one cap per backend), applied
    // at the next reset.
    bgfx::gl::g_stormMaxAnisotropy.store(static_cast<float>(state.max_anisotropy), std::memory_order_relaxed);
    bgfx::vk::g_stormMaxAnisotropy.store(static_cast<float>(state.max_anisotropy), std::memory_order_relaxed);
    lighting_.configure(settings_, renderer_state_, support.lamp_depth);
    upscales().configure(settings_.upscale, settings_.upscale_budget_mb);
    // One line per change of what runs in the background: «Низкая» makes the far LODs only, no bake and no texture.
    const fx::BackgroundProducts products = fx::BackgroundProductsFor(settings_);
    if (!products_logged_ || !(products == products_))
        render::Log().info("effects: background products for preset {} ({}): {}", preset_,
                           std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))), products.Describe());
    products_ = products;
    products_logged_ = true;
}

bool Host::needs_self_test(const fx::Settings &settings)
{
    return !settings.PostIsIdentity() || settings.render_height > 0 || fx::RendererStateFor(settings).msaa_samples > 1;
}

void Host::set_weather(const render::EffectsWeather &weather)
{
    weather_ = weather;
    weather_this_frame_ = true;
    update_wetness();
    // The hemisphere ambient takes its tint from the sky dome; the aerial perspective needs a sky (no interiors).
    lighting_.shadows().set_sun(weather.sun_dir, weather.sun_present);
    lighting_.set_weather_sun(weather.sun_dir, weather.sun_color, weather.sun_present);
    if (settings_.lighting_flags & (fx::kLightingHemisphere | fx::kLightingAerial))
        lighting_.set_sky(&sky_state());
}

void Host::update_wetness()
{
    const auto now = std::chrono::steady_clock::now();
    // Real time between weather frames, at most half a second: a long frame (a hitch, a save) does not dry the place.
    const float seconds = std::min(std::chrono::duration<float>(now - wetness_time_).count(), 0.5f);
    wetness_time_ = now;
    wetness_.Update(weather_.rain, seconds);
    WetFrame wet;
    if (settings_.wet_surfaces)
    {
        wet.level = settings_.wet_force >= 0.0f ? std::min(settings_.wet_force, 1.0f) : wetness_.Level();
        // The fog colour is the sky's at the horizon by day; the night sky the surfaces reflect is darker than its fog.
        const float t = std::clamp((weather_.sky_sun_dir[1] + 0.1f) / 0.25f, 0.0f, 1.0f);
        const float day = t * t * (3.0f - 2.0f * t);
        for (int c = 0; c < 3; ++c)
            wet.sky[c] = weather_.fog_color[c] * (0.35f + 0.65f * day);
    }
    lighting_.set_wet(wet);
}

const fx::SkyState &Host::sky_state()
{
    // One sky state for the fx sky and water, the scene grade and the hemisphere ambient. ComputeSky costs ~2 ms on
    // the host; Sky::Update rebuilds it only when the weather moves (SkyNeedsRebuild), not every frame.
    update_sky(camera_height_);
    return sky_.State();
}

fx::WeatherInput Host::weather_input() const
{
    fx::WeatherInput input;
    input.sun_dir = weather_.sky_sun_dir;
    input.sun_color = weather_.sun_color;
    input.ambient_color = weather_.ambient;
    input.fog_color = weather_.fog_color;
    input.fog_density = weather_.fog_density;
    input.overcast = weather_.overcast;
    input.wind = weather_.wind_dir;
    input.wind_speed = weather_.wind_speed;
    return input;
}

uint16_t Host::submit_post(const PostTargets &targets)
{
    post_submitted_ = true;
    fx::PostChain::Frame frame;
    frame.scene_color = targets.scene_color;
    frame.scene_width = targets.width;
    frame.scene_height = targets.height;
    frame.target = targets.target;
    frame.target_width = targets.target_width;
    frame.target_height = targets.target_height;
    frame.scene_depth = targets.scene_depth;
    last_post_size_ = {targets.width, targets.height, targets.target_width, targets.target_height};
    frame.first_view = targets.first_view;
    frame.flip_v = false; // the target is the game's next offscreen picture, same row order as the scene
    frame.wobble = {.on = wobble_.on, .amplitude = wobble_.amplitude, .phase = wobble_.phase};

    // A frame without weather has no sky (interiors).
    const bool weather = has_weather();
    if (weather && weather_.sun_present)
    {
        // Sun on the screen (lens flare, shafts): the direction through the camera rotation and the projection.
        const auto &v = targets.view;
        const auto &p = targets.projection;
        const auto &d = weather_.sun_dir;
        float e[3];
        for (int c = 0; c < 3; ++c)
            e[c] = d[0] * v[0 * 4 + c] + d[1] * v[1 * 4 + c] + d[2] * v[2 * 4 + c];
        float clip[4];
        for (int c = 0; c < 4; ++c)
            clip[c] = e[0] * p[0 * 4 + c] + e[1] * p[1 * 4 + c] + e[2] * p[2 * 4 + c];
        if (clip[3] > 1e-4f)
        {
            const float x = clip[0] / clip[3], y = clip[1] / clip[3];
            // fx takes the uv with the origin at the top-left of the picture on a top-left-origin backend; the
            // shim's targets keep D3D's row order on every backend, which on GL is fx's flipped convention.
            const float v_top = 0.5f - 0.5f * y;
            frame.sun_uv = {0.5f + 0.5f * x, bgfx::getCaps()->originBottomLeft ? 1.0f - v_top : v_top};
            // Fades in as the sun clears the horizon; clouds hide it.
            frame.sun_visibility =
                std::clamp(weather_.sun_dir[1] * 8.0f, 0.0f, 1.0f) * (1.0f - std::clamp(weather_.overcast, 0.0f, 1.0f));
        }
        frame.sun_color = weather_.sun_color;
        frame.sun_glare = sun_glare_;
        frame.sun_ghosts = sun_ghosts_;
    }
    if (settings_.scene_grading)
        frame.scene_grade = fx::SceneGrade(sky_state(), weather_.overcast, weather ? 0.0f : 1.0f);
    frame.scene_cut = scene_cut_pending_;
    scene_cut_pending_ = false;
    const uint16_t used = post_.Submit(settings_, frame);
    last_post_first_ = targets.first_view;
    last_post_views_ = used;
    return used;
}

void Host::advance_switch()
{
    shim::Device *device = shim::Device::current;
    if (!switching_ || !device_ready_ || !device || self_test_.Running())
        return;
    ++switch_frames_;
    // The programs of the new settings, one per frame: each is a shader link (tens of milliseconds on a phone,
    // seconds on a first run under ANGLE); linked all at once in the first frame of the new settings they held
    // the picture for seconds. While the previous frame's programs all came from the program cache (loaded, none
    // linked: storm/render/program_cache.hpp) a few more go per frame.
    const render::ProgramCacheStats cache = render::CurrentProgramCacheStats();
    const bool from_cache = cache.loaded != switch_cache_.loaded && cache.stored == switch_cache_.stored;
    switch_cache_ = cache;
    const int per_frame = from_cache ? kCachedProgramsPerFrame : kProgramsPerFrame;
    int budget = per_frame;
    // The post chain's programs also on «Низкая»: the underwater wobble runs on every preset.
    // The sea's backdrop draws with the sun's caster programs (world.cpp).
    const bool sea_depth = target_.sea_soft_edges || target_.sea_turbidity;
    const bool ready =
        post_.Prepare(target_, budget) &&
        (!sea_depth ||
         (sea_depth_.Prepare(budget) && lighting_.casters().prepare_programs(*device, Casters::Pass::Sun, budget))) &&
        (!target_.sea_wakes || sea_wakes_.Prepare(budget)) && lighting_.prepare_programs(*device, target_, budget);
    switch_programs_ += per_frame - budget;
    report_.prepared_programs = switch_programs_;
    if (!ready)
        return;
    // The effect path of the new settings on a known picture (views kPostViewBegin..), once per settings: a
    // settings set that passed on this device is not tested again.
    if (needs_self_test(target_) && std::find(verified_.begin(), verified_.end(), target_) == verified_.end())
    {
        tested_ = target_;
        self_test_.Submit(tested_, render::kPostViewBegin, post_);
        return;
    }
    switching_ = false;
    report_.pending = false;
    activate(target_, target_support_);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - switch_started_).count();
    render::Log().info("effects: preset {} ({}) active after {} frames, {:.0f} ms ({} programs prepared)", preset_,
                       std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))), switch_frames_, ms,
                       switch_programs_);
}

uint64_t Host::release_post_targets()
{
    // Between frames: frames_without_post_ is 0 when the last presented frame ran the chain.
    return post_.ReleaseTargets(post_submitted_ || frames_without_post_ == 0 ? 1 : 0);
}

void Host::end_frame(bool scene_frame, uint32_t frame)
{
    release_idle_post();
    // The renderer sees every perspective scene draw, including unlit/low-preset geometry and the sea; a place that
    // loads draws its scene under the loading cover, unseen.
    ReliefGenerator::instance().frame(scene_frame, ScenePreparing());
    share_background_work();
    poll_self_test(frame);
    if (!scene_frame)
        scene_cut_pending_ = true;
    end_weather_frame();
    sun_glare_ = sun_ghosts_ = 0.0f;
    const std::array<int, 2> draw_shadows{lighting_.shadows().initial_state(!lighting_.lamp_shadows().interior()),
                                          lighting_.lamp_shadows().initial_state()};
    lighting_.end_frame();
    upscales().end_frame(SceneUploadScope());
    update_place();
    // All next-frame producers have now pumped. Shadow preparation was captured before end_frame reset it.
    SampleSceneFrame(scene_frame, frame, static_cast<ShadowState>(draw_shadows[0]),
                     static_cast<ShadowState>(draw_shadows[1]));
    transform_view_ = UINT16_MAX; // the shim's views start again next frame
    times_ = {};
    if (profile_)
        profile_frame();
    last_post_views_ = 0;
}

void Host::release_idle_post()
{
    frames_without_post_ = post_submitted_ ? 0 : frames_without_post_ + 1;
    post_submitted_ = false;
    if (frames_without_post_ == kPostIdleFrames)
        if (const uint64_t released = post_.ReleaseTargets(0))
            render::Log().info("effects: post chain: released {:.1f} MB of targets, no scene for {} frames",
                               static_cast<double>(released) / (1024.0 * 1024.0), kPostIdleFrames);
}

void Host::share_background_work()
{
    // The background work's share while the game is played follows the device's heat (thermal.hpp); a place that
    // loads is not played, and takes every core as before. Every 30 s, with the frame pacing and memory summaries,
    // the thermal reading and that share are logged whether or not they changed.
    const ThermalLoad heat = thermal_load();
    fx::CpuAdmission &admission = fx::SharedCpuAdmission();
    const unsigned threads = background_threads(heat, admission.normal_gameplay_threads());
    if (admission.set_gameplay_limit(threads))
        render::Log().info("effects: background work while the game is played: {} of {} cores (thermal load {}: {})",
                           threads, admission.maximum_threads(), thermal_load_name(heat), thermal_text());
    background_play(!ScenePreparing());
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_thermal_summary_)
    {
        next_thermal_summary_ = now + render::kSummaryPeriod;
        render::Log().info("effects: thermal status: {}; background work while the game is played: {} of {} cores",
                           thermal_text(), threads, admission.maximum_threads());
    }
}

void Host::poll_self_test(uint32_t frame)
{
    if (self_test_.Running())
    {
        const fx::SelfTest::Result result = self_test_.Poll(frame);
        if (result.done && result.failed == fx::SelfTest::Stage::None)
        {
            render::Log().info("effects self-test passed on {} (preset {}, MSAA {}x)",
                               shim::Context::instance().device_info().renderer, preset_,
                               fx::RendererStateFor(tested_).msaa_samples);
            verified_.push_back(tested_);
        }
        if (result.done && result.failed != fx::SelfTest::Stage::None)
        {
            const bool scene = result.failed == fx::SelfTest::Stage::SceneTarget;
            render::Log().error("effects self-test failed on {} ({}): {}. {} switched off; {}",
                                shim::Context::instance().device_info().renderer,
                                bgfx::getRendererName(bgfx::getRendererType()), result.detail,
                                scene ? "MSAA" : "the post-processing chain", support_.Describe());
            // Like a missing capability: ConstrainSettings switches the stage off and reports it.
            (scene ? msaa_broken_ : post_broken_) = true;
            notice_ = std::string("Графика: ") + (scene ? "сглаживание MSAA" : "постобработка") +
                      " не работает на этом GPU и отключена.";
            apply();
        }
    }
}

void Host::end_weather_frame()
{
    if (!weather_this_frame_ && !weather_last_frame_)
    {
        // Two frames without the weather: an interior or a loading screen. Nothing is wet there, and the next
        // place outdoors starts at its own rain.
        wetness_.Interrupt();
        lighting_.set_wet({});
    }
    weather_last_frame_ = weather_this_frame_;
    if (!weather_this_frame_)
    {
        lighting_.set_sky(nullptr);                             // interiors: no sky tint on the hemisphere
        lighting_.shadows().set_sun({0.0f, 1.0f, 0.0f}, false); // and no sun shadows
    }
    weather_this_frame_ = false;
}

void Host::update_place()
{
    // The place's bakes, for the next frame's draws: its probes are read by the lit draws (Низкая has none, so it
    // bakes none; fx_visibility=0 switches them off for comparisons), an interior's occlusion lobes by
    // fx_ao_volume, a town's sky and rays by the town light.
    TownLight &town = lighting_.town_light();
    place_.update(products_.visibility || products_.ao_volume, town.needs_rays());
    town.set_place(place_.ready());
    // Our lightmaps for the hour's sun, for the next frame's draws (the pre-game views of the next frame). The
    // sun is the last weather's: a frame without weather (an interface over the place, a dialogue) is no night,
    // and the lightmaps are not composed for one. A place that is left takes its lightmaps with it
    // (set_lightmaps), an interior has none.
    town.update(weather_.sun_dir, weather_.sun_present);
    if (shim::Device *device = shim::Device::current; device && device_ready_)
        lighting_.relight_town(*device);
    const Visibility::Bound *volume = products_.visibility ? place_.visibility() : nullptr;
    lighting_.set_visibility(volume);
    ao_bound_ = products_.ao_volume ? place_.ao_volume() : nullptr;
    lighting_.set_ao_volume(ao_bound_);
    // Wet surfaces stay dry where the volume shows no open sky: without it (still baking, or failed) the rain
    // reaches under the roofs too. Said once until a volume is there.
    if (volume)
        wet_unsheltered_logged_ = false;
    else if (lighting_.wet().level > 0.0f && render::Once(wet_unsheltered_logged_))
    {
        static const char *const kPlaceStates[] = {"no location geometry", "baking", "ready", "its bake failed"};
        render::Log().info("effects: wet surfaces without a visibility volume ({}): wet under roofs too",
                           kPlaceStates[static_cast<int>(place_.state())]);
    }
}

void Host::profile_frame()
{
    // GPU timer queries (option fx_gpu_profile=1): the whole frame and the post chain, averaged and logged every
    // kProfileFrames presented frames with cpu_frame_ms (bgfx cpuTimeFrame: the interval from frame to frame, the waits
    // for the render thread and the display's slot included; not CPU work), the game thread's work per phase and
    // entity class (platform/frame_work.hpp) and draw calls. Timer results lag a few frames; a backend without timer
    // queries reports none (logged as n/a). The per-view times come from the frames the shim samples for its
    // "gpu passes:" line with detailed diagnostics (shim/gpu_passes.hpp); a view's time is its latest result.
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats)
    {
        if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
        {
            const double to_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
            times_.frame_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_ms;
            profile_in_scene_ = false;
            profile_view_frames_ += stats->numViews > 0;
            for (uint16_t i = 0; i < stats->numViews; ++i)
            {
                const bgfx::ViewStats &view = stats->viewStats[i];
                profile_views_[view.name] += static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * to_ms;
                // The scene target's views, and its passes: runs of them that no other target's view cuts (a cut
                // stores, resolves and loads the multisampled scene on a tile GPU). In the frame's view order.
                const bool scene = std::strncmp(view.name, "scene", 5) == 0; // shim::Device::name_view
                profile_scene_views_ += scene;
                profile_scene_passes_ += scene && !profile_in_scene_;
                profile_in_scene_ = scene;
                // The pass of fx::SeaWakes (SetupView).
                if (std::strncmp(view.name, "fx sea wakes", 12) == 0)
                    times_.sea_wakes_ms += static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * to_ms;
                if (last_post_views_ && view.view >= last_post_first_ &&
                    view.view < last_post_first_ + last_post_views_)
                {
                    const double ms = static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * to_ms;
                    times_.post_ms += ms;
                    // The pass names of fx::PostChain (SetupView).
                    if (std::strncmp(view.name, "fx smaa", 7) == 0)
                        times_.smaa_ms += ms;
                    else if (std::strcmp(view.name, "fx fsr easu") == 0)
                        times_.easu_ms += ms;
                    else if (std::strcmp(view.name, "fx fsr rcas") == 0)
                        times_.rcas_ms += ms;
                }
            }
            times_.post_views = last_post_views_;
            profile_sum_frame_ += times_.frame_ms;
            profile_sum_post_ += times_.post_ms;
            profile_sum_smaa_ += times_.smaa_ms;
            profile_sum_easu_ += times_.easu_ms;
            profile_sum_rcas_ += times_.rcas_ms;
            profile_sum_sea_wakes_ += times_.sea_wakes_ms;
            ++profile_gpu_frames_;
        }
        profile_sum_cpu_ +=
            static_cast<double>(stats->cpuTimeFrame) * 1000.0 / static_cast<double>(stats->cpuTimerFreq);
        profile_draws_ += stats->numDraw;
        if (++profile_count_ == kProfileFrames)
        {
            char gpu[256] = "gpu_frame_ms=n/a gpu_post_ms=n/a (no timer queries)";
            if (profile_gpu_frames_ > 0)
            {
                // The frame over every frame of the line; the passes over the frames that had per-view results.
                const double views = std::max(profile_view_frames_, 1);
                snprintf(gpu, sizeof(gpu),
                         "gpu_frame_ms=%.2f gpu_post_ms=%.2f gpu_smaa_ms=%.2f gpu_easu_ms=%.2f gpu_rcas_ms=%.2f "
                         "gpu_sea_wakes_ms=%.3f gpu_view_frames=%d",
                         profile_sum_frame_ / profile_gpu_frames_, profile_sum_post_ / views, profile_sum_smaa_ / views,
                         profile_sum_easu_ / views, profile_sum_rcas_ / views, profile_sum_sea_wakes_ / views,
                         profile_view_frames_);
            }
            // The GPU time per view name over the line's frames, the eight largest: "name:ms,..." per frame.
            std::string views;
            {
                std::vector<std::pair<double, std::string>> order;
                for (const auto &[name, ms] : profile_views_)
                    order.emplace_back(ms, name.empty() ? "unnamed" : name);
                std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
                for (size_t i = 0; i < std::min<size_t>(order.size(), 8) && profile_view_frames_ > 0; ++i)
                {
                    std::replace(order[i].second.begin(), order[i].second.end(), ' ', '_');
                    std::replace(order[i].second.begin(), order[i].second.end(), ',', ';');
                    char ms[24];
                    snprintf(ms, sizeof(ms), ":%.2f", order[i].first / profile_view_frames_);
                    views += (i ? "," : "") + order[i].second + ms;
                }
                profile_views_.clear();
            }
            if (views.empty())
                views = "n/a";
            const Lighting::Counts counts = lighting_.last_frame();
            // The sun's layers over the kProfileFrames frames of this line: how often the static one was drawn (and
            // with how much), in how many frames the dynamic one, its casters.
            const SunShadows::Counters sun = lighting_.shadows().take_counters();
            // The towns' relight since the start: the place's sun map drawn, ratio maps composed.
            const TownLight::SunCounters town = lighting_.town_light().sun_counters();
            render::Log().info(
                "effects profile: preset {} scene={}x{} display={}x{} {} cpu_frame_ms={:.2f} draws={:.0f} {} "
                "thermal={} background_cpu={}/{} scene_lines={} scene_views={:.1f} scene_passes={:.1f} "
                "sea_backdrop_draws={:.0f} gpu_views={} post_views={} lit_draws={} baked_draws={} coverage_draws={} "
                "relief_draws={} parallax_draws={} gloss_draws={} aerial_draws={} shadowed_draws={} "
                "sun_ground_draws={} ao_draws={} lamp_draws={} lamp_slots={} lamp_static_faces={} "
                "sun_static_redraws={} sun_static_tiles={} sun_static_triangles={} sun_dynamic_frames={} "
                "sun_dynamic_casters={} lightmapped_draws={} town_relit_draws={} town_ratio_missed={} "
                "town_place_draws={} town_composes={} town_wall_draws={} wet_draws={} compound_uploads={} "
                "compound_bytes={} compound_members_bound={} compound_fallbacks={} flash_events={} flashes={} "
                "flashed_draws={} {}{}",
                preset_, last_post_size_[0], last_post_size_[1], last_post_size_[2], last_post_size_[3], gpu,
                profile_sum_cpu_ / kProfileFrames, static_cast<double>(profile_draws_) / kProfileFrames,
                storm::platform::frame_work::Describe(
                    storm::platform::frame_work::Take(storm::platform::frame_work::Reader::Profile)),
                thermal_text(), fx::SharedCpuAdmission().stats().reserved, fx::SharedCpuAdmission().stats().phase_cap,
                renderer_state_.render_height,
                profile_view_frames_ ? static_cast<double>(profile_scene_views_) / profile_view_frames_ : 0.0,
                profile_view_frames_ ? static_cast<double>(profile_scene_passes_) / profile_view_frames_ : 0.0,
                static_cast<double>(sea_backdrop_draws_) / kProfileFrames, views, last_post_views_, counts.lit,
                counts.baked, counts.coverage, counts.relief, counts.parallax, counts.glossy, counts.aerial,
                counts.shadowed, counts.ground, counts.ao, counts.lamp_lit, lighting_.lamp_shadows().last_slots(),
                lighting_.lamp_shadows().last_static_faces(), sun.static_redraws, sun.static_tiles,
                sun.static_triangles, sun.dynamic_frames, sun.dynamic_casters, lighting_.town_light().last_counts()[0],
                lighting_.town_light().last_counts()[1], lighting_.town_light().last_counts()[2], town.place_draws,
                town.composes, counts.town_walls, counts.wet, counts.aggregate_uploads, counts.aggregate_bytes,
                counts.aggregate_members, counts.aggregate_fallbacks, lighting_.take_flash_events(), counts.flashes,
                counts.flashed, caster_counts(lighting_.shadow_casters().last()),
                render::far_lod::Take(kProfileFrames));
            profile_count_ = profile_gpu_frames_ = profile_view_frames_ = 0;
            sea_backdrop_draws_ = 0;
            profile_scene_views_ = profile_scene_passes_ = 0;
            profile_sum_frame_ = profile_sum_post_ = profile_sum_cpu_ = 0.0;
            profile_sum_smaa_ = profile_sum_easu_ = profile_sum_rcas_ = profile_sum_sea_wakes_ = 0.0;
            profile_draws_ = 0;
        }
    }
}

} // namespace storm::effects

namespace storm::render
{
const std::vector<EffectsOptionInfo> &EffectsOptionNames()
{
    static const std::vector<EffectsOptionInfo> names = [] {
        std::vector<EffectsOptionInfo> list{
            {"fx_preset", "0 Низкая, 1 Средняя, 2 Высокая"},
            {"fx_caps_profile", "0 device, 1 gles30-minimal, 2 mali-g5x, 3 powervr (development)"},
            {"fx_gpu_profile", "1: log GPU timer averages every 30 frames (measurements)"}};
        for (const auto &option : effects::kOptions)
            list.push_back({option.name, option.values});
        return list;
    }();
    return names;
}

EffectsReport ConfigureEffects(int preset, const std::vector<EffectsOption> &options)
{
    return effects::Host::instance().configure(preset, options);
}

void EffectsSceneComplete()
{
    if (shim::Device *device = shim::Device::current)
        device->scene_complete();
}

void SetEffectsTechnique(const char *name)
{
    effects::Host::instance().lighting().set_technique(name);
}

void SetEffectsSeaFrame(const EffectsSeaFrame &frame)
{
    effects::Host::instance().set_sea_frame(frame);
}

void SetEffectsWorldOrigin(const glm::vec3 &origin)
{
    effects::Host::instance().lighting().set_world_origin(origin);
    effects::generation_focus().set_origin(origin);
}

void SetEffectsPlayerPosition(const glm::vec3 &position, const void *owner, EffectsPlayerRole role)
{
    effects::generation_focus().set_player(
        position, owner, role == EffectsPlayerRole::Ship ? effects::PlayerRole::Ship : effects::PlayerRole::Character);
}

void ClearEffectsPlayerPosition(const void *owner)
{
    effects::generation_focus().clear_player(owner);
}

void SetEffectsLightmaps(std::string_view name, std::span<const glm::vec3> positions,
                         std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                         std::span<const uint32_t> triangle_lightmap, std::span<const EffectsLightmap> lightmaps)
{
    effects::Host::instance().lighting().town_light().set_lightmaps(name, positions, uvs, indices, triangle_lightmap,
                                                                    lightmaps);
    // The lightmapped ground is where one walks: the place's probes cover it.
    glm::vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const uint32_t i : indices)
        if (i < positions.size())
        {
            lo = glm::min(lo, positions[i]);
            hi = glm::max(hi, positions[i]);
        }
    effects::Host::instance().place().set_focus(name, lo, hi);
}

void SetEffectsLocationGeometry(const EffectsLocationGeometry &geometry)
{
    effects::CurrentSceneReveal().restart_frames();
    for (const auto &surface : geometry.surfaces)
        effects::ObserveSceneTexture(shim::shared_store_of(surface.texture));
    effects::generation_focus().set_scene(geometry.name);
    effects::Host::instance().location_changed();
    effects::Host::instance().lighting().lamp_shadows().set_geometry(geometry);
    effects::Host::instance().place().set_geometry(geometry);
    // Outdoors the still geometry casts the sun's shadows (in tiles, with its textures' holes); an interior has no sun.
    if (geometry.interior)
        effects::Host::instance().lighting().shadows().set_casters({});
    else
        effects::Host::instance().lighting().shadows().set_casters(geometry);
}

namespace
{
// A game light (D3D range and attenuation) with the one falloff of all lights (fx/light_falloff.hpp).
fx::Lamp ShapedLamp(const EffectsPointLight &l)
{
    const fx::LightShape shape =
        fx::ShapeOfGameLight(l.range, l.att0, l.att1, l.att2, effects::Host::instance().settings().light_falloff);
    return {l.position, l.color, shape.range, shape.intensity, shape.core};
}
} // namespace

std::shared_ptr<const fx::AggregateSnapshot> ShapeEffectsAggregates(fx::AggregateSnapshot snapshot)
{
    static uint64_t generation = 0;
    snapshot.generation = ++generation;
    const auto &falloff = effects::Host::instance().settings().light_falloff;
    for (auto &g : snapshot.groups)
    {
        g.shape = fx::ShapeOfGameLight(g.authored_range, g.attenuation[0], g.attenuation[1], g.attenuation[2], falloff);
        g.support = 0;
        for (uint32_t i = 0; i < g.count; ++i)
            g.support =
                std::max(g.support, glm::distance(g.center, snapshot.members[g.first + i].position) + g.shape.range);
    }
    return std::make_shared<const fx::AggregateSnapshot>(std::move(snapshot));
}

bool EffectsAggregateLightsAvailable()
{
    return effects::Host::instance().lighting().aggregate_available();
}

void SetEffectsLightAggregate(uint32_t index, const fx::AggregateRef &ref)
{
    if (auto *device = shim::Device::current; device && index < device->lights.size() && index < 256 &&
                                              device->lights[index].light.Type == D3DLIGHT_POINT && ref.get() &&
                                              ref.snapshot->valid())
        device->lights[index].aggregate = ref;
}

void SetEffectsAggregateLamps(std::shared_ptr<const fx::AggregateSnapshot> snapshot)
{
    std::vector<fx::Lamp> lamps;
    std::vector<EffectsLampFlame> flames;
    const bool compound = snapshot && snapshot->valid() && EffectsAggregateLightsAvailable();
    static bool reported = false;
    if (snapshot && !compound && render::Once(reported))
        render::Log().info("compound lights: off (mode, device or capacity): lights unmerged, no compound shadows");
    if (snapshot)
        for (uint32_t n = 0; n < snapshot->groups.size(); ++n)
        {
            const auto &g = snapshot->groups[n];
            if (g.moving)
                continue;
            if (compound)
            {
                const auto &m = snapshot->members[g.first];
                fx::Lamp lamp{g.center, m.color, g.support, g.shape.intensity, g.shape.core};
                lamp.aggregate = {snapshot, n};
                const uint32_t index = static_cast<uint32_t>(lamps.size());
                lamps.push_back(lamp);
                for (uint32_t i = 0; i < g.count; ++i)
                    flames.push_back({snapshot->members[g.first + i].position, index});
            }
        }
    // Unsupported snapshots deliberately cast no compound shadows. Raw originals remain in D3D lighting.
    effects::Host::instance().lighting().lamp_shadows().set_lamps(lamps, flames);
}

void SetEffectsDynamicLights(std::span<const EffectsPointLight> lights)
{
    fx::PointLight converted[fx::kMaxPointLights * 4];
    const size_t count = std::min(lights.size(), std::size(converted));
    for (size_t i = 0; i < count; ++i)
    {
        const fx::Lamp shaped = ShapedLamp(lights[i]);
        converted[i] = {.position = shaped.position,
                        .color = shaped.color,
                        .range = shaped.range,
                        .intensity = shaped.intensity,
                        .core = shaped.core};
    }
    effects::Host::instance().lighting().set_dynamic_lights(std::span(converted, count));
    // The same lights for the lamps: the nearest to the camera take a moving slot and cast shadows.
    std::vector<fx::Lamp> moving;
    moving.reserve(lights.size());
    for (const EffectsPointLight &l : lights)
        moving.push_back(ShapedLamp(l));
    effects::Host::instance().lighting().lamp_shadows().set_moving(std::move(moving));
}

void SetEffectsShipLights(std::span<const EffectsShip> ships, std::span<const EffectsShipLight> lights)
{
    std::vector<effects::LampShadows::ShipLight> converted;
    converted.reserve(lights.size());
    for (const EffectsShipLight &l : lights)
        converted.push_back({ShapedLamp(l.light), l.ship, l.local});
    effects::Host::instance().lighting().lamp_shadows().set_ship_lights({ships.begin(), ships.end()},
                                                                        std::move(converted));
}

bool EffectsShipGeometryWanted(uint64_t ship, uint64_t signature)
{
    return effects::Host::instance().lighting().lamp_shadows().ship_geometry_wanted(ship, signature);
}

void SetEffectsShipGeometry(uint64_t ship, uint64_t signature, const EffectsShipGeometry &geometry)
{
    effects::Host::instance().lighting().lamp_shadows().set_ship_geometry(ship, signature, geometry);
}

namespace
{
// The frame's shadow volumes for the picture the device is drawing now; null: none.
effects::ShadowCasters *FrameShadowVolumes()
{
    effects::Lighting &lighting = effects::Host::instance().lighting();
    shim::Device *device = shim::Device::current;
    if (!device || lighting.idle() || !lighting.shadow_volumes(*device))
        return nullptr;
    return &lighting.shadow_casters();
}
} // namespace

ShadowCasterStill::ShadowCasterStill(uint64_t owner)
    : previous_(effects::Host::instance().lighting().shadow_casters().still())
{
    effects::Host::instance().lighting().shadow_casters().set_still(owner);
}

ShadowCasterStill::~ShadowCasterStill()
{
    effects::Host::instance().lighting().shadow_casters().set_still(previous_);
}

ShadowCasterBounds::ShadowCasterBounds(const glm::vec3 &center, float radius)
    : previous_(effects::Host::instance().lighting().shadow_casters().bounds())
{
    effects::Host::instance().lighting().shadow_casters().set_bounds(glm::vec4(center, radius));
}

ShadowCasterBounds::~ShadowCasterBounds()
{
    effects::Host::instance().lighting().shadow_casters().set_bounds(previous_);
}

ShadowCasterOnly::ShadowCasterOnly() : previous_(effects::Host::instance().lighting().shadow_casters().only())
{
    effects::Host::instance().lighting().shadow_casters().set_only(true);
}

ShadowCasterOnly::~ShadowCasterOnly()
{
    effects::Host::instance().lighting().shadow_casters().set_only(previous_);
}

bool ShadowCasterReach(const glm::vec3 &center, float radius)
{
    const effects::ShadowCasters *casters = FrameShadowVolumes();
    if (!casters)
        return false;
    const glm::vec3 &origin = effects::Host::instance().lighting().world_origin();
    return casters->reach(center - origin, radius, origin, casters->still());
}

bool ShadowCasterReachLocal(const glm::vec3 &center, float radius)
{
    const effects::ShadowCasters *casters = FrameShadowVolumes();
    if (!casters)
        return false;
    glm::vec3 world{};
    const float reach = shim::world_sphere(shim::Device::current->s.world, center, radius, world);
    return casters->reach(world, reach, effects::Host::instance().lighting().world_origin(), casters->still());
}

void EffectsFlash(EffectsFlashKind kind, const glm::vec3 &position)
{
    static_assert(static_cast<int>(EffectsFlashKind::ShipExplosion) == static_cast<int>(fx::FlashKind::ShipExplosion) &&
                  static_cast<int>(EffectsFlashKind::ShipExplosion) + 1 == fx::kFlashKinds);
    effects::Host::instance().lighting().add_flash(static_cast<fx::FlashKind>(kind), position);
}

void SetEffectsTextureSource(storm::shim::BaseTexture *texture, const char *path, const std::filesystem::path &file)
{
    if (shim::TexStore *store = shim::store_of(texture))
    {
        store->name = fx::NormalizeTexturePath(path ? path : "");
        store->cutout = effects::texture_cutout(*store);
        store->opaque = effects::texture_opaque(*store);
        // A new material source drops its previous skin reference; a character's atlas starts a new one.
        store->skin.reset(store->name.rfind("characters/", 0) == 0 ? effects::texture_level(*store, fx::kSkinToneLevel)
                                                                   : nullptr);
        effects::Host::instance().lighting().normal_maps().on_texture_loaded(*store, path, file);
        effects::upscales().on_texture_loaded(*store, store->name, file);
        std::string lower(path ? path : "");
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        store->lightmap = lower.find("lighting") != std::string::npos && lower.find("shadow.tga") != std::string::npos;
        effects::ObserveSceneTexture(shim::shared_store_of(texture));
    }
}

bool UpscaleSettled()
{
    return effects::upscales().settled();
}

UpscaleSupport UpscaleAvailability()
{
    return effects::upscales().support();
}

bool BakesBusy()
{
    return fx::BakesOutstanding() > 0;
}

void ConfigureRelief(const ReliefConfig &config)
{
    effects::ConfigureUpscaleDirectory(config.upscale_directory);
    effects::ConfigureRelief({config.directory, config.textures, config.model});
}

void TrimRelief()
{
    effects::ReliefGenerator::instance().trim();
}

uint64_t ReleaseIdleRenderTargets()
{
    const uint64_t post = effects::Host::instance().release_post_targets();
    const uint64_t clients = shim::Context::instance().release_client_memory();
    render::Log().info(
        "render targets: released {:.1f} MB (post chain {:.1f} MB, interface {:.1f} MB): the system is low on memory",
        static_cast<double>(post + clients) / (1024.0 * 1024.0), static_cast<double>(post) / (1024.0 * 1024.0),
        static_cast<double>(clients) / (1024.0 * 1024.0));
    return post + clients;
}

void ShutdownRelief()
{
    effects::ReliefGenerator::instance().shutdown();
}

void HoldBackgroundWork(const char *why)
{
    effects::hold_background_work(why);
}

bool BackgroundWorkBusy()
{
    return effects::ReliefGenerator::instance().stats().working;
}

std::string ThermalReading()
{
    return effects::thermal_text();
}

ReliefMaps CurrentReliefMaps()
{
    const effects::NormalMaps::Stats maps = effects::Host::instance().lighting().normal_maps().stats();
    const effects::ReliefGenerator::Stats generator = effects::ReliefGenerator::instance().stats();
    return {.resident = maps.resident,
            .materials = maps.materials,
            .waiting = maps.waiting + generator.requested,
            .working = generator.working,
            .paused = generator.paused};
}

std::string TakeEffectsNotice()
{
    return effects::Host::instance().take_notice();
}

EffectsReport CurrentEffects()
{
    return effects::Host::instance().report();
}

void SetEffectsWeather(const EffectsWeather &weather)
{
    effects::Host::instance().set_weather(weather);
}

void SetEffectsWobble(const EffectsWobble &wobble)
{
    effects::Host::instance().set_wobble(wobble);
}

void SetEffectsSunGlare(float glare, float ghosts)
{
    effects::Host::instance().set_sun_glare(glare, ghosts);
}
} // namespace storm::render
