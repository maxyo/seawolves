// Frame pacing of the game window through Android Frame Pacing (Swappy GL or Swappy Vk), at the «Частота кадров»
// setting. Design: renderer_bgfx/README.md, "Frame pacing".
#pragma once

#include "storm/render/frame_rate.hpp"
#include "swap_interval.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace storm::shim
{

class FramePacing
{
  public:
    static constexpr std::chrono::nanoseconds kDefaultFrame{16'666'667};

    static FramePacing &instance();

    // Game thread, before bgfx::init of the game window (Android: Swappy with SDL's activity). `render_thread`:
    // bgfx presents on its render thread (the present functions then run there; everything else of this class is
    // called on the game thread). `vulkan`: the device is started on bgfx's Vulkan backend.
    void start(bool render_thread, bool vulkan);
    // Game thread, once per presented frame: takes in what the presenting thread reported (a new Vulkan swap
    // chain: the swap interval is chosen for it).
    void game_frame();
    // Game thread: it was blocked this long handing a frame to the render thread, which is not work (ADPF). Without
    // a render thread the present itself, on the game thread, reports its wait.
    void game_thread_waited(std::chrono::nanoseconds wait);
    // Game thread: bgfx's render thread (Linux tid) shares the frame's work; ADPF's hint session takes it in.
    void render_thread_started(int tid);
    // Game thread: the swap interval of the frame rate setting the frames are paced at; zero when no frame rate is
    // paced (desktop).
    std::chrono::nanoseconds interval() const;
    // Presenting thread: how long the presents since the last call waited (zero without pacing).
    std::chrono::nanoseconds take_present_wait();
    // The window the game presents to (Swappy reports the frame rate for it); nullptr while there is none.
    void set_window(void *native_window);
    // The frame rate to pace at, frames per second; applies at once (logged), before start() at its start.
    void set_frame_rate(uint32_t fps);
    // Game thread: while on, every present interval is also kept for take_intervals() (the automatic graphics
    // preset's measurement, renderer auto_preset.hpp); off drops what was kept.
    void set_sampling(bool on);
    // Game thread: the present intervals (ms) kept since the last call.
    std::vector<float> take_intervals();
    // Game thread: the renderer bench's presentation (storm/render/frame_rate.hpp SetBenchPresentation).
    void set_bench(bool on);
    render::BenchFrames take_bench_frames();
    bool gpu_times_arrive() const;
    void shutdown();

  private:
#ifdef __ANDROID__
    static bool present(void *display, void *surface);
    // bgfx::VkPresentHooks (presenting thread).
    static void vk_swap_chain_created(void *physical_device, void *device, void *queue, uint32_t queue_family,
                                      uint64_t swap_chain, void *native_window);
    static void vk_swap_chain_destroying(void *device, uint64_t swap_chain);
    static int32_t vk_present(void *queue, const void *present_info);
    // Presenting thread: the present took from `before` to `after`.
    void presented(std::chrono::steady_clock::time_point before, std::chrono::steady_clock::time_point after);
    // What Swappy paces at now and the display's refresh period, for the backend in use.
    std::chrono::nanoseconds swappy_interval();
    std::chrono::nanoseconds swappy_refresh();
    // `display_period` (ns) > 0: the display did not enter the chosen mode; choose among whole refreshes of it.
    void choose_interval(uint64_t display_period = 0);
    // Game thread, kModeCheckDelay after a choice: the display runs the chosen mode, else choose again.
    void check_display_mode();
    // Any presenting path without Swappy: says why as an error, reads the display's rate for the statistics.
    void unpaced(const char *why);
    void log_stats(double seconds);
    void reset_stats();

    bool started_ = false;
    bool render_thread_ = false; // the presents run on bgfx's render thread
    bool vulkan_ = false;        // Swappy Vk; false: Swappy GL
    // Vulkan: the swap chain Swappy Vk paces (zero: none) and its device, set by the presenting thread; the game
    // thread's Swappy calls for it hold chain_mutex_ so the swap chain cannot go meanwhile.
    std::mutex chain_mutex_;
    void *vk_device_ = nullptr;
    uint64_t vk_swap_chain_ = 0;
    uint64_t vk_refresh_ns_ = 0;             // SwappyVk_initAndGetRefreshCycleDuration
    uint64_t vk_interval_ns_ = 0;            // the interval the game thread chose last; a new swap chain starts on it
    std::atomic<bool> chain_changed_{false}; // a new swap chain: the game thread chooses its interval
    void *vulkan_library_ = nullptr;         // the loader, for vkQueuePresentKHR without Swappy
    int32_t (*vk_queue_present_)(void *queue, const void *present_info) = nullptr;
    std::chrono::nanoseconds interval_{0}; // game thread: the interval of the setting
    static constexpr std::chrono::seconds kModeCheckDelay{1};
    uint64_t mode_check_period_ = 0; // game thread: the mode period to check at mode_check_at_; 0: none
    std::chrono::steady_clock::time_point mode_check_at_{};
    std::chrono::nanoseconds present_wait_{0}; // presenting thread
    std::chrono::nanoseconds target_ = kDefaultFrame;
    std::atomic<bool> swappy_{false}; // false: no Swappy, presents with eglSwapBuffers / vkQueuePresentKHR
    // What follows is written by the present (render thread) and by the game thread's interval choice.
    std::mutex mutex_;
    SwapIntervalWatch swap_interval_;
    std::chrono::nanoseconds refresh_period_{0};
    std::chrono::steady_clock::time_point last_present_{};
    std::chrono::steady_clock::time_point window_start_{};
    std::vector<float> intervals_ms_;
    double wait_ms_ = 0.0;
    bool sampling_ = false;
    std::vector<float> samples_ms_; // set_sampling / take_intervals
    // The bench: presents bypass Swappy (read by the presenting thread), which is set to the fastest display mode.
    std::atomic<bool> bench_{false};
    // Game thread: each frame's GPU time once (bgfx repeats the last result in frames without a new one).
    uint32_t last_gpu_frame_ = UINT32_MAX;
    bool gpu_seen_ = false;
    std::vector<render::GpuFrameTime> bench_gpu_;
#endif
};

} // namespace storm::shim
