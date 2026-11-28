// World passes of the graphics effects: the analytic sky and the improvements of the game's own sea.
// The consumer entities (SKY, shadow) call them where they would draw their legacy passes; the draws go into the shim's
// current game view in submission order, with the D3D camera of that moment as the bgfx view transform.
#include "effects/effects.hpp"

#include "effects/scene_reveal.hpp"
#include "shim/device.hpp"
#include "storm/render/depth_stencil.hpp"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{
// The Sea2 unit the wakes' target (fx::SeaWakes, sea_wakes.sh) is bound to (free on the Sea2 draws: fs_port_sea2.sc).
constexpr uint8_t kWakeStage = 12;
// Metres one repeat of the game's foam texture (pena.tga, a photograph of churned water) covers.
constexpr double kFoamTile = 10.0;

// D3D perspective: _33 = f / (f - n), _43 = -n f / (f - n). Leaves the planes as they are for other matrices.
void clip_planes(const D3DMATRIX &p, float &near_plane, float &far_plane)
{
    if (p.m[2][2] != 0.0f && p.m[2][2] != 1.0f)
    {
        near_plane = -p.m[3][2] / p.m[2][2];
        far_plane = p.m[3][2] / (1.0f - p.m[2][2]);
    }
}

} // namespace

// D3D clip space -> backend clip space, as Device::apply_ff_uniforms builds it for the fixed-function draws (without
// depth bias): y flipped on bottom-left-origin backends, D3D's half-pixel offset, depth 0..1 -> -1..1 where the
// backend wants it. The fx draws then land on exactly the pixels and depths of the shim's own geometry.
std::array<float, 16> BackendProjection(const shim::Device &device, const D3DMATRIX &proj)
{
    const auto &context = shim::Context::instance();
    const float vw = static_cast<float>(std::max<DWORD>(1, device.s.viewport.Width));
    const float vh = static_cast<float>(std::max<DWORD>(1, device.s.viewport.Height));
    const float fy = context.flip_y() ? -1.0f : 1.0f;
    float fix[4][4] = {{1, 0, 0, 0}, {0, fy, 0, 0}, {0, 0, 1, 0}, {1.0f / vw, -fy / vh, 0, 1}};
    if (context.caps()->homogeneousDepth)
    {
        const float keep = 1.0f - 2e-6f;
        fix[2][2] = 2.0f * keep;
        fix[3][2] = -keep;
    }
    std::array<float, 16> out;
    bx::mtxMul(out.data(), &proj.m[0][0], &fix[0][0]);
    return out;
}

render::EffectsSea Host::sea() const
{
    return {settings_.sea_cascades, settings_.sea_cascade_size};
}

render::EffectsWorld Host::world() const
{
    render::EffectsWorld world;
    if (!device_ready_)
        return world;
    world.lens_flare = settings_.lens_flare;
    world.flash_lights = lighting_.flashes_on();
    return world;
}

void Host::update_sky(float camera_height)
{
    sky_.Update(weather_input(), camera_height);
}

void Host::draw_sky()
{
    shim::Device *device = shim::Device::current;
    if (!device || !device_ready_ || !shim::Context::instance().ready() || !env_.sky)
        return;
    const D3DMATRIX &v = device->s.view, &p = device->s.proj;
    if (p.m[2][3] == 0.0f || p.m[0][0] == 0.0f || p.m[1][1] == 0.0f)
        return; // no perspective camera: no sky
    update_sky(camera_height_);
    // The view-space direction through a corner of D3D's clip space, turned into the world: the inverse of the view's
    // rotation is its transpose.
    const auto corner = [&](float x, float y) {
        const glm::vec3 eye{x / p.m[0][0], y / p.m[1][1], 1.0f};
        glm::vec3 world;
        for (int j = 0; j < 3; ++j)
            world[j] = eye[0] * v.m[j][0] + eye[1] * v.m[j][1] + eye[2] * v.m[j][2];
        return world;
    };
    sky_.Draw(device->effects_view(false), {corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(-1.0f, 1.0f)},
              shim::Context::instance().flip_y());
}

