// The execution order of the game's views (render::kGameViewBegin..kGameViewEnd) within one bgfx frame, so that
// passes into other targets do not cut the scene's pass. Design: renderer_bgfx/README.md, "View schedule".
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace storm::shim
{

class ViewSchedule
{
  public:
    enum class Lane : uint8_t
    {
        Target,
        Scene,
        Post,
        Picture,
    };
    struct Clear
    {
        uint16_t flags = 0;
        uint32_t rgba = 0;
        float z = 1.0f;
        uint8_t stencil = 0;
        bool known = false; // set_clear recorded it (views the effects set up themselves keep theirs unknown)
    };

    explicit ViewSchedule(uint16_t first) : first_(first)
    {
    }
    // A new bgfx frame: the game views start again at `first`.
    void begin();
    // `view` writes `writes` (bgfx texture ids) as `lane`; a view opened again forgets what it held.
    void open(uint16_t view, Lane lane, const char *name, std::initializer_list<uint16_t> writes);
    void read(uint16_t view, uint16_t texture);
    void write(uint16_t view, uint16_t texture);
    // `before` runs right before `view` in every plan (the depth pre-pass of `view`, on its frame buffer).
    void twin(uint16_t before, uint16_t view);
    // The clear the view was given (bgfx::setViewClear), kept to add discard flags once the order is known.
    void set_clear(uint16_t view, const Clear &clear);
    // The execution order of the `count` views used from `first` (`order` receives their ids).
    void plan(uint16_t count, std::vector<uint16_t> &order);
    // After plan(): the last view in execution order that writes `texture` (UINT16_MAX: none), and a view's clear.
    uint16_t last_writer(uint16_t texture) const;
    const Clear &clear_of(uint16_t view) const
    {
        return views_[view - first_].clear;
    }
    // After plan(): the scene's views and its passes (runs of them no view of another lane cuts).
    uint32_t scene_views() const
    {
        return scene_views_;
    }
    uint32_t scene_passes() const
    {
        return scene_passes_;
    }

  private:
    struct View
    {
        bool opened = false;
        Lane lane = Lane::Picture;
        std::string name;
        std::vector<uint16_t> writes, reads;
        Clear clear;
        uint16_t twin = UINT16_MAX;    // the view that runs right before this one
        uint16_t twin_of = UINT16_MAX; // the view this one runs right before
    };
    static bool conflict(const View &earlier, const View &later);
    View &at(uint16_t view);

    uint16_t first_;
    std::vector<View> views_;
    std::vector<uint16_t> order_; // the last plan, view ids
    uint32_t scene_views_ = 0, scene_passes_ = 0;
    std::vector<std::string> logged_; // "<view> after <view it stopped at>" already logged
};

} // namespace storm::shim
