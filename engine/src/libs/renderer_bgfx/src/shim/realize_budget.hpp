// The game-thread time a frame of play spends making textures' GPU copies on their first draw. Design:
// renderer_bgfx/README.md, "Texture realize budget".
#pragma once

#include <algorithm>
#include <cstdint>

namespace storm::shim
{

class RealizeBudget
{
  public:
    // Of a 33 ms (30 fps) frame; a single texture over it still goes through, alone in its frame.
    static constexpr double kFrameMs = 4.0;

    // Whether a texture may be made now (and timed with made()). Unlimited while a scene is prepared.
    bool admit(bool preparing)
    {
        frame_.preparing |= preparing;
        return preparing || frame_.made == 0 || frame_.ms < kFrameMs;
    }
    void made(double ms)
    {
        frame_.ms += ms;
        ++frame_.made;
    }
    // admit() said no: a draw left out of this frame (its texture has no GPU copy yet), or the swap of a finished
    // transcode postponed (its interim copy draws meanwhile).
    void left_out()
    {
        ++frame_.left_out;
    }
    void postponed()
    {
        ++frame_.postponed;
    }

    struct Frame
    {
        double ms = 0;          // spent making textures
        uint32_t made = 0;      // textures made
        uint32_t left_out = 0;  // draws left out
        uint32_t postponed = 0; // transcode swaps postponed
        bool preparing = false; // a scene was prepared in this frame: not a frame of play
    };
    // At each presented frame: this frame's totals, and the budget starts over.
    Frame end_frame()
    {
        const Frame frame = frame_;
        frame_ = {};
        return frame;
    }

    // The frames of play since the last report (end_frame's results that are not `preparing`).
    struct Window
    {
        uint64_t made = 0, left_out = 0, postponed = 0;
        uint32_t frames_made = 0, frames_left_out = 0;
        double ms = 0, worst_ms = 0;
        void add(const Frame &frame)
        {
            if (frame.preparing)
                return;
            made += frame.made;
            left_out += frame.left_out;
            postponed += frame.postponed;
            frames_made += frame.made != 0;
            frames_left_out += frame.left_out != 0;
            ms += frame.ms;
            worst_ms = std::max(worst_ms, frame.ms);
        }
        bool empty() const
        {
            return made == 0 && left_out == 0 && postponed == 0;
        }
    };

  private:
    Frame frame_;
};

} // namespace storm::shim