glm::vec3 Host::sky_horizon()
{
    return sky_state().horizon;
}

bool Host::begin_world_pass(WorldPass &pass, const render::EffectsMatrix &view, bool fresh)
{
    shim::Device *device = shim::Device::current;
    if (!device || !device_ready_ || !shim::Context::instance().ready())
        return false;
    pass.view_mtx = view;
    pass.projection = BackendProjection(*device, device->s.proj);
    std::array<float, 32> transform;
    std::copy(pass.view_mtx.begin(), pass.view_mtx.end(), transform.begin());
    std::copy(pass.projection.begin(), pass.projection.end(), transform.begin() + 16);
    pass.view = device->effects_view(fresh);
    // One view transform per bgfx view: a second camera in the same shim view (rare) gets a view of its own.
    if (pass.view == transform_view_ && transform != transform_)
        pass.view = device->effects_view(true);
    if (pass.view != transform_view_ || transform != transform_)
    {
        bgfx::setViewTransform(pass.view, pass.view_mtx.data(), pass.projection.data());
        transform_view_ = pass.view;
        transform_ = transform;
    }
    // Camera position: -t * R^T of the row-vector view matrix.
    const auto &v = view;
    for (int i = 0; i < 3; ++i)
        pass.camera[i] = -(v[12] * v[i * 4 + 0] + v[13] * v[i * 4 + 1] + v[14] * v[i * 4 + 2]);
    clip_planes(device->s.proj, pass.near_plane, pass.far_plane);
    camera_height_ = pass.camera[1];
    return true;
}

// Under the loading cover (HideScenePicture: the game picture is drawn for its resources, not shown) the sea's draws
// are discarded (Device::submit_draw) and nothing reads its backdrop or depth capture: they are drawn in the first
// covered frame (their targets and the capture's pipeline are made then) and not again until the picture shows. The
// frames that follow draw them as before: a sea drawn under the cover keeps the backdrop armed (sea_drawn_). A sea
// drawn into another target is not hidden. True: this frame leaves them out.
bool Host::sea_depth_covered(const shim::Device &device)
{
    if (device.rt != device.back_color)
        return false;
    const uint32_t frame = shim::Context::instance().submitted_frames();
    const shim::TexStore *picture = device.held_color   ? device.held_color.get()
                                    : device.back_color ? device.back_color->store.get()
                                                        : nullptr;
    if (!picture || !HideScenePicture(shim::Context::instance().Surface().generation, picture->serial))
    {
        if (sea_cover_skipped_ > 0)
            render::Log().info("effects: sea: its backdrop and depth capture were left out in {} frames under the "
                               "loading cover",
                               sea_cover_skipped_);
        sea_cover_skipped_ = 0;
        return false;
    }
    if (sea_covered_frame_ != frame)
    {
        sea_cover_skip_ = sea_covered_frame_ != UINT32_MAX && sea_covered_frame_ + 1 == frame;
        sea_covered_frame_ = frame;
        sea_cover_skipped_ += sea_cover_skip_;
    }
    return sea_cover_skip_;
}

