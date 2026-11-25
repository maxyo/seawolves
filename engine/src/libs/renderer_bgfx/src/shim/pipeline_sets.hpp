// The scenes' Vulkan pipelines (storm/render/pipelines.hpp, patches/bgfx-vulkan-pipeline-cache.patch): recorded per
// scene and prewarmed while it loads. Design: renderer_bgfx/README.md, "Vulkan pipeline sets".
#pragma once

#include <bgfx/bgfx.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace storm::shim
{

class Device;

class PipelineSets
{
  public:
    // The longest a fade-in waits for the scene's pipelines; the rest are created while it is shown, their draws
    // left out until then (each logged).
    static constexpr auto kRevealBound = std::chrono::seconds(5);

    static PipelineSets &instance();

    // ProgramCache::attach_device, before bgfx::init: `directory` empty = nothing is kept between runs. Sets the
    // pipeline threads and the creation report; begins the scene "start" (the menus).
    void attach(const std::filesystem::path &directory, bool vulkan);
    // ProgramCache::detach_device, after bgfx::shutdown: logs the device's totals, writes the file.
    void detach();
    // ProgramCache::cacheWrite of the pipeline cache: the sets are written with it.
    void save();

    // A program made outside the shim (the interface's), by its shaders' names: recorded pipelines of it wait for it
    // (they are not forgotten) and are prewarmed once it is registered. Unregister before it is destroyed.
    void register_program(const char *vs, const char *fs, bgfx::ProgramHandle program);
    void unregister_program(bgfx::ProgramHandle program);

    void begin_scene(const char *scene);
    bool hold_reveal(bool strict = false);
    // Device::Present, after the frame was handed to bgfx: the prewarms asked for go to bgfx; the reveal and the
    // measured window advance.
    void frame(Device &device, uint32_t presented_frame, bool scene_frame);
    // Context::surface_lost: the measured window ends there and a fade-in's wait starts again on return; a stay in the
    // background is neither a frame time nor a wait for pipelines.
    void surface_lost();

  private:
    struct Recipe
    {
        std::string vs, fs; // bgfx::setName of the shaders
        std::vector<uint8_t> bytes;
    };
    // bgfx::setVkPipelineCreated (render thread; the game thread without a render thread).
    static void created(const char *vs, const char *fs, uint64_t state, double ms, uint32_t flags, uint32_t hash,
                        const void *recipe, uint32_t size);
    void forget(uint32_t hash);
    void load();
    void write_file();
    bgfx::ProgramHandle program_of(Device &device, const Recipe &recipe);
    void start_measure(std::chrono::steady_clock::time_point now);
    void end_measure(const char *why);

    std::mutex mutex_;
    bool vulkan_ = false;
    std::filesystem::path file_;                       // empty: nothing kept
    std::string build_;                                // the engine's build ID: the file belongs to one build
    std::unordered_map<uint32_t, Recipe> recipes_;     // by pipeline hash
    std::map<std::string, std::set<uint32_t>> scenes_; // the pipelines each scene created, by hash
    bool dirty_ = false;
    std::thread writer_;
    std::string scene_;
    std::set<uint32_t> created_; // this device's pipelines (created or asked for), by hash
    std::vector<uint32_t> to_prewarm_;
    std::map<std::string, bgfx::ProgramHandle>
        programs_;               // by "vs+fs": the fx package's, the interface's, made for prewarms
    std::set<uint32_t> waiting_; // recorded pipelines whose program was not made yet (the interface's: at its init)

    // The scene begun last, until its reveal.
    struct Reveal
    {
        bool armed = false, holding = false;
        std::chrono::steady_clock::time_point begun{}, hold_started{};
        std::chrono::steady_clock::time_point lost{}; // the surface went while armed: begun moves on by the stay
        uint32_t hold_frame = 0, query_frame = 0;     // when the fade-in first asked; last asked
        uint32_t asked = 0, refused = 0;              // prewarms handed to bgfx; not handed (other format)
        uint32_t prewarmed = 0, unusable = 0;         // reported by bgfx
        double prewarm_ms = 0.0;                      // compile time of the prewarms (summed over the threads)
        uint32_t waited = 0;                          // pipelines a scene draw waited for under the fader
        uint32_t scene_frames = 0;                    // presented without a fade-in asking (no fader: no reveal)
    } reveal_;
    uint32_t presented_frame_ = 0;
    bool caps_logged_ = false;

    // The first kMeasureWindow after a reveal.
    struct Measure
    {
        bool active = false;
        std::string scene;
        std::chrono::steady_clock::time_point started{}, last{};
        std::vector<float> frame_ms;
        uint32_t left_out_start = 0, incomplete_frames = 0, unpredicted = 0;
        double incomplete_ms = 0.0, unpredicted_ms = 0.0;
        uint32_t last_waiting = 0;
    } measure_;

    // Totals of this device.
    uint32_t pipelines_ = 0, render_thread_ = 0, slow_ = 0, off_thread_ = 0, prewarms_ = 0, unpredicted_ = 0;
    double render_thread_ms_ = 0.0, off_thread_ms_ = 0.0;
};

} // namespace storm::shim
