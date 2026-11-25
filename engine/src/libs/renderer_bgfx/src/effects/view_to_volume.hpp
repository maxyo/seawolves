#pragma once

#include <bx/math.h>

#include <array>

namespace storm::effects
{
// A volume's view-to-texture transform for the draws (the AO and visibility volumes): made again only when the camera
// or the volume's own transform changed. A new room or a rebake can arrive while the camera stands still.
class ViewToVolume
{
  public:
    const std::array<float, 16> &of(const std::array<float, 16> &world_view, const std::array<float, 16> &world_to_uvw)
    {
        if (world_view != view_ || world_to_uvw != world_to_uvw_)
        {
            float inverse_view[16];
            bx::mtxInverse(inverse_view, world_view.data());
            bx::mtxMul(view_to_uvw_.data(), inverse_view, world_to_uvw.data());
            view_ = world_view;
            world_to_uvw_ = world_to_uvw;
        }
        return view_to_uvw_;
    }

  private:
    std::array<float, 16> view_{}, world_to_uvw_{}, view_to_uvw_{};
};
} // namespace storm::effects