void Host::prepare_sea(shim::Device &device)
{
    if (!bgfx::isValid(u_sea_fx_))
    {
        u_sea_fx_ = bgfx::createUniform("u_seaFx", bgfx::UniformType::Vec4, 6);
        s_sea_depth_ = bgfx::createUniform("s_seaDepth", bgfx::UniformType::Sampler);
    }
    float near_plane = 0.1f, far_plane = 4000.0f;
    clip_planes(device.s.proj, near_plane, far_plane);
    bool soft_edges = device_ready_ && settings_.sea_soft_edges;
    bool turbidity = device_ready_ && settings_.sea_turbidity;
    bool depth_ready = false;
    const uint32_t frame = shim::Context::instance().submitted_frames();
    const bool into_scene = sea_backdrop_target(device);
    const bool depth = (soft_edges || turbidity) && !sea_depth_covered(device);
    if (depth && into_scene)
    {
        // The scene distance behind the sea: the backdrop of this frame (sea_backdrop), captured at the sea's first
        // draw into the scene in a view ahead of the scene. A frame without one (the first frame of a sea) has hard
        // edges.
        if (sea_backdrop_frame_ != frame && sea_backdrop_armed(device))
        {
            // Nothing drew depth before the sea: the backdrop is the cleared depth, as the scene's is.
            std::array<float, 32> transform{};
            memcpy(transform.data(), &device.s.view.m[0][0], sizeof(float) * 16);
            const std::array<float, 16> projection = BackendProjection(device, device.s.proj);
            std::copy(projection.begin(), projection.end(), transform.begin() + 16);
            if (open_sea_backdrop_view(device, transform))
                bgfx::touch(sea_backdrop_view_);
        }
        if (sea_backdrop_frame_ == frame &&
            (sea_depth_held_.frame != frame || sea_depth_held_.source != &sea_backdrop_depth_))
        {
            const bgfx::ViewId capture = device.effects_target_view("fx sea depth", {}, {sea_backdrop_depth_.idx});
            const bool captured = sea_depth_.Capture(capture, sea_backdrop_depth_, sea_backdrop_width_,
                                                     sea_backdrop_height_, near_plane, far_plane);
            if (bgfx::isValid(sea_depth_.Texture()))
                device.schedule.write(capture, sea_depth_.Texture().idx);
            sea_depth_held_.captured(captured, frame, device.rt, &sea_backdrop_depth_);
        }
        depth_ready = sea_backdrop_frame_ == frame && sea_depth_held_.holds(frame, device.rt, &sea_backdrop_depth_) &&
                      bgfx::isValid(sea_depth_.Texture());
    }
    else if (depth)
    {
        // The sea in another target: the depth of that target, read in a view of its own before the first sea draw
        // into it in this frame; the sea then continues in a new view on the same target.
        shim::TexStore *color = device.rt ? device.rt->store.get() : nullptr;
        // new_view may substitute single_depth for a multisampled back-depth attachment.
        // Use what this target actually writes, never the nominal D3D depth surface.
        if (color)
            device.ensure_view();
        shim::TexStore *depth = color ? device.view_depth : nullptr;
        // ConstrainSettings keeps the soft edges off where the backend does not resolve multisampled depth.
        const bool readable = SeaDepthReadable(color, depth, support_.msaa_depth);
        if (!readable)
        {
            soft_edges = turbidity = false;
            if (render::Once(sea_depth_logged_))
                render::Log().error("effects: the sea is drawn into a target without a readable depth: no soft sea "
                                    "edges or turbidity there (logged once)");
        }
        else if (!sea_depth_held_.holds(frame, device.rt, depth))
        {
            depth->sync();
            const bgfx::ViewId capture = device.effects_view(true);
            const bool captured = sea_depth_.Capture(capture, depth->handle, static_cast<uint16_t>(color->width),
                                                     static_cast<uint16_t>(color->height), near_plane, far_plane);
            device.effects_view(true);
            sea_depth_held_.captured(captured, frame, device.rt, depth);
        }
        depth_ready = readable && sea_depth_held_.holds(frame, device.rt, depth) && bgfx::isValid(sea_depth_.Texture());
    }
    soft_edges &= depth_ready;
    turbidity &= depth_ready;
    if (into_scene) // the next frames draw the backdrop (sea_backdrop)
    {
        sea_drawn_frame_ = shim::Context::instance().frame_index();
        sea_drawn_ = true;
    }
    advance_sea_wakes(device);
    set_sea_wakes_uniforms();
    if (bgfx::isValid(sea_wakes_.Target()))
        device.note_read(sea_wakes_.Target()); // bound when there are wakes
    const bool specular = device_ready_ && settings_.sea_specular && has_weather();
    const auto &sun = env_.light_dir;
    const float strength = settings_.sea_specular_strength;
    // The sea's pixel constant 5: the game's current water colour (Sea2.WaterColor), not a new global tint.
    const auto &water_color = device.s.ps_const[5];
    // clang-format off
    // The glint's light: the sun's colour as the scene shows it, as much of it as the sky shows of the sun's disc; the
    // moon's comes from the game's sun road cube (the moon it draws there).
    const bool sun_lit = has_weather() && env_.light_is_sun;
    const float clear_sky = sun_lit ? env_.DiscShown() : 0.0f;
    // The light's disc: the half size of the sun's or moon's sprite (tangent of the angle to its edge).
    const float disc = sun_lit ? env_.sun_size : env_.moon_size;
    if (specular && !sun_lit && disc <= 0.0f && render::Once(sea_no_moon_disc_logged_))
        render::Log().error("effects: the moon has no disc size (sunglow Glow.MoonSize): no moon glint on the sea");
    const float sea_fx[24] = {
        0.0f, soft_edges ? 1.0f : 0.0f, specular ? 1.0f : 0.0f, far_plane,
        sun[0], sun[1], sun[2], disc * disc,
        strength, 0.0f, 0.0f,
        1.0f / std::max(settings_.sea_edge_depth, 0.01f),
        water_color[0], water_color[1], water_color[2], turbidity ? 1.0f / 12.0f : 0.0f,
        device.s.view.m[0][2], device.s.view.m[1][2], device.s.view.m[2][2], 0.0f,
        sun_display_[0] * clear_sky, sun_display_[1] * clear_sky, sun_display_[2] * clear_sky, sun_lit ? 1.0f : 0.0f,
    };
    // clang-format on
    bgfx::setUniform(u_sea_fx_, sea_fx, 6);
    if (depth_ready) // otherwise neither optical feature reads the unit
    {
        bgfx::setTexture(fx::kNormalMapStage, s_sea_depth_, sea_depth_.Texture(),
                         BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        device.note_read(sea_depth_.Texture());
    }
}

// The sea's sun road cube (fs_port_sea2.sc s_cube2, stage 2 -> unit 7): the game binds its sun road there
// (sea.cpp); a draw without one gets a black cube (no moon glint) instead of whatever 2D texture another program
// left on the unit.
void Host::bind_sea_sun_road(shim::Device &device)
{
    const bool specular = device_ready_ && settings_.sea_specular && has_weather();
    const shim::TexStore *sun_road = shim::store_of(device.s.textures[2]);
    if (!sun_road || sun_road->kind != shim::TexKind::Cube || !bgfx::isValid(sun_road->handle))
    {
        if (!bgfx::isValid(sea_no_sun_road_))
        {
            static const uint32_t black[6] = {};
            s_sea_sun_road_ = bgfx::createUniform("s_cube2", bgfx::UniformType::Sampler);
            sea_no_sun_road_ = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT,
                                                       bgfx::copy(black, sizeof(black)));
        }
        if (specular && render::Once(sea_no_sun_road_logged_))
            render::Log().info(
                "effects: the sea is drawn without the game's sun road cube (stage 2): no moon glint on it");
        bgfx::setTexture(7, s_sea_sun_road_, sea_no_sun_road_);
    }
}

