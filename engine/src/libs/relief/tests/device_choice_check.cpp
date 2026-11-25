// Real Session::Upscale and the real x4-to-x2 pipeline/cache writer: each texture's device follows the game's state
// (device_choice.hpp, Session::SetGame). Only device/model inference is deterministic.
#include "loading_retry.hpp"
#include "relief.hpp"
#include "backend.hpp"
#include "storm/fx/cpu_admission.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <unistd.h>
#include <vector>
namespace
{
int cpu_runs = 0, gpu_runs = 0, gpu_loads = 0;
bool fail_gpu_load = false, fail_cpu_height = false, gpu_open = false;
bool shown = false, free_gpu = false; // the game's state, as the generator reports it
std::function<void()> on_gpu_tile;    // a GPU tile is about to be submitted
int live_cpu_models = 0;
std::function<void()> on_cpu_tile;      // a CPU tile is about to run, under its permit
std::vector<unsigned> cpu_tile_threads; // each CPU tile's permitted threads
std::function<void()> on_gpu_load;      // a GPU network begins to load (its calibration sees what this does)
std::function<void()> on_gpu_loaded;    // a GPU network has loaded (its pipelines made after its calibration)
std::string gpu_marker;
void expect(bool v, const char *s)
{
    if (!v)
    {
        std::fprintf(stderr, "FAIL: %s\n", s);
        std::exit(1);
    }
    std::printf("ok: %s\n", s);
}
class Model final : public storm::relief::Network
{
    bool gpu_;
    int scale_;
    std::function<bool()> yield_;
    std::function<std::shared_ptr<const unsigned>()> admission_;

  public:
    Model(bool gpu, int scale) : gpu_(gpu), scale_(scale)
    {
        if (!gpu_)
            ++live_cpu_models;
    }
    ~Model() override
    {
        if (!gpu_)
            --live_cpu_models;
    }
    bool Infer(const float *, int w, int h, float *out, std::string &error) override
    {
        if (gpu_)
        {
            if (on_gpu_tile)
                on_gpu_tile();
            if (yield_ && yield_())
            {
                error = "yielded";
                return false;
            } // the production GPU network's check before each submission
            ++gpu_runs;
        }
        else
        {
            const auto permit = admission_ ? admission_() : nullptr;
            if (admission_ && !permit)
            {
                error = "CPU admission cancelled";
                return false;
            }
            if (permit)
                cpu_tile_threads.push_back(*permit);
            if (on_cpu_tile)
                on_cpu_tile();
            ++cpu_runs;
        }
        std::fill(out, out + size_t(w) * h * 3 * scale_ * scale_, .5f);
        return true;
    }
    void SetCpuAdmission(std::function<std::shared_ptr<const unsigned>()> admission) override
    {
        admission_ = std::move(admission);
    }
    int TileCeiling() const override
    {
        return 32; // a 64-texel texture in four tiles
    }
    void Cancel() override
    {
    }
    void SetYield(std::function<bool()> yield) override
    {
        yield_ = std::move(yield);
    }
    std::string Describe() const override
    {
        return gpu_ ? "GPU deterministic" : "CPU deterministic";
    }
};
class Runtime final : public storm::relief::CpuRuntime
{
    int loads_ = 0;

