#pragma once

#include "storm/render/log.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/storm_vk_traffic.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace storm::shim
{

struct GpuPassTimes
{
    std::string name; // the views' bgfx name
    double gpu_mean = 0.0, gpu_max = 0.0, cpu_mean = 0.0, cpu_max = 0.0;
};

struct GpuPassWindow
{
    uint32_t frames = 0; // frames with a GPU time
    double frame_mean = 0.0, frame_max = 0.0;
    uint32_t sampled_frames = 0; // of them, those with the per-view queries
    double sampled_mean = 0.0;
    uint32_t gpu_samples = 0, cpu_samples = 0; // sampled frames with per-view GPU / render-thread times
    std::vector<GpuPassTimes> passes;          // largest GPU time first
};

// The "gpu passes:" line of a window of `seconds`; empty when no GPU frame time arrived in it. Without sampled frames
// (detailed diagnostics off) it is the frame's GPU time alone.
inline std::string Describe(const GpuPassWindow &window, double seconds)
{
    if (window.frames == 0)
        return {};
    std::string line = fmt::format("gpu passes: {:.1f} s; gpu_frame_ms mean {:.2f} max {:.2f} over {} frames", seconds,
                                   window.frame_mean, window.frame_max, window.frames);
    if (window.sampled_frames == 0 && window.cpu_samples == 0)
        return line;
    fmt::format_to(std::back_inserter(line),
                   ", sampled frames {:.2f} over {}; per pass gpu_ms mean/max over {} sampled frames, cpu_ms mean/max "
                   "over {}:",
                   window.sampled_mean, window.sampled_frames, window.gpu_samples, window.cpu_samples);
    for (const GpuPassTimes &pass : window.passes)
        fmt::format_to(std::back_inserter(line), " [{}] {:.2f}/{:.2f} {:.2f}/{:.2f}", pass.name, pass.gpu_mean,
                       pass.gpu_max, pass.cpu_mean, pass.cpu_max);
    return line;
}

// What the Vulkan backend's render passes load, store and resolve, per frame and per pass name, the scene's cuts, and
// the fragment shader invocations of its draws (patches/bgfx-vulkan-traffic-counters.patch): the "render passes:" line
// (README.md, "GPU passes"). Game thread only.
class PassTraffic
{
  public:
    struct Group
    {
        uint32_t passes = 0, views = 0;
        uint64_t area = 0; // the largest render area of its passes, pixels
        uint64_t loaded[2] = {}, stored[2] = {}, resolved[2] = {};
        uint64_t draws[bgfx::VkDrawClass::Count] = {}, invocations[bgfx::VkDrawClass::Count] = {};
        bool counted = true; // every pass had its invocations counted
    };
    struct Window
    {
        uint32_t frames = 0, passes = 0, scene_passes = 0, scene_views = 0;
        uint64_t loaded[2] = {}, stored[2] = {}, resolved[2] = {};
        std::map<std::string, Group, std::less<>> groups;
        std::map<std::string, uint32_t, std::less<>> cuts; // "<cause> before '<next pass>'": count
    };

    // A frame's passes (bgfx::getVkPassTraffic): added to the window.
    void add(const bgfx::VkPassTraffic *passes, uint32_t count)
    {
        ++window_.frames;
        window_.passes += count;
        for (uint32_t i = 0; i < count; ++i)
        {
            const bgfx::VkPassTraffic &pass = passes[i];
            const bool scene = IsScene(pass);
            window_.scene_passes += scene;
            window_.scene_views += scene ? pass.views : 0;
            Group &group = window_.groups[GroupName(pass)];
            ++group.passes;
            group.views += pass.views;
            group.area = std::max<uint64_t>(group.area, uint64_t(pass.width) * pass.height);
            for (int k = 0; k < 2; ++k)
            {
                group.loaded[k] += pass.loaded[k];
                group.stored[k] += pass.stored[k];
                group.resolved[k] += pass.resolved[k];
                window_.loaded[k] += pass.loaded[k];
                window_.stored[k] += pass.stored[k];
                window_.resolved[k] += pass.resolved[k];
            }
            for (int k = 0; k < bgfx::VkDrawClass::Count; ++k)
            {
                group.draws[k] += pass.draws[k];
                if (pass.invocations[k] == UINT64_MAX)
                    group.counted = false;
                else
                    group.invocations[k] += pass.invocations[k];
            }
            // A cut: the scene's pass ends and a later pass of the frame draws the scene again; named with the next
            // views' pass (a resolve the cut causes comes between).
            if (scene)
                for (uint32_t j = i + 1; j < count; ++j)
                    if (IsScene(passes[j]))
                    {
                        uint32_t next = i + 1;
                        while (passes[next].kind != bgfx::VkPassKind::Views)
                            ++next;
                        ++window_.cuts[fmt::format("{} before '{}'", EndName(pass.end), GroupName(passes[next]))];
                        break;
                    }
        }
    }

    // The window so far; the next one starts.
    Window take()
    {
        return std::exchange(window_, {});
    }

    // The scene's views are named "scene ..." (Device::name_view).
    static bool IsScene(const bgfx::VkPassTraffic &pass)
    {
        return pass.kind == bgfx::VkPassKind::Views && std::string_view(pass.name).starts_with("scene ");
    }

    static std::string GroupName(const bgfx::VkPassTraffic &pass)
    {
        if (pass.kind == bgfx::VkPassKind::Views)
            return pass.name[0] ? std::string(pass.name) : "unnamed";
        return fmt::format("{} {}x{}{}", pass.name, pass.width, pass.height,
                           pass.samples > 1 ? fmt::format(" x{}", pass.samples) : std::string());
    }

    static const char *EndName(uint8_t end)
    {
        static constexpr const char *kNames[] = {"target", "rect", "blit", "compute", "profiler", "frame end", "own"};
        return end < std::size(kNames) ? kNames[end] : "?";
    }

    // The line of a window of `seconds` (MB: 10^6 bytes per frame; invocations per pixel of the group's render area
    // per frame, opaque/alpha-tested/blended/depth only); empty without frames.
    static std::string Describe(const Window &window, double seconds)
    {
        if (window.frames == 0)
            return {};
        const double frames = window.frames;
        const auto mb = [&](uint64_t bytes) { return static_cast<double>(bytes) / 1e6 / frames; };
        std::string line = fmt::format(
            "render passes: {:.1f} s, {} frames; per frame {:.2f} passes, scene {:.2f} passes of {:.2f} views; MB "
            "loaded {:.2f}/{:.2f} stored {:.2f}/{:.2f} resolved {:.2f}/{:.2f} (colour/depth):",
            seconds, window.frames, window.passes / frames, window.scene_passes / frames, window.scene_views / frames,
            mb(window.loaded[0]), mb(window.loaded[1]), mb(window.stored[0]), mb(window.stored[1]),
            mb(window.resolved[0]), mb(window.resolved[1]));
        for (const auto &[name, group] : window.groups)
        {
            fmt::format_to(std::back_inserter(line),
                           " [{}] {:.2f} passes {:.2f} views L {:.2f}/{:.2f} S {:.2f}/{:.2f} R {:.2f}/{:.2f} draws "
                           "{:.1f}/{:.1f}/{:.1f}/{:.1f}",
                           name, group.passes / frames, group.views / frames, mb(group.loaded[0]), mb(group.loaded[1]),
                           mb(group.stored[0]), mb(group.stored[1]), mb(group.resolved[0]), mb(group.resolved[1]),
                           group.draws[0] / frames, group.draws[1] / frames, group.draws[2] / frames,
                           group.draws[3] / frames);
            const double px = static_cast<double>(std::max<uint64_t>(group.area, 1)) * frames;
            if (group.counted)
                fmt::format_to(
                    std::back_inserter(line), " inv/px {:.3f}/{:.3f}/{:.3f}/{:.3f}",
                    static_cast<double>(group.invocations[0]) / px, static_cast<double>(group.invocations[1]) / px,
                    static_cast<double>(group.invocations[2]) / px, static_cast<double>(group.invocations[3]) / px);
        }
        line += "; scene cuts per frame:";
        if (window.cuts.empty())
            line += " none";
        for (const auto &[cut, n] : window.cuts)
            fmt::format_to(std::back_inserter(line), " {} {:.2f};", cut, n / frames);
        return line;
    }

  private:
    Window window_;
};

// The GPU and render-thread time of each pass of the frame, for the "gpu passes:" line (README.md, "GPU passes").
// Game thread only.
class GpuPasses
{
  public:
    // bgfx's per-view timer queries end the render pass at every view (a tile GPU stores and reloads its targets
    // there), so they are on in one presented frame of this many, and only with detailed diagnostics; the frame's own
    // query runs in every frame.
    static constexpr uint32_t kSampleFrames = 30;

    // Before bgfx::frame(): whether the frame about to be submitted carries the per-view queries (BGFX_DEBUG_PROFILER).
    // Never before the backend has delivered a GPU frame time: without timer queries there is nothing to sample.
    bool sample_next() const
    {
        return render::DetailedDiagnostics() && timer_queries_ && frames_since_sample_ + 1 >= kSampleFrames;
    }

    // After bgfx::frame() returned `submitted` (`sampled`: it carried the per-view queries). `stats` is
    // bgfx::getStats(): an earlier frame's, with its view list when that frame was sampled; a view's GPU result is the
    // latest that arrived for its view id, which is the previous sampled frame's.
    void add(const bgfx::Stats &stats, uint32_t submitted, bool sampled)
    {
        if (sampled)
        {
            awaiting_ = submitted;
            last_sample_ = submitted;
            frames_since_sample_ = 0;
        }
        else
            ++frames_since_sample_;
        if (stats.gpuTimerFreq > 0 && stats.gpuTimeEnd > stats.gpuTimeBegin)
        {
            timer_queries_ = true;
            // A frame whose render thread saw no new result repeats the last one.
            if (stats.gpuFrameNum != last_gpu_frame_)
            {
                last_gpu_frame_ = stats.gpuFrameNum;
                const double ms = ticks_ms(stats.gpuTimeEnd - stats.gpuTimeBegin, stats.gpuTimerFreq);
                frame_.add(ms);
                if (stats.gpuFrameNum == last_sample_)
                    sampled_frame_.add(ms);
            }
        }
        if (stats.numViews > 0 && awaiting_)
            add_views(stats, *awaiting_);
        if (render::DetailedDiagnostics())
            add_traffic();
    }

    // The window so far; the next window starts.
    GpuPassWindow take()
    {
        GpuPassWindow window{.frames = frame_.count,
                             .frame_mean = frame_.mean(),
                             .frame_max = frame_.max,
                             .sampled_frames = sampled_frame_.count,
                             .sampled_mean = sampled_frame_.mean(),
                             .gpu_samples = gpu_samples_,
                             .cpu_samples = cpu_samples_};
        for (const auto &[name, pass] : passes_)
            window.passes.push_back({name, gpu_samples_ ? pass.gpu.sum / gpu_samples_ : 0.0, pass.gpu.max,
                                     cpu_samples_ ? pass.cpu.sum / cpu_samples_ : 0.0, pass.cpu.max});
        std::sort(window.passes.begin(), window.passes.end(), [](const GpuPassTimes &a, const GpuPassTimes &b) {
            return std::pair(a.gpu_mean, a.cpu_mean) > std::pair(b.gpu_mean, b.cpu_mean);
        });
        frame_ = {};
        sampled_frame_ = {};
        passes_.clear();
        gpu_samples_ = cpu_samples_ = 0;
        return window;
    }

  private:
    // The Vulkan backend's last complete frame, once; its "render passes:" line every render::kSummaryPeriod.
    void add_traffic()
    {
        uint32_t frame = 0;
        uint32_t count =
            bgfx::getVkPassTraffic(traffic_frame_.data(), static_cast<uint32_t>(traffic_frame_.size()), &frame);
        if (count > traffic_frame_.size())
        {
            traffic_frame_.resize(count);
            count = bgfx::getVkPassTraffic(traffic_frame_.data(), count, &frame);
        }
        if (frame != 0 && frame != last_traffic_frame_)
        {
            last_traffic_frame_ = frame;
            traffic_.add(traffic_frame_.data(),
                         std::min<uint32_t>(count, static_cast<uint32_t>(traffic_frame_.size())));
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - traffic_start_ < render::kSummaryPeriod)
            return;
        const double seconds = std::chrono::duration<double>(now - traffic_start_).count();
        traffic_start_ = now;
        if (const std::string line = PassTraffic::Describe(traffic_.take(), seconds); !line.empty())
            render::Log().info("{}", line);
    }

    PassTraffic traffic_;
    std::vector<bgfx::VkPassTraffic> traffic_frame_ = std::vector<bgfx::VkPassTraffic>(64);
    uint32_t last_traffic_frame_ = 0;
    std::chrono::steady_clock::time_point traffic_start_ = std::chrono::steady_clock::now();

    struct Series
    {
        double sum = 0.0, max = 0.0;
        uint32_t count = 0;
        double now = 0.0; // the sampled frame being added up
        void add(double ms)
        {
            sum += ms;
            max = std::max(max, ms);
            ++count;
        }
        double mean() const
        {
            return count ? sum / count : 0.0;
        }
    };
    struct Pass
    {
        Series gpu, cpu; // a pass absent from a sampled frame counts 0 ms there
    };

    static double ticks_ms(int64_t ticks, int64_t frequency)
    {
        return 1000.0 * static_cast<double>(ticks) / static_cast<double>(frequency);
    }

    static std::string view_name(const bgfx::ViewStats &view)
    {
        return view.name[0] ? std::string(view.name) : fmt::format("view {}", view.view);
    }

    // The view list of sampled frame `frame`: its render-thread times, and the GPU times of the sampled frame before
    // it (by the names its views had then). Views of one name are added up.
    void add_views(const bgfx::Stats &stats, uint32_t frame)
    {
        awaiting_.reset();
        bool gpu = false;
        for (uint16_t i = 0; i < stats.numViews; ++i)
        {
            const bgfx::ViewStats &view = stats.viewStats[i];
            passes_[view_name(view)].cpu.now += ticks_ms(view.cpuTimeEnd - view.cpuTimeBegin, stats.cpuTimerFreq);
            if (named_frame_ && view.gpuFrameNum == *named_frame_ && view.gpuTimeEnd > view.gpuTimeBegin &&
                stats.gpuTimerFreq > 0)
                for (const auto &[id, name] : names_)
                    if (id == view.view)
                    {
                        passes_[name].gpu.now += ticks_ms(view.gpuTimeEnd - view.gpuTimeBegin, stats.gpuTimerFreq);
                        gpu = true;
                        break;
                    }
        }
        ++cpu_samples_;
        gpu_samples_ += gpu;
        for (auto &[name, pass] : passes_)
        {
            pass.cpu.add(std::exchange(pass.cpu.now, 0.0));
            if (gpu)
                pass.gpu.add(std::exchange(pass.gpu.now, 0.0));
        }
        names_.clear();
        for (uint16_t i = 0; i < stats.numViews; ++i)
            names_.emplace_back(stats.viewStats[i].view, view_name(stats.viewStats[i]));
        named_frame_ = frame;
    }

    bool timer_queries_ = false;
    uint32_t frames_since_sample_ = 0;
    std::optional<uint32_t> awaiting_; // the sampled frame whose view list has not arrived yet
    // A frame's GPU time arrives a few frames after it (bgfx's frame latency), long before the next sample.
    std::optional<uint32_t> last_sample_;
    uint32_t last_gpu_frame_ = UINT32_MAX;
    std::optional<uint32_t> named_frame_; // the sampled frame of names_
    std::vector<std::pair<bgfx::ViewId, std::string>> names_;
    Series frame_, sampled_frame_;
    std::map<std::string, Pass, std::less<>> passes_;
    uint32_t gpu_samples_ = 0, cpu_samples_ = 0;
};

} // namespace storm::shim