void Host::set_sea_frame(const render::EffectsSeaFrame &frame)
{
    sea_frame_ = frame;
    sea_sources_.assign(frame.sources.begin(), frame.sources.end());
    sea_frame_.sources = sea_sources_;
    sea_frame_set_ = shim::Context::instance().submitted_frames();
}

void Host::advance_sea_wakes(shim::Device &device)
{
    const uint32_t frame = shim::Context::instance().submitted_frames();
    if (!settings_.sea_wakes)
    {
        sea_wakes_.Reset();
        sea_wakes_ready_ = false;
        return;
    }
    if (!device_ready_ || sea_frame_set_ != frame || sea_wakes_frame_ == frame)
        return;
    // The sea was not drawn in the frame before: another scene may have come since (its hulls are new).
    const bool resumed = sea_wakes_frame_ + 1 != frame;
    sea_wakes_frame_ = frame;
    const auto &focus = sea_frame_.focus;
    sea_hulls_.clear();
    for (const render::EffectsWaveSource &s : sea_sources_)
    {
        fx::WaveSource w;
        w.identity = s.identity;
        w.position = {s.position[0], s.position[2]};
        w.forward = s.forward;
        w.half_length = s.half_length;
        w.half_width = s.half_width;
        w.stern = s.stern;
        w.stem = s.stem;
        w.station_side = s.station_side;
        w.station_half = s.station_half;
        w.speed = s.speed;
        sea_hulls_.push_back(w);
    }

    // The wakes of the hulls, aged in game time and redrawn every frame into a target of their own that only these
    // draws and the sea's (Device::note_read) touch: it runs ahead of the scene (ViewSchedule).
    sea_wake_time_ += sea_frame_.dt;
    sea_wakes_.Update(sea_hulls_, sea_wake_time_, resumed);
    const uint16_t wake_view = device.effects_target_view("fx sea wakes", {}, {});
    sea_wakes_ready_ = sea_wakes_.Render(wake_view, {focus[0], focus[2]}, sea_wake_time_);
    if (bgfx::isValid(sea_wakes_.Target()))
        device.schedule.write(wake_view, sea_wakes_.Target().idx);
}

