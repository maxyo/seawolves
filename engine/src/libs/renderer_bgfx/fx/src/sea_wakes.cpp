#include "storm/fx/sea_wakes.hpp"

#include "gpu_util.hpp"
#include "storm/render/log.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
using namespace detail;

namespace
{
const bgfx::VertexLayout &Layout()
{
    static const bgfx::VertexLayout layout = [] {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
            .end();
        return l;
    }();
    static_assert(sizeof(WakeVertex) == 9 * sizeof(float));
    return layout;
}
} // namespace

SeaWakes::~SeaWakes()
{
    Shutdown();
}

bool SeaWakes::Prepare(int &budget)
{
    if (bgfx::isValid(program_))
        return true;
    if (budget <= 0)
        return false;
    program_ = LoadProgram("vs_fx_wake", "fs_fx_wake");
    --budget;
    return true;
}

void SeaWakes::Update(std::span<const WaveSource> sources, double now, bool resumed)
{
    trails_.Update(sources, now, resumed);
}

void SeaWakes::Reset()
{
    trails_.Reset();
}

bool SeaWakes::Draw(bgfx::ViewId view, bool bow)
{
    const auto count = static_cast<uint32_t>(vertices_.size());
    if (count < 4)
        return false;
    if (bgfx::getAvailTransientVertexBuffer(count, Layout()) < count)
    {
        if (unbuffered_++ % 600 == 0)
            render::Log().error("fx: sea wakes: no transient vertex buffer for the {} vertices of a hull's wake: "
                                "not drawn this frame ({} times so far)",
                                count, unbuffered_);
        return false;
    }
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, count, Layout());
    std::copy(vertices_.begin(), vertices_.end(), reinterpret_cast<WakeVertex *>(buffer.data));
    const float wake[4] = {WakeTrails::kFoamLife, WakeTrails::kLife, bow ? 1.0f : 0.0f, 0.0f};
    bgfx::setUniform(u_wake_, wake);
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setState(BGFX_STATE_WRITE_R | BGFX_STATE_WRITE_G | BGFX_STATE_PT_TRISTRIP |
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
                   BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_MAX));
    bgfx::submit(view, program_);
    return true;
}

bool SeaWakes::Render(bgfx::ViewId view, std::array<double, 2> camera, double now)
{
    // Centred on the camera in whole texels: the bands stay on the same texels while it moves.
    constexpr double texel = kExtent / kSize;
    centre_ = {std::floor(camera[0] / texel + 0.5) * texel, std::floor(camera[1] / texel + 0.5) * texel};
    const bool flip_y = !bgfx::getCaps()->originBottomLeft;
    // The target and its view only once there is a band to draw: a sea without ships under way has neither.
    bool drawn = false;
    const auto band = [&](bool bow) {
        if (vertices_.size() < 4)
            return;
        if (!drawn)
        {
            if (!bgfx::isValid(target_))
            {
                texture_ = bgfx::createTexture2D(kSize, kSize, false, 1, bgfx::TextureFormat::RGBA8,
                                                 BGFX_TEXTURE_RT | kClampLinear);
                target_ = bgfx::createFrameBuffer(1, &texture_, false);
                render::Log().info("fx: sea wakes: {}x{} RGBA8 target over {:.0f} m", kSize, kSize, kExtent);
            }
            if (!bgfx::isValid(program_))
                program_ = LoadProgram("vs_fx_wake", "fs_fx_wake");
            if (!bgfx::isValid(u_wake_))
                u_wake_ = bgfx::createUniform("u_wake", bgfx::UniformType::Vec4);
            SetupView(view, "fx sea wakes", target_, kSize, kSize);
            bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000);
            bgfx::touch(view);
            drawn = true;
        }
        Draw(view, bow);
    };
    for (const WakeTrails::Trail &trail : trails_.Trails())
    {
        vertices_.clear();
        WakeTrails::Ribbon(trail, now, centre_, kExtent, flip_y, vertices_);
        band(false);
    }
    for (const WakeTrails::Trail &trail : trails_.Trails())
    {
        vertices_.clear();
        WakeTrails::BowBand(trail, now, centre_, kExtent, flip_y, vertices_);
        band(true);
    }
    return drawn;
}

void SeaWakes::Shutdown()
{
    Destroy(target_);
    Destroy(texture_);
    Destroy(program_);
    Destroy(u_wake_);
    trails_.Reset();
}

} // namespace storm::fx
