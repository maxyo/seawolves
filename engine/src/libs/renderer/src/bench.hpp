// The renderer bench (debuggable Android builds, renderer.cpp): the GPU time of graphics variants where the player
// stands, each measured between two measurements of the reference. Header-only and free of the engine, so
// tests/android_bench runs it as it is. Design: renderer/README.md, "Bench".
#pragma once

#include "auto_preset.hpp"
#include "storm/render/frame_rate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace storm::render
{

enum class BenchMode
{
    Full,  // every variant of BenchVariants
    Quick, // the scene heights only: the cost per pixel
};

// Overrides over the reference in engine.ini's option syntax ("fx_msaa=1,fx_shadows=0").
struct BenchVariant
{
    std::string name;
    std::string options;
};

// The texture switches as the reference has them: each variant turns one the other way.
struct BenchSwitches
{
    bool normal_maps = false, height_maps = false, upscale = false;
};

inline std::vector<BenchVariant> BenchVariants(BenchMode mode, const BenchSwitches &reference)
{
    // Heights above the refresh period's worth of work: the slope per megapixel is the cost of a pixel.
    std::vector<BenchVariant> variants = {{"height 1200", "fx_render_height=1200"},
                                          {"height 1400", "fx_render_height=1400"}};
    if (mode == BenchMode::Quick)
        return variants;
    variants.insert(variants.begin(), {"height 1000", "fx_render_height=1000"});
    const auto flip = [](const char *name, bool on) {
        return BenchVariant{fmt::format("{} {}", name, on ? "off" : "on"), fmt::format("{}={}", name, on ? 0 : 1)};
    };
    variants.insert(variants.end(), {{"msaa 1", "fx_msaa=1"},
                                     {"msaa 2", "fx_msaa=2"},
                                     {"msaa 4", "fx_msaa=4"},
                                     {"sun shadows off", "fx_shadows=0"},
                                     {"lamp shadows off", "fx_lamp_shadows=0"},
                                     {"point lights off", "fx_light_point=0"},
                                     {"plain shading", "fx_light_per_pixel=0,fx_light_point=0"},
                                     {"ao volume off", "fx_ao_volume=0"},
                                     {"visibility volume off", "fx_visibility=0"},
                                     {"specular and gloss off", "fx_light_specular=0,fx_gloss=0"},
                                     {"specialised programs off", "fx_ff_specialised=0"},
                                     {"lamp edge maps", "fx_lamp_edges=1"},
                                     {"anisotropy 1", "fx_anisotropy=1"},
                                     {"anisotropy 8", "fx_anisotropy=8"},
                                     flip("texture_normal_maps", reference.normal_maps),
                                     flip("texture_height_maps", reference.height_maps),
                                     flip("texture_upscale", reference.upscale),
                                     {"alpha to coverage off", "fx_alpha_to_coverage=0"},
                                     {"preset 0", "fx_preset=0"},
                                     {"preset 1", "fx_preset=1"},
                                     {"preset 2", "fx_preset=2"}});
    // The depth pre-pass both ways whatever the preset has, alone and with what it may interact with. A slot the
    // display or the CPU holds does not show its work (BenchSample::gpu_share): the pairs again at the larger scenes,
    // where the GPU is the limit.
    for (const char *with :
         {"", "fx_ao_volume=0", "fx_light_per_pixel=0,fx_light_point=0", "fx_ff_specialised=0", "fx_render_height=1200",
          "fx_render_height=1400", "fx_light_per_pixel=0,fx_light_point=0,fx_render_height=1400",
          "fx_msaa=1,fx_render_height=1400"})
        for (const int on : {0, 1})
        {
            const std::string options = fmt::format("fx_depth_prepass={}{}{}", on, *with ? "," : "", with);
            variants.push_back({fmt::format("pre-pass {}", options.substr(sizeof("fx_depth_prepass=") - 1)), options});
        }
    // What High had before and may have again; drawn only with the relief maps, so their switches go on with them and
    // "relief on" is their own reference.
    constexpr const char *relief = "texture_normal_maps=1,texture_height_maps=1";
    variants.insert(variants.end(),
                    {{"relief on", relief},
                     {"occlusion parallax", fmt::format("{},fx_parallax=2,fx_parallax_shadow=1", relief)},
                     {"character parallax", fmt::format("{},fx_character_relief=2", relief)},
                     {"lamps 2 per pixel 1 filtered", "fx_lamp_per_pixel=2,fx_lamp_filtered=1"}});
    return variants;
}

// One measured slot: the GPU times of the frames presented from the end of its settle on.
struct BenchSample
{
    double mean_ms = 0.0, p95_ms = 0.0;
    size_t gpu_frames = 0;       // frames whose GPU time arrived
    uint32_t frames = 0;         // frames presented while it was measured
    double seconds = 0.0;        // measured
    double ready_s = 0.0;        // from the switch until the measurement began (prepared, idle, settled)
    unsigned measured_again = 0; // measurements thrown away (the reason is a Step::invalid)

    // The part of the measured time the GPU worked. Frames that queue back to back keep it near 1; a slot whose frames
    // wait for the display's refresh or for the CPU leaves the GPU idle, its clock may fall, and mean_ms is no cost.
    double gpu_share() const
    {
        return seconds > 0.0 ? mean_ms * frames / (1000.0 * seconds) : 0.0;
    }
};

struct BenchResult
{
    size_t variant = 0;                // of Bench::variants()
    BenchSample sample, before, after; // the variant, the reference before and after it
};

// The bench as a state machine, driven once per frame by the renderer. Slots alternate the reference and the variants
// (reference, variant 0, reference, variant 1, ..., reference). A slot is measured only once its settings draw
// (nothing pending), nothing generates in the background, it is played, and it then settled for the automatic
// preset's settle time and frames without a frame that left a draw out; it is measured for the automatic preset's
// measuring time and frames. A frame that leaves a draw out, a switch or background work starting while it is measured
// throws the measurement away, and the slot waits and is measured again. Only GPU times of frames presented after the
// measurement began count, so results of earlier frames that arrive late never do.
class Bench
{
  public:
    enum class Phase
    {
        Waiting,    // for the slot's settings, the background work, play, or a frame with every draw
        Settling,   // the settle time from the moment nothing held it
        Measuring,  // frames counted from `start_frame_`
        Collecting, // the GPU times of the measured frames still arriving
        Done,
        Aborted,
    };

    struct Frame
    {
        double now_s = 0.0;
        bool playing = false;              // the scene is drawn for play: no interface open, no place loading
        bool backgrounded = false;         // the app went to the background since the last frame
        bool pending = false;              // a settings switch is being prepared (EffectsReport::pending)
        bool upscale = false;              // ×2 textures still being read, uploaded or made
        bool relief = false;               // relief maps of drawn textures being made or requested
        bool bakes = false;                // a bake queued or running
        uint32_t incomplete = 0;           // storm::render::LastIncompleteFrame
        uint32_t presented = 0;            // bgfx frame number of the last presented frame
        std::span<const GpuFrameTime> gpu; // the GPU times that arrived since the last frame
    };

    struct Step
    {
        bool apply = false; // apply variant `variant` of variants() now, or the reference when it is -1
        int variant = -1;
        bool measuring = false;            // the slot's measurement began this frame
        std::string invalid;               // the slot's measurement was thrown away: why
        std::optional<BenchResult> result; // a variant measured, with the reference after it
        bool finished = false;
        std::string aborted; // not empty: the bench ended before its last slot: why
    };

    Bench(BenchMode mode, std::vector<BenchVariant> variants) : mode_(mode), variants_(std::move(variants))
    {
    }

    BenchMode mode() const
    {
        return mode_;
    }
    const std::vector<BenchVariant> &variants() const
    {
        return variants_;
    }
    Phase phase() const
    {
        return phase_;
    }
    bool running() const
    {
        return phase_ != Phase::Done && phase_ != Phase::Aborted;
    }
    size_t slot() const
    {
        return slot_;
    }
    size_t slots() const
    {
        return 2 * variants_.size() + 1;
    }
    // While it is not measuring: what the slot waits for, and since when.
    const char *waiting_for() const
    {
        return phase_ == Phase::Collecting ? "the GPU times of the measured frames" : hold_;
    }

    // The first slot: the reference.
    Step start(double now_s)
    {
        return begin(0, now_s);
    }

    Step frame(const Frame &frame)
    {
        if (!running())
            return {};
        if (frame.backgrounded)
            return abort("the game went to the background");
        if (played_ && !frame.playing)
            return abort("the game left play (an interface opened or a place began to load)");
        played_ = played_ || frame.playing;
        max_gpu_frame_ = std::max<int64_t>(max_gpu_frame_, latest(frame.gpu));
        Step step;
        const char *held = holds(frame);
        switch (phase_)
        {
        case Phase::Waiting:
            hold_ = held ? held : "";
            if (!held)
                settle(frame);
            break;
        case Phase::Settling:
            if (held)
            {
                hold_ = held;
                phase_ = Phase::Waiting;
                break;
            }
            if (frame.now_s - settle_since_ >= kAutoPresetSettleSeconds &&
                frame.presented - settle_frame_ >= kAutoPresetSettleFrames)
            {
                phase_ = Phase::Measuring;
                start_s_ = frame.now_s;
                start_frame_ = frame.presented;
                times_.clear();
                step.measuring = true;
            }
            break;
        case Phase::Measuring:
            if (held)
            {
                hold_ = held;
                step.invalid = fmt::format("{}: {} while it was measured", slot_name(), held);
                ++measured_again_;
                phase_ = Phase::Waiting;
                break;
            }
            collect(frame.gpu, frame.presented);
            if (frame.now_s - start_s_ >= kAutoPresetMeasureSeconds &&
                frame.presented - start_frame_ >= kAutoPresetMinFrames)
            {
                end_frame_ = frame.presented;
                end_s_ = frame.now_s;
                phase_ = Phase::Collecting;
            }
            break;
        case Phase::Collecting:
            collect(frame.gpu, end_frame_);
            if (max_gpu_frame_ >= static_cast<int64_t>(end_frame_))
                return finish_slot(frame.now_s);
            break;
        case Phase::Done:
        case Phase::Aborted:
            break;
        }
        return step;
    }

    // From outside the frame (new settings, a request to stop): ends a running bench.
    Step abort(std::string reason)
    {
        Step step;
        if (!running())
            return step;
        phase_ = Phase::Aborted;
        step.aborted = std::move(reason);
        return step;
    }

  private:
    // Why the slot cannot be measured at this frame; null when nothing holds it.
    const char *holds(const Frame &frame) const
    {
        if (!frame.playing)
            return "the game to be played";
        if (frame.pending)
            return "its settings being prepared";
        if (frame.relief)
            return "relief maps being made";
        if (frame.upscale)
            return "x2 textures being made or uploaded";
        if (frame.bakes)
            return "a bake";
        if ((phase_ == Phase::Settling || phase_ == Phase::Measuring) && frame.incomplete != incomplete_)
            return "a frame that left a draw out";
        return nullptr;
    }

    static int64_t latest(std::span<const GpuFrameTime> gpu)
    {
        int64_t frame = -1;
        for (const GpuFrameTime &time : gpu)
            frame = std::max<int64_t>(frame, time.frame);
        return frame;
    }

    void settle(const Frame &frame)
    {
        phase_ = Phase::Settling;
        settle_since_ = frame.now_s;
        settle_frame_ = frame.presented;
        incomplete_ = frame.incomplete;
    }

    // The GPU times of frames presented after the measurement began, up to `last`.
    void collect(std::span<const GpuFrameTime> gpu, uint32_t last)
    {
        for (const GpuFrameTime &time : gpu)
            if (time.frame > start_frame_ && time.frame <= last)
                times_.push_back(time.ms);
    }

    std::string slot_name() const
    {
        return slot_ % 2 == 0 ? std::string("reference") : variants_[slot_ / 2].name;
    }

    Step begin(size_t slot, double now_s)
    {
        slot_ = slot;
        phase_ = Phase::Waiting;
        hold_ = "its settings to be applied";
        applied_s_ = now_s;
        measured_again_ = 0;
        Step step;
        step.apply = true;
        step.variant = slot % 2 == 0 ? -1 : static_cast<int>(slot / 2);
        return step;
    }

    Step finish_slot(double now_s)
    {
        if (times_.empty())
            return abort(fmt::format("{}: no GPU time arrived for its frames {}..{}", slot_name(), start_frame_ + 1,
                                     end_frame_));
        BenchSample sample;
        sample.gpu_frames = times_.size();
        double sum = 0.0;
        for (const float ms : times_)
            sum += ms;
        sample.mean_ms = sum / static_cast<double>(times_.size());
        sample.p95_ms = AutoPresetPercentile(times_, 95);
        sample.frames = end_frame_ - start_frame_;
        sample.seconds = end_s_ - start_s_;
        sample.ready_s = start_s_ - applied_s_;
        sample.measured_again = measured_again_;
        Step step;
        if (slot_ % 2 == 1)
            variant_sample_ = sample;
        else
        {
            if (slot_ > 0)
                step.result = BenchResult{slot_ / 2 - 1, variant_sample_, reference_, sample};
            reference_ = sample;
        }
        if (slot_ + 1 == slots())
        {
            phase_ = Phase::Done;
            step.finished = true;
            return step;
        }
        Step next = begin(slot_ + 1, now_s);
        next.result = std::move(step.result);
        return next;
    }

    BenchMode mode_;
    std::vector<BenchVariant> variants_;
    Phase phase_ = Phase::Waiting;
    size_t slot_ = 0;
    const char *hold_ = "";
    bool played_ = false; // play was seen: leaving it ends the bench
    double applied_s_ = 0.0;
    double settle_since_ = 0.0;
    uint32_t settle_frame_ = 0;
    uint32_t incomplete_ = 0; // LastIncompleteFrame when the settle began
    double start_s_ = 0.0, end_s_ = 0.0;
    uint32_t start_frame_ = 0, end_frame_ = 0;
    int64_t max_gpu_frame_ = -1;
    unsigned measured_again_ = 0;
    std::vector<float> times_;
    BenchSample reference_, variant_sample_;
};

} // namespace storm::render