  public:
    ~Runtime() override
    {
        expect(live_cpu_models == 0, "CPU runtime outlives every network");
    }
    std::unique_ptr<storm::relief::Network> Load(std::span<const uint8_t>, std::span<const uint8_t>, int, int scale,
                                                 std::string &error) override
    {
        if (++loads_ == 2 && fail_cpu_height)
        {
            error = "synthetic partial CPU height load failure";
            return nullptr;
        }
        return std::make_unique<Model>(false, scale);
    }
    size_t JobPeak() const override
    {
        return 0;
    }
    std::pair<unsigned, unsigned> JobThreads() const override
    {
        return {0, 0};
    }
    void EndJob() override
    {
    }
};
void write_tx(const std::filesystem::path &p)
{
    constexpr int size = 64;
    const int32_t head[] = {0, size, size, 7, 21, size * size * 4};
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(head), sizeof(head));
    for (int s = size; s; s /= 2)
        for (int i = 0; i < s * s; ++i)
        {
            const uint8_t rgba[] = {70, 100, 180, 255};
            out.write(reinterpret_cast<const char *>(rgba), 4);
        }
}
} // namespace
namespace storm::relief
{
std::unique_ptr<CpuRuntime> CpuRuntime::Open(int, std::string &, int)
{
    return std::make_unique<Runtime>();
}
const GpuDevice &Gpu()
{
    if (!gpu_open)
    {
        expect(gpu_marker == kGpuOpening, "lazy GPU reopening has opening-Vulkan crash guard");
        gpu_open = true;
    }
    static GpuDevice gpu{true, "deterministic", "1", {}};
    return gpu;
}
bool ReleaseGpu()
{
    const bool was = gpu_open;
    gpu_open = false;
    return was;
}
std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t>, std::span<const uint8_t>, int, int scale,
                                        std::function<bool()> yield, std::string &error)
{
    ++gpu_loads;
    if (on_gpu_load)
        on_gpu_load();
    if (yield())
    {
        error = "ncnn could not run the network"; // the calibration's submissions stop when the scene shows
        return nullptr;
    }
    if (fail_gpu_load)
    {
        error = "synthetic controlled GPU initialization failure";
        return nullptr;
    }
    auto network = std::make_unique<Model>(true, scale);
    network->SetYield(std::move(yield));
    if (on_gpu_loaded)
        on_gpu_loaded();
    return network;
}
bool GpuPossible(const std::filesystem::path &)
{
    MarkGpuRunning({}, kGpuOpening);
    Gpu();
    ClearGpuRunning({});
    return true;
}
void MarkGpuRunning(const std::filesystem::path &, const std::string &what)
{
    gpu_marker = what;
}
void ClearGpuRunning(const std::filesystem::path &)
{
    gpu_marker.clear();
}
std::string GpuCrash(const std::filesystem::path &)
{
    return {};
}
std::string GpuLabel(const GpuDevice &)
{
    return "deterministic";
}
} // namespace storm::relief
int main(int argc, char **argv)
{
    using namespace storm::relief;
    expect(argc == 2, "isolated fixture root supplied");
    std::filesystem::path root = std::filesystem::path(argv[1]) /
                                 ("run-" + std::to_string(getpid()) + "-" +
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
                          textures = root / "Textures";
    std::filesystem::create_directories(textures / "Characters");
    const uint8_t m[] = {1};
    Models models{m, m, m, m, m, m, m, m};
    std::string error;
    int index = 0;
    std::filesystem::path last;
    auto run = [&](Session &session) {
        last = textures / "Characters" / ("Face-" + std::to_string(index++) + ".tga.tx");
        write_tx(last);
        return session.Upscale(textures, last, {root / "x2", true}, 64, error);
    };
    auto game = [&](Session &session) { session.SetGame([] { return free_gpu; }, [] { return shown; }); };
    int T = 0; // inferences a texture (its x4 tiles): counted on the first one
    auto runs = [&](int cpu, int gpu) {
        const bool same = cpu_runs == cpu * T && gpu_runs == gpu * T;
        cpu_runs = gpu_runs = 0;
        return same;
    };
    auto deadline = [] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count()) +
               10000;
    };
    // The game: its state decides.
    auto session = Session::Open(models, 1, root, error);
    expect(bool(session), "real production Session opens");
    expect(session->stats().gpu_possible && gpu_marker.empty(), "the GPU is usable and no GPU work is left marked");
    game(*session);
    shown = true;
    free_gpu = false;
    expect(run(*session) == Outcome::Prepared, "played: x2 prepared");
    T = cpu_runs;
    expect(T >= 1 && gpu_runs == 0, "played: every inference on the CPU, none on the GPU");
    cpu_runs = 0;
    for (int i = 0; i < 2; ++i)
        expect(run(*session) == Outcome::Prepared && runs(1, 0), "played: the next ones on the CPU too");
    expect(gpu_loads == 0, "played: no GPU network is even loaded");
    expect(!session->stats().gpu && session->stats().device_why == DeviceReason::SceneShown,
           "played: the CPU, because the scene is shown");
    shown = true;
    free_gpu = true; // the frames say quiet but the last one showed a scene: the scene wins
    expect(run(*session) == Outcome::Prepared && runs(1, 0), "the scene shown wins over a stale free GPU");
    // A loading or an interface screen without a scene.
    shown = false;
    free_gpu = true;
    expect(run(*session) == Outcome::Prepared && runs(0, 1), "no scene: the GPU");
    expect(session->stats().gpu && session->stats().device_why == DeviceReason::GpuFree && gpu_marker.empty(),
           "no scene: on the GPU because it is free; the crash guard cleared after the texture");
    shown = true;
    free_gpu = false;
    expect(run(*session) == Outcome::Prepared && runs(1, 0) && !session->stats().gpu,
           "played again: back on the CPU at once");
    // The scene under the loading cover: one texture in the window, at most one a minute.
    shown = false;
    free_gpu = false;
    expect(run(*session) == Outcome::Prepared && runs(1, 0) && session->stats().device_why == DeviceReason::GameBusy,
           "under the loading cover without a window: the CPU");
    auto window = std::make_shared<LoadingRetry>(deadline());
    session->SetLoadingRetry(window);
    expect(run(*session) == Outcome::Prepared && runs(0, 1) && window->used &&
               session->stats().device_why == DeviceReason::LoadingWindow,
           "the loading window: one texture on the GPU");
    expect(run(*session) == Outcome::Prepared && runs(1, 0), "the window's texture is used: the CPU");
    window = std::make_shared<LoadingRetry>(deadline());
    session->SetLoadingRetry(window);
    expect(run(*session) == Outcome::Prepared && runs(1, 0) && !window->used,
           "a renewed window within the minute: the CPU");
    session->SetLoadingRetry(nullptr);
    // The scene starts while a texture runs on the GPU: it stops at its next submission, nothing is written, no crash
    // is recorded; the same texture is then made on the CPU. A new session: its GPU is unproven (the crash guard marks
    // it).
    session.reset();
    session = Session::Open(models, 1, root, error);
    game(*session);
    shown = false;
    free_gpu = true;
    int tiles = 0;
    on_gpu_tile = [&] {
        expect(gpu_marker == "deterministic", "the unproven GPU's work is marked");
        ++tiles;
        shown = true;
    };
    expect(run(*session) == Outcome::Yielded, "scene shown mid-texture: the GPU texture yields");
    on_gpu_tile = nullptr;
    expect(cpu_runs == 0 && gpu_runs == 0 && tiles == 1,
           "it stopped at its next submission: nothing more submitted, nothing on the CPU yet");
    cpu_runs = gpu_runs = 0;
    expect(gpu_marker.empty(), "a yielded GPU texture leaves no crash marker");
    const auto again = session->Upscale(textures, last, {root / "x2", true}, 64, error);
    expect(again == Outcome::Prepared && runs(1, 0) && !session->stats().gpu, "the yielded texture is made on the CPU");
    expect(session->Upscale(textures, last, {root / "x2", true}, 64, error) == Outcome::Existing,
           "and its x2 is stored once");
    session.reset();
    // GPU networks that fail to load: the texture on the CPU, and the session gives the GPU up.
    fail_gpu_load = true;
    session = Session::Open(models, 1, root, error);
    game(*session);
    shown = false;
    free_gpu = true;
    gpu_loads = 0;
    expect(run(*session) == Outcome::Prepared && runs(1, 0), "a texture survives a GPU initialization error");
    expect(!session->stats().gpu_possible && session->stats().device_why == DeviceReason::NoGpu,
           "a GPU model error ends GPU use for the session");
    fail_gpu_load = false;
    expect(run(*session) == Outcome::Prepared && runs(1, 0) && gpu_loads == 1, "then the CPU, with the GPU free");
    session.reset();
    // The CPU's networks load at the opening: a failure leaves no session and no partial networks.
    fail_cpu_height = true;
    expect(!Session::Open(models, 1, root, error) && live_cpu_models == 0,
           "a partial CPU network load opens no session and keeps none");
    fail_cpu_height = false;
    session = Session::Open(models, 1, root, error);
    expect(bool(session), "the next opening succeeds");
    // The scene shows while the GPU networks load (each is seconds of pipeline creation and a calibration): the one
    // loading stops at its calibration's next submission, no further one starts, the texture is made on the CPU, and
    // it counts as no GPU failure.
    session.reset();
    session = Session::Open(models, 1, root, error);
    game(*session);
    shown = false;
    free_gpu = true;
    gpu_loads = 0;
    on_gpu_load = [&] {
        if (gpu_loads == 2)
            shown = true;
    };
    expect(run(*session) == Outcome::Prepared && runs(1, 0) && gpu_loads == 2,
           "the scene shows during the second network's load: it stops, no third starts, the texture on the CPU");
    expect(session->stats().device_why == DeviceReason::SceneShown && session->stats().gpu_possible &&
               gpu_marker.empty(),
           "on the CPU because the scene shows; the GPU stays possible and no GPU work is left marked");
    on_gpu_load = nullptr;
    for (int i = 0; i < 3; ++i)
        expect(run(*session) == Outcome::Prepared && runs(1, 0) && gpu_loads == 2,
               "played: the rest of the GPU networks are not loaded");
    shown = false;
    free_gpu = true;
    expect(run(*session) == Outcome::Prepared && runs(0, 1) && gpu_loads == 5,
           "the GPU free again: the three remaining networks load and the texture runs on the GPU");
    // The scene shows while a network's pipelines are made (no submission to stop at): that network is kept, no
    // further one starts; once all are loaded a shown scene still sends the texture to the CPU.
    session.reset();
    session = Session::Open(models, 1, root, error);
    game(*session);
    gpu_loads = 0;
    on_gpu_loaded = [&] { shown = true; };
    for (int i = 1; i <= 4; ++i)
    {
        shown = false;
        free_gpu = true;
        expect(run(*session) == Outcome::Prepared && runs(1, 0) && gpu_loads == i,
               "the scene shows as a network finishes loading: one network a texture, the texture on the CPU");
    }
    on_gpu_loaded = nullptr;
    shown = false;
    expect(session->stats().gpu_possible && run(*session) == Outcome::Prepared && runs(0, 1) && gpu_loads == 4,
           "every network loaded: the GPU once it is free, nothing loaded again");
    // The CPU's share follows the game per tile: a texture begun under the loading picture (every core) runs its
    // tiles after the reveal on the gameplay share.
    session.reset();
    session = Session::Open(models, 0, root, error);
    game(*session);
    shown = false;
    free_gpu = false;
    auto &admission = storm::fx::SharedCpuAdmission();
    const unsigned cores = admission.maximum_threads(), play = admission.gameplay_threads();
    if (cores <= play)
    {
        std::printf("NOT_RUN: %u cores leave no share above the gameplay one\n", cores);
        return 77;
    }
    admission.begin(41, true);
    admission.opaque(41, true);
    cpu_tile_threads.clear();
    on_cpu_tile = [&] { admission.end(41); }; // the loading picture lifts during the first tile
    expect(run(*session) == Outcome::Prepared && runs(1, 0), "a texture across the reveal is made");
    on_cpu_tile = nullptr;
    expect(cpu_tile_threads.size() >= 2 && cpu_tile_threads.front() == cores,
           "its first tile under the loading picture: every core");
    expect(std::all_of(cpu_tile_threads.begin() + 1, cpu_tile_threads.end(), [&](unsigned t) { return t == play; }),
           "its tiles after the reveal: the gameplay share");
    std::puts("production Session: the device follows the game's state: PASS");
}