void Host::set_sea_wakes_uniforms()
{
    if (!bgfx::isValid(u_sea_wake_))
    {
        u_sea_wake_ = bgfx::createUniform("u_seaWake", bgfx::UniformType::Vec4, 3);
        s_sea_wake_ = bgfx::createUniform("s_seaWake", bgfx::UniformType::Sampler);
    }
    const bool frame = sea_frame_set_ == shim::Context::instance().submitted_frames();
    const auto &origin = sea_frame_.vertex_origin;
    // The wakes (fx::SeaWakes): [0] xy target uv at the sea's vertex origin, z 1 / metres it covers, w 1 = on;
    // [1] xy the foam texture's uv at the sea's vertex origin, z its repeats per metre; [2] rgb the sun's light on
    // the level sea, for the foam.
    std::array<float, 12> wake{};
    const bool wakes = device_ready_ && frame && settings_.sea_wakes && sea_wakes_ready_;
    if (wakes)
    {
        const auto centre = sea_wakes_.Centre();
        wake[0] = static_cast<float>((static_cast<double>(origin[0]) - centre[0]) / fx::SeaWakes::kExtent + 0.5);
        wake[1] = static_cast<float>((static_cast<double>(origin[2]) - centre[1]) / fx::SeaWakes::kExtent + 0.5);
        wake[2] = 1.0f / fx::SeaWakes::kExtent;
        wake[3] = 1.0f;
        // From the absolute world, as the fraction of a repeat (the origin moves with the camera).
        const auto tile_uv = [](float world) {
            const double t = static_cast<double>(world) / kFoamTile;
            return static_cast<float>(t - std::floor(t));
        };
        wake[4] = tile_uv(origin[0]);
        wake[5] = tile_uv(origin[2]);
        wake[6] = static_cast<float>(1.0 / kFoamTile);
        // The sun's colour by the sine of its height and the share of its disc the sky shows; none from the moon or
        // under the horizon.
        if (has_weather() && env_.light_is_sun)
        {
            const float lit = std::max(env_.light_dir[1], 0.0f) * env_.DiscShown();
            for (int i = 0; i < 3; ++i)
                wake[8 + i] = sun_display_[i] * lit;
        }
    }
    bgfx::setUniform(u_sea_wake_, wake.data(), 3);
    if (wakes)
        bgfx::setTexture(kWakeStage, s_sea_wake_, sea_wakes_.Target(), BGFX_SAMPLER_UVW_CLAMP);
}

void Host::release_sea_backdrop()
{
    if (bgfx::isValid(sea_backdrop_fb_))
        bgfx::destroy(sea_backdrop_fb_); // and its depth texture with it
    sea_backdrop_fb_ = BGFX_INVALID_HANDLE;
    sea_backdrop_depth_ = BGFX_INVALID_HANDLE;
    sea_backdrop_width_ = sea_backdrop_height_ = 0;
    sea_backdrop_frame_ = UINT32_MAX;
    sea_backdrop_view_ = UINT16_MAX;
    sea_backdrop_clear_z_ = -1.0f;
    sea_drawn_ = false;
}

void Host::sea_backdrop_clear(const shim::Device &device, float z)
{
    if (sea_backdrop_target(device))
        sea_backdrop_clear_z_ = z;
}

bool Host::sea_backdrop_target(const shim::Device &device) const
{
    // The game picture before the scene boundary, and after it while the picture's depth is the multisampled scene
    // depth, which is write-only (a scene at the display size, Device::create_scene_stores; the title's sea is drawn
    // after its interface ended the scene). At a render scale the picture after the boundary has a single-sample depth
    // of its own, captured in place.
    return device.rt == device.back_color &&
           (!device.scene_done || (device.back_depth->store->target_flags & BGFX_TEXTURE_RT_WRITE_ONLY));
}

bool Host::sea_backdrop_armed(const shim::Device &device) const
{
    const auto &context = shim::Context::instance();
    return device_ready_ && (settings_.sea_soft_edges || settings_.sea_turbidity) && sea_drawn_ &&
           context.frame_index() <= sea_drawn_frame_ + kSeaBackdropFrames &&
           !(sea_depth_held_.frame == context.submitted_frames() && sea_depth_held_.source == &sea_backdrop_depth_) &&
           sea_backdrop_target(device);
}

bool Host::open_sea_backdrop_view(shim::Device &device, const std::array<float, 32> &transform)
{
    const uint32_t frame = shim::Context::instance().submitted_frames();
    if (sea_backdrop_frame_ == frame && transform == sea_backdrop_transform_ && sea_backdrop_clear_z_ < 0.0f)
        return true;
    const shim::TexStore &scene = *device.rt->store;
    const uint16_t w = static_cast<uint16_t>(scene.width), h = static_cast<uint16_t>(scene.height);
    if (!bgfx::isValid(sea_backdrop_fb_) || sea_backdrop_width_ != w || sea_backdrop_height_ != h)
    {
        if (bgfx::isValid(sea_backdrop_fb_))
            bgfx::destroy(sea_backdrop_fb_);
        sea_backdrop_depth_ = bgfx::createTexture2D(w, h, false, 1, render::DepthStencilFormat(),
                                                    BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        sea_backdrop_fb_ = bgfx::createFrameBuffer(1, &sea_backdrop_depth_, true);
        sea_backdrop_width_ = w;
        sea_backdrop_height_ = h;
    }
    const uint16_t view = device.side_view("fx sea backdrop", sea_backdrop_depth_.idx);
    if (view == UINT16_MAX)
        return false;
    bgfx::setViewFrameBuffer(view, sea_backdrop_fb_);
    bgfx::setViewRect(view, 0, 0, w, h);
    // A new frame starts as the scene's depth was cleared (the far plane unless the game said otherwise), as does a
    // clear of the scene's depth within the frame; a second camera draws on. Nothing uses the stencil: cleared too, so
    // a tile GPU does not load it.
    const bool clear = sea_backdrop_frame_ != frame || sea_backdrop_clear_z_ >= 0.0f;
    bgfx::setViewClear(view, clear ? BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL : BGFX_CLEAR_NONE, 0,
                       sea_backdrop_clear_z_ >= 0.0f ? sea_backdrop_clear_z_ : 1.0f, 0);
    bgfx::setViewTransform(view, transform.data(), transform.data() + 16);
    sea_backdrop_view_ = view;
    sea_backdrop_frame_ = frame;
    sea_backdrop_transform_ = transform;
    sea_backdrop_clear_z_ = -1.0f;
    return true;
}

bool Host::sea_backdrop(shim::Device &device, const shim::LayoutInfo &layout, bool vertex_port)
{
    // Before the sea's capture in a frame that follows one with a sea in the scene; what writes depth there.
    if (!sea_backdrop_armed(device) || layout.rhw || !device.s.rs[D3DRS_ZENABLE] || !device.s.rs[D3DRS_ZWRITEENABLE] ||
        device.s.proj.m[2][3] == 0.0f)
        return false;
    const shim::TexStore &scene = *device.rt->store;
    const D3DVIEWPORT9 &viewport = device.s.viewport;
    const DWORD zfunc = device.s.rs[D3DRS_ZFUNC];
    const char *left_out =
        vertex_port ? "with a vertex shader of its own"
        : viewport.X != 0 || viewport.Y != 0 || viewport.Width != scene.addressed_width(0) ||
                viewport.Height != scene.addressed_height(0) || viewport.MinZ != 0.0f || viewport.MaxZ != 1.0f
            ? "into a part of the target or its depth range"
        : zfunc != D3DCMP_LESSEQUAL && zfunc != D3DCMP_LESS                      ? "with another depth test"
        : device.s.rs[D3DRS_STENCILENABLE]                                       ? "with a stencil test"
        : device.s.rs[D3DRS_DEPTHBIAS] || device.s.rs[D3DRS_SLOPESCALEDEPTHBIAS] ? "with a depth bias"
                                                                                 : nullptr;
    if (left_out)
    {
        // The sea fades as if it were not there (logged once per reason).
        if (std::find(sea_backdrop_logged_.begin(), sea_backdrop_logged_.end(), left_out) == sea_backdrop_logged_.end())
        {
            render::Log().info(
                "effects: sea backdrop: a draw before the sea {} is left out of the soft edges' depth (logged once)",
                left_out);
            sea_backdrop_logged_.emplace_back(left_out);
        }
        return false;
    }
    sea_backdrop_material_ = caster_material(device, layout);
    if (!sea_backdrop_material_.casts)
        return false;
    const std::array<float, 16> projection = BackendProjection(device, device.s.proj);
    std::array<float, 32> transform;
    memcpy(transform.data(), &device.s.view.m[0][0], sizeof(float) * 16);
    std::copy(projection.begin(), projection.end(), transform.begin() + 16);
    // No bgfx::touch in a new view: the draw being set up would lose its bindings; the copy that follows draws in it.
    return open_sea_backdrop_view(device, transform);
}

void Host::submit_sea_backdrop(shim::Device &device, bool skinned, uint64_t cull, bool keep)
{
    const bgfx::ProgramHandle program =
        lighting_.casters().program(device, Casters::Pass::Sun, skinned, sea_backdrop_material_);
    bgfx::setScissor(UINT16_MAX);
    bgfx::setStencil(BGFX_STENCIL_NONE);
    bgfx::setTransform(&device.s.world.m[0][0]);
    bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | cull);
    if (bgfx::isValid(program))
    {
        bgfx::submit(sea_backdrop_view_, program, 0, keep ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
        ++sea_backdrop_draws_;
    }
    else if (!keep)
        bgfx::discard();
}

} // namespace storm::effects

namespace storm::render
{
EffectsWorld CurrentEffectsWorld()
{
    return effects::Host::instance().world();
}

EffectsSea CurrentEffectsSea()
{
    return effects::Host::instance().sea();
}
} // namespace storm::render
