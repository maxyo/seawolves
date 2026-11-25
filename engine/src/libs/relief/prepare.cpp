#include "storm/fx/cpu_admission.hpp"
#include "storm/render/log.hpp"
#include "loading_retry.hpp"
#include "relief.hpp"
#include "finalizer.hpp"

#include "backend.hpp"
#include "device_choice.hpp"
#include "tx_file.hpp"

#include "formats.hpp"
#include "eac_encoder.hpp"
#include "storm/fx/relief_model.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/height_map.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/relief_store.hpp"

#include "upscale.hpp"
#include "cache.hpp"
#include "storm/fx/upscale.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace storm::relief
{

namespace
{
using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point since)
{
    return std::chrono::duration<double>(Clock::now() - since).count();
}

// A texture that could not be made: an error, unless its session was cancelled (the game's shutdown).
void LogFailure(const std::string &name, const std::string &failure, bool cancelled)
{
    if (cancelled)
        render::Log().debug("relief {}: cancelled", name);
    else
        render::Log().error("no relief map for {}: {}", name, failure);
}

struct Candidate
{
    std::filesystem::path file;
    std::string name; // normalized texture path
    fx::ReliefClass relief = fx::ReliefClass::None;
    fx::GlossClass gloss = fx::GlossClass::None;
};

std::string Lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A relief or gloss texture: the rules give it relief or gloss, or its name says "bump" (its pixels decide).
std::optional<Candidate> Classify(const std::filesystem::path &textures, const std::filesystem::path &file)
{
    std::error_code error;
    if (Lower(file.extension().string()) != ".tx")
        return std::nullopt;
    const std::string name =
        fx::NormalizeTexturePath(std::filesystem::relative(file, textures, error).generic_string());
    if (error)
        return std::nullopt;
    const fx::ReliefVerdict verdict = fx::ClassifyTextureName(name);
    const bool bump =
        fx::NameSuggestsHeightMap(name) && !(verdict.by_folder && verdict.relief == fx::ReliefClass::None);
    const fx::GlossClass gloss = fx::ClassifyGlossName(name).gloss;
    if (verdict.relief == fx::ReliefClass::None && !bump && gloss == fx::GlossClass::None)
        return std::nullopt;
    return Candidate{file, name, verdict.relief, gloss};
}

// The texture's key under `method`, or nothing when it gets no prepared map (unknown format, too small for a probe
// level).
std::optional<uint64_t> Key(const Candidate &candidate, const TxFile &tx, uint64_t method, int map_size)
{
    const int probe =
        fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    if (probe < 0)
        return std::nullopt;
    const std::span<const uint8_t> bytes = tx.Level(probe);
    const int pw = static_cast<int>(tx.width >> probe), ph = static_cast<int>(tx.height >> probe);
    if (bytes.size() != texture::d3d_level_bytes(tx.format, pw, ph))
        return std::nullopt;
    return fx::ReliefKey(candidate.name, bytes, pw, ph, method, map_size);
}

// `height`: the map with PBRify's height in its material (fx::ReliefMethod).
std::optional<uint64_t> Key(const Candidate &candidate, const TxFile &tx, int map_size, bool height)
{
    return candidate.relief == fx::ReliefClass::None
               ? std::nullopt
               : Key(candidate, tx, fx::ReliefMethod(candidate.relief, height), map_size);
}

// The key of the texture's own gloss map: a texture of a gloss class whose relief class has no height (the others
// keep their gloss in their relief's material texture).
std::optional<uint64_t> GlossKey(const Candidate &candidate, const TxFile &tx)
{
    if (candidate.gloss == fx::GlossClass::None || fx::ReliefHasHeight(candidate.relief))
        return std::nullopt;
    return Key(candidate, tx, fx::GlossMethod(candidate.gloss), fx::kGlossMapSize);
}

// Box halvings of an RGBA8 image down to `tw` x `th` (a size NormalMapSize gives: the image halved, or itself).
std::vector<uint8_t> HalveTo(std::vector<uint8_t> rgba, int &w, int &h, int tw, int th)
{
    while (w > tw || h > th)
    {
        const int nw = w > tw ? w / 2 : w, nh = h > th ? h / 2 : h, sx = w / nw, sy = h / nh;
        std::vector<uint8_t> out(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int dy = 0; dy < sy; ++dy)
                        for (int dx = 0; dx < sx; ++dx)
                            sum += rgba[(static_cast<size_t>(y * sy + dy) * w + x * sx + dx) * 4 + c];
                    out[(static_cast<size_t>(y) * nw + x) * 4 + c] =
                        static_cast<uint8_t>((sum + sx * sy / 2) / (sx * sy));
                }
        rgba = std::move(out);
        w = nw;
        h = nh;
    }
    return rgba;
}

// The same for one float plane.
std::vector<float> HalveTo(std::vector<float> v, int w, int h, int tw, int th)
{
    while (w > tw || h > th)
    {
        const int nw = w > tw ? w / 2 : w, nh = h > th ? h / 2 : h, sx = w / nw, sy = h / nh;
        std::vector<float> out(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                float sum = 0.0f;
                for (int dy = 0; dy < sy; ++dy)
                    for (int dx = 0; dx < sx; ++dx)
                        sum += v[static_cast<size_t>(y * sy + dy) * w + x * sx + dx];
                out[static_cast<size_t>(y) * nw + x] = sum / static_cast<float>(sx * sy);
            }
        v = std::move(out);
        w = nw;
        h = nh;
    }
    return v;
}

// A plane of `w` x `h` bilinearly stretched (wrapping: the textures tile) over `tw` x `th`, texel centres aligned.
std::vector<uint8_t> Stretch(std::span<const float> v, int w, int h, int tw, int th)
{
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
        {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(w) / static_cast<float>(tw) - 0.5f;
            const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(h) / static_cast<float>(th) - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
            const float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
            const auto at = [&](int xi, int yi) {
                return v[static_cast<size_t>(((yi % h) + h) % h) * w + static_cast<size_t>(((xi % w) + w) % w)];
            };
            const float value = (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) +
                                (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
            out[static_cast<size_t>(y) * tw + x] =
                static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }
    return out;
}

// A grey "bump" texture: a height map the game derives itself (the runtime classifier decides from the same level).
bool GreyHeightMap(const Candidate &candidate, const TxFile &tx)
{
    if (!fx::NameSuggestsHeightMap(candidate.name))
        return false;
    const int probe =
        fx::ReliefProbeLevel(static_cast<int>(tx.width), static_cast<int>(tx.height), static_cast<int>(tx.levels));
    if (probe < 0)
        return false;
    const uint32_t pw = tx.width >> probe, ph = tx.height >> probe;
    std::vector<uint8_t> rgba(size_t(pw) * ph * 4);
    return texture::decode_rgba8(tx.format, tx.Level(probe).data(), pw, ph, rgba.data()) && fx::IsGrayscale(rgba);
}

// The key of a texture that gets a prepared map, with the class it is prepared as.
std::optional<uint64_t> PreparedKey(const Candidate &candidate, const TxFile &tx, int map_size, bool height,
                                    fx::ReliefClass &relief)
{
    const std::optional<uint64_t> key = Key(candidate, tx, map_size, height);
    relief = key && !GreyHeightMap(candidate, tx) ? candidate.relief : fx::ReliefClass::None;
    return relief == fx::ReliefClass::None ? std::nullopt : key;
}

} // namespace

namespace
{
// The game's ×2 of a texture (fx/upscale.hpp): its chain encoded for the device's tier, into the upscale directory.
bool WriteDisplay(const UpscaleOutput &display, uint64_t key, const std::vector<uint8_t> &x2, int width, int height,
                  const std::string &name)
{
    const Clock::time_point start = Clock::now();
    bool opaque = true;
    for (size_t i = 3; i < x2.size() && opaque; i += 4)
        opaque = x2[i] == 255;
    const texture::GpuFormat format = display.bc
                                          ? (opaque ? texture::GpuFormat::Bc1 : texture::GpuFormat::Bc3)
                                          : (opaque ? texture::GpuFormat::Etc2Rgb8 : texture::GpuFormat::Etc2Rgba8);
    const texture::Blob ktx =
        texture::encode_rgba_to_ktx(x2.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), format);
    if (!fx::WriteUpscaled(display.directory, key, {ktx.data.get(), ktx.size}))
    {
        render::Log().error("×2 of {}: cannot write {}", name, fx::UpscaleFile(display.directory, key).string());
        return false;
    }
    render::Log().info("×2 {}: {}x{} {}, {:.1f} MB, mips and encode {:.0f} ms", name, width, height,
                       opaque ? "opaque" : "with alpha", static_cast<double>(ktx.size) / 1048576.0,
                       Seconds(start) * 1000.0);
    return true;
}

} // namespace

// Which device a texture runs on: device_choice.hpp. Each change is logged by the game (relief_generator.cpp); the
// crash record of the GPU work is kept across sessions (backend.hpp MarkGpuRunning).
struct Session::Impl
{
    Models models; // owned by the caller, alive with the session
    int threads = 1;
    std::filesystem::path cache;
    bool gpu_possible = false;                   // a GPU, its graphs, no crash recorded for it
    bool on_gpu = false;                         // the last texture's device
    std::function<bool()> gpu_free, scene_shown; // the game's state (SetGame)
    std::shared_ptr<LoadingRetry> loading_retry;
    uint64_t loading_retry_at = 0;
    bool TakeLoadingRetry()
    {
        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        if (!loading_retry || !loading_retry->take(now, gpu_possible, loading_retry_at))
            return false;
        loading_retry_at = now;
        return true;
    }
    bool Shown() const
    {
        return scene_shown && scene_shown();
    }
    // The device of the next texture (device_choice.hpp), its networks loaded: false with `failure` when they cannot
    // be.
    bool PickDevice(bool &use_gpu, std::string &failure)
    {
        DeviceChoice choice =
            ChooseTextureDevice(gpu_possible, Shown(), gpu_free && gpu_free(), [this] { return TakeLoadingRetry(); });
        if (!EnsureDevice(choice, failure))
            return false;
        use_gpu = on_gpu = choice.gpu;
        stats.device_why = choice.why;
        Describe();
        return true;
    }
    // A GPU texture stopped because the player sees the scene: no crash, its work ended (the guard's marker goes; the
    // GPU stays unproven until a texture completes on it).
    bool Yielded(bool use_gpu, const std::string &name)
    {
        if (!use_gpu || cancelled || !Shown())
            return false;
        if (gpu_unproven)
            ClearGpuRunning(cache);
        render::Log().info("relief {}: stopped on the GPU (the player sees the game's scene): made again on the CPU",
                           name);
        return true;
    }
    bool gpu_unproven = false;           // the GPU's first texture: the crash guard's marker is written around it
    std::unique_ptr<CpuRuntime> runtime; // before the networks: released after them
    std::array<std::unique_ptr<Network>, 3> gpu, cpu;     // normal, height, roughness
    std::unique_ptr<Upscaler> gpu_upscaler, cpu_upscaler; // the 4x upscaler (fx/upscale.hpp), on each device
    std::mutex networks_mutex;                            // swaps against Cancel from another thread
    Stats stats;
    std::atomic<bool> cancelled{false};
    std::function<bool(size_t)> compute_admission;
    std::unique_ptr<Finalizer> synchronous_finalizer; // compute owner only, never used by finalizer thread
    std::shared_ptr<std::atomic<bool>> finalize_cancelled = std::make_shared<std::atomic<bool>>(false);
    inline static std::atomic<uint64_t> next_session{0};
    const uint64_t session = ++next_session;

    std::array<std::unique_ptr<Network>, 3> &Active()
    {
        return on_gpu ? gpu : cpu;
    }
    // Around each texture's computation, however it ends: the CPU networks' memory goes back to the system after it.
    struct CpuJob
    {
        Impl &impl;
        ~CpuJob()
        {
            if (!impl.runtime)
                return;
            impl.stats.cpu_memory_peak = impl.runtime->JobPeak();
            impl.runtime->EndJob();
        }
    };
    // The device's description for the texture's log line; on the CPU with the networks' memory peak of this texture.
    std::string JobProvider() const
    {
        if (on_gpu || !runtime)
            return stats.provider;
        const auto [fewest, most] = runtime->JobThreads();
        return fmt::format("{}; tiles on {}..{} threads; networks' memory peak {:.0f} MB", stats.provider, fewest, most,
                           static_cast<double>(runtime->JobPeak()) / 1048576.0);
    }
    enum class GpuLoad
    {
        Ready,
        SceneShown, // the scene showed before every network was loaded: the loaded ones are kept
        Failed,
    };
    // The GPU networks, one by one and only while the game leaves the GPU to them: each costs seconds of pipeline
    // creation and, the first time in the process, a calibration on the GPU.
    GpuLoad EnsureGpu(std::string &error)
    {
        if (gpu_upscaler)
            return Shown() ? GpuLoad::SceneShown : GpuLoad::Ready;
        std::lock_guard lock(networks_mutex);
        // The networks' pipelines are made on the GPU now: the crash guard's marker stays until its first texture.
        // A failed load may have closed Vulkan (EnsureDevice): its opening is guarded as in GpuPossible.
        MarkGpuRunning(cache, kGpuOpening);
        gpu_unproven = true;
        MarkGpuRunning(cache, GpuLabel(Gpu()));
        const auto yield = [this] { return Shown(); };
        const struct
        {
            std::span<const uint8_t> param, bin;
            int halo, scale;
        } graphs[] = {{models.normal_param, models.normal_bin, fx::kReliefModelPad, 1},
                      {models.height_param, models.height_bin, fx::kReliefModelPad, 1},
                      {models.roughness_param, models.roughness_bin, fx::kReliefModelPad, 1},
                      {models.upscale_param, models.upscale_bin, fx::kUpscalePad, 4}};
        for (size_t i = 0; i < std::size(graphs); ++i)
        {
            if (i < gpu.size() ? bool(gpu[i]) : bool(gpu_upscaler))
                continue;
            if (Shown())
                return GpuLoad::SceneShown;
            std::unique_ptr<Network> network =
                LoadGpuNetwork(graphs[i].param, graphs[i].bin, graphs[i].halo, graphs[i].scale, yield, error);
            if (!network)
                return Shown() ? GpuLoad::SceneShown : GpuLoad::Failed;
            if (i < gpu.size())
                gpu[i] = std::move(network);
            else
                gpu_upscaler = std::make_unique<Upscaler>(std::move(network));
        }
        return Shown() ? GpuLoad::SceneShown : GpuLoad::Ready;
    }
    bool EnsureCpu(std::string &error)
    {
        if (cpu_upscaler)
            return true;
        std::lock_guard lock(networks_mutex);
        // A failed load must leave no partial set of networks; a transient allocation/model error can be retried on
        // this Session.
        const auto reopen_started = Clock::now();
        auto next_runtime = CpuRuntime::Open(threads, error);
        if (!next_runtime)
            return false;
        std::array<std::unique_ptr<Network>, 3> next_cpu;
        next_cpu[0] = next_runtime->Load(models.normal_param, models.normal_bin, fx::kReliefModelPad, 1, error);
        if (next_cpu[0])
            next_cpu[1] = next_runtime->Load(models.height_param, models.height_bin, fx::kReliefModelPad, 1, error);
        if (next_cpu[1])
            next_cpu[2] =
                next_runtime->Load(models.roughness_param, models.roughness_bin, fx::kReliefModelPad, 1, error);
        if (!next_cpu[2])
            return false;
        auto upscale = next_runtime->Load(models.upscale_param, models.upscale_bin, fx::kUpscalePad, 4, error);
        if (!upscale)
            return false;
        // Publish only a complete set, destroying any old networks before replacing their runtime.
        cpu_upscaler.reset();
        cpu = {};
        runtime = std::move(next_runtime);
        cpu = std::move(next_cpu);
        cpu_upscaler = std::make_unique<Upscaler>(std::move(upscale));
        // Each run (a tile) as many threads as the game's CPU share gives at that moment, at most the session's.
        auto admission = [this] {
            return fx::SharedCpuAdmission().acquire(static_cast<unsigned>(threads),
                                                    [this] { return cancelled.load(); });
        };
        for (auto &network : cpu)
            network->SetCpuAdmission(admission);
        cpu_upscaler->SetCpuAdmission(admission);
        render::Log().info("relief CPU runtime ready: {} threads, model reopen {:.3f} ms", threads,
                           std::chrono::duration<double, std::milli>(Clock::now() - reopen_started).count());
        return true;
    }
    // The networks of the device `choice` names; a GPU choice that cannot load them becomes the CPU, with the reason.
    bool EnsureDevice(DeviceChoice &choice, std::string &error)
    {
        if (!choice.gpu)
            return EnsureCpu(error);
        switch (EnsureGpu(error))
        {
        case GpuLoad::Ready:
            return true;
        case GpuLoad::SceneShown: {
            // Nothing crashed: the marker goes, the GPU stays unproven until a texture completes on it.
            ClearGpuRunning(cache);
            render::Log().info("relief GPU networks: {} of {} loaded when the scene showed: this texture on the CPU, "
                               "the rest when the GPU is free again",
                               std::count_if(gpu.begin(), gpu.end(), [](const auto &n) { return n != nullptr; }) +
                                   (gpu_upscaler ? 1 : 0),
                               gpu.size() + 1);
            choice = {false, DeviceReason::SceneShown};
            return EnsureCpu(error);
        }
        case GpuLoad::Failed:
            break;
        }
        if (cancelled)
            return false;
        // A controlled model-load/calibration error is not a driver crash (no crash record): this session goes on
        // on the CPU, the next one tries the GPU again.
        render::Log().error("relief GPU model initialization failed ({}): this session continues on the CPU", error);
        {
            std::lock_guard lock(networks_mutex);
            gpu_upscaler.reset();
            gpu = {};
        }
        ClearGpuRunning(cache);
        gpu_unproven = false;
        ReleaseGpu();
        gpu_possible = stats.gpu_possible = false;
        choice = {false, DeviceReason::NoGpu}; // its GPU networks failed to load
        error.clear();
        return EnsureCpu(error);
    }
    void Describe()
    {
        stats.gpu = on_gpu;
        stats.provider = Active()[0] ? Active()[0]->Describe() : "-";
    }
    ~Impl()
    {
        // Closed before the GPU's first texture (no work came): nothing crashed.
        if (gpu_unproven)
            ClearGpuRunning(cache);
    }
};

Session::Session(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
Session::~Session() = default;

std::unique_ptr<Session> Session::Open(const Models &models, int threads, const std::filesystem::path &cache,
                                       std::string &error)
{
    auto impl = std::make_unique<Impl>();
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    threads = threads > 0 ? std::min(threads, cores) : cores;
    impl->models = models;
    impl->threads = threads;
    impl->cache = cache;
    for (const auto &bytes : {models.normal_param, models.normal_bin, models.height_param, models.height_bin,
                              models.roughness_param, models.roughness_bin, models.upscale_param, models.upscale_bin})
        if (bytes.empty())
        {
            error = "relief: a network's ncnn graph is missing (relief.hpp kModelFiles)";
            return nullptr;
        }
    impl->gpu_possible = impl->stats.gpu_possible = GpuPossible(cache);
    // The CPU's networks now (the game's device while it is played; a failure here leaves the session unopened), the
    // GPU's at the first texture that runs on it.
    if (!impl->EnsureCpu(error))
    {
        render::Log().error("relief: {}", error);
        return nullptr;
    }
    if (!impl->gpu_possible)
        ReleaseGpu(); // opened to learn the GPU, never used
    impl->Describe();
    return std::unique_ptr<Session>(new Session(std::move(impl)));
}

void Session::SetGame(std::function<bool()> gpu_free, std::function<bool()> scene_shown)
{
    impl_->gpu_free = std::move(gpu_free);
    impl_->scene_shown = std::move(scene_shown);
}

void Session::SetLoadingRetry(std::shared_ptr<LoadingRetry> retry)
{
    impl_->loading_retry = std::move(retry);
}

void Session::Cancel()
{
    impl_->cancelled = true;
    *impl_->finalize_cancelled = true;
    std::lock_guard lock(impl_->networks_mutex);
    for (auto *set : {&impl_->gpu, &impl_->cpu})
        for (const std::unique_ptr<Network> &network : *set)
            if (network)
                network->Cancel();
    for (Upscaler *upscaler : {impl_->gpu_upscaler.get(), impl_->cpu_upscaler.get()})
        if (upscaler)
            upscaler->Cancel();
}

const Session::Stats &Session::stats() const
{
    return impl_->stats;
}

// What ComputeFile's steps share: the texture, its keys, the colour the models read, their outputs and the stages'
// times.
struct Session::Compute
{
    const std::filesystem::path &output;
    int map_size;
    bool with_height;
    std::string &failure;
    const UpscaleOutput *display;
    Clock::time_point start = Clock::now();
    Clock::time_point mark = start;
    std::optional<Candidate> candidate;
    TxFile tx;
    fx::ReliefClass relief = fx::ReliefClass::None;
    std::optional<uint64_t> key, gloss_key;
    double read_ms = 0.0, decode_ms = 0.0, upscale_ms = 0.0, infer_ms = 0.0, model_ms = 0.0;
    int level = 0, width = 0, height = 0;
    std::vector<uint8_t> rgba;
    bool use_gpu = false, from_x2 = false;
    int mw = 0, mh = 0, gw = 0, gh = 0;
    std::string per_model; // each inference: the model, its padded size, its time
    bool height_class = false, gloss_material = false, gloss_wanted = false;
    std::vector<float> normal_planes, height_planes, gloss_height, roughness_planes;

    // The time since the previous lap.
    double lap()
    {
        const Clock::time_point now = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
        return ms;
    }
};

ComputeResult Session::ComputeFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                                   const std::filesystem::path &output, int map_size, bool with_height,
                                   std::string &failure, const UpscaleOutput *display)
{
    // One decode, then PBRify's networks: the normals and (`with_height`) the height at the map size, the
    // roughness at the gloss map's. Outputs: the relief map (EAC RG11 normals, with the material texture of a class
    // with a height: R the height normalised per region, G the gloss; without the height only for a gloss class, G its
    // gloss over a flat R) and, for a texture of a gloss class without a height, its own gloss map (R the metal mask, G
    // the gloss). Each stage is timed: the log line of the texture says where the time went.
    const Impl::CpuJob job{*impl_};
    Compute c{output, map_size, with_height, failure, display};
    if (std::optional<ComputeResult> early = ReadSource(c, textures, file))
        return std::move(*early);
    if (std::optional<ComputeResult> early = DecodeSource(c))
        return std::move(*early);
    if (std::optional<ComputeResult> early = UpscaleSource(c))
        return std::move(*early);
    if (std::optional<ComputeResult> early = RunModels(c))
        return std::move(*early);
    return Product(c);
}

// The texture's class and keys from its probe level; its source level read when a map is to be made.
std::optional<ComputeResult> Session::ReadSource(Compute &c, const std::filesystem::path &textures,
                                                 const std::filesystem::path &file)
{
    std::string &failure = c.failure;
    TxFile &tx = c.tx;
    const int map_size = c.map_size;
    c.candidate = Classify(textures, file);
    const std::optional<Candidate> &candidate = c.candidate;
    // From the level the models' input is box-filtered from (the first no larger than twice the map) on: it and the
    // probe level.
    const auto source_level = [map_size](const TxFile &t) {
        int level = 0;
        while (level + 1 < static_cast<int>(t.levels) &&
               std::max(t.width >> level, t.height >> level) > uint32_t(2 * map_size))
            ++level;
        return level;
    };
    // The keys from the probe level; the models' level only when a map is to be made.
    if (!candidate)
        return Outcome::Skipped;
    if (!tx.LoadProbe(file))
    {
        if (!tx.unreadable)
            return Outcome::Skipped; // not a texture the relief reads (TxFile::Error says why)
        failure = tx.Error();
        LogFailure(candidate->name, failure, false);
        return Outcome::Failed;
    }
    c.key = PreparedKey(*candidate, tx, map_size, c.with_height, c.relief);
    c.gloss_key = GlossKey(*candidate, tx);
    const std::optional<uint64_t> &key = c.key, &gloss_key = c.gloss_key;
    if (!key && !gloss_key)
        return Outcome::Skipped;
    if ((!key || fx::HasReliefMap(c.output, *key)) && (!gloss_key || fx::HasGlossMap(c.output, *gloss_key)))
        return Outcome::Existing;
    const int selected = source_level(tx);
    const size_t source_area = size_t(std::max(1u, tx.width >> selected)) * std::max(1u, tx.height >> selected);
    // Full-image vectors + maximum x4 tile I/O scratch. The networks' own activations are bounded by their tiles.
    if (impl_->compute_admission && !impl_->compute_admission(source_area * 256 + (size_t{32} << 20)))
    {
        failure = "cancelled";
        return Outcome::Failed;
    }
    if (!tx.Load(file, source_level))
    {
        failure = tx.Error(); // its probe read a moment ago: the file changed or is damaged
        LogFailure(candidate->name, failure, false);
        return Outcome::Failed;
    }
    c.read_ms = c.lap();
    return std::nullopt;
}

// The source level decoded to RGBA8, and the device the networks run on.
std::optional<ComputeResult> Session::DecodeSource(Compute &c)
{
    std::string &failure = c.failure;
    const TxFile &tx = c.tx;
    // The source: the first level no larger than twice the map, which the models' inputs are box-filtered down from.
    const int level = tx.first;
    c.level = level;
    const uint32_t lw = std::max(1u, tx.width >> level), lh = std::max(1u, tx.height >> level);
    c.width = static_cast<int>(lw);
    c.height = static_cast<int>(lh);
    std::vector<uint8_t> &rgba = c.rgba;
    rgba.resize(size_t(lw) * lh * 4);
    const std::span<const uint8_t> bytes = tx.Level(level);
    if (bytes.size() < texture::d3d_level_bytes(tx.format, lw, lh) ||
        !texture::decode_rgba8(tx.format, bytes.data(), lw, lh, rgba.data()))
    {
        failure = "unreadable level " + std::to_string(level);
        LogFailure(c.candidate->name, failure, false);
        return Outcome::Failed;
    }
    c.decode_ms = c.lap();
    // The device of this texture: the game's state (device_choice.hpp).
    {
        std::string error;
        if (!impl_->PickDevice(c.use_gpu, error))
        {
            failure = error;
            LogFailure(c.candidate->name, failure, false);
            return Outcome::Failed;
        }
    }
    return std::nullopt;
}

// A texture up to the map's size: its ×2 first (fx::ReliefMapsFromUpscale), the models' input box-filtered from it
// (the ×2 is kept for the game's display too, `display`). The sizes the models run at.
std::optional<ComputeResult> Session::UpscaleSource(Compute &c)
{
    std::string &failure = c.failure;
    const Candidate &candidate = *c.candidate;
    const bool use_gpu = c.use_gpu;
    int &width = c.width, &height = c.height;
    Upscaler &upscaler = use_gpu ? *impl_->gpu_upscaler : *impl_->cpu_upscaler;
    c.from_x2 = fx::ReliefMapsFromUpscale(width, height);
    // The sizes the models run at, halvings of the texture itself: the relief map's (map_size) and the gloss's
    // (fx::kGlossMapSize). Their cost grows with the texels they read, so they never run larger than the map they make.
    fx::NormalMapOptions options;
    options.max_size = c.map_size;
    fx::NormalMapSize(width, height, options, c.mw, c.mh);
    options.max_size = fx::kGlossMapSize;
    fx::NormalMapSize(width, height, options, c.gw, c.gh);
    if (c.from_x2)
    {
        std::vector<uint8_t> x2;
        const fx::UpscaleTarget target = fx::ClassifyUpscale(candidate.name, c.relief, false);
        // The crash guard around the GPU's first texture (backend.hpp MarkGpuRunning): the ×2 is its first network.
        if (use_gpu && impl_->gpu_unproven)
            MarkGpuRunning(impl_->cache, GpuLabel(Gpu()));
        if (!upscaler.Run(c.rgba, width, height, fx::UpscaleWraps(target, c.relief), x2, failure))
        {
            if (impl_->Yielded(use_gpu, candidate.name))
                return Outcome::Yielded;
            if (impl_->cancelled)
                failure = "cancelled";
            LogFailure(candidate.name, "its ×2: " + failure, impl_->cancelled);
            return Outcome::Failed;
        }
        const UpscaleOutput *display = c.display;
        if (display && c.level == 0 && target != fx::UpscaleTarget::None)
            if (const std::optional<uint64_t> x2_key =
                    Key(candidate, c.tx, fx::UpscaleMethod(display->bc), std::max(width, height)))
                if (!fx::HasUpscaled(display->directory, *x2_key))
                {
                    auto permit = fx::SharedCpuAdmission().acquire(1, [&] { return impl_->cancelled.load(); });
                    if (!permit)
                    {
                        failure = "cancelled";
                        return Outcome::Failed;
                    }
                    WriteDisplay(*display, *x2_key, x2, width * 2, height * 2, candidate.name);
                }
        c.rgba = std::move(x2);
        width *= 2;
        height *= 2;
        c.upscale_ms = c.lap();
    }
    return std::nullopt;
}

// PBRify's networks: the normals and the height at the map's size, the roughness (and a height for the gloss's
// grooves) at the gloss's.
std::optional<ComputeResult> Session::RunModels(Compute &c)
{
    std::string &failure = c.failure;
    const Candidate &candidate = *c.candidate;
    const bool use_gpu = c.use_gpu;
    const int width = c.width, height = c.height, mw = c.mw, mh = c.mh, gw = c.gw, gh = c.gh;
    const std::vector<uint8_t> &rgba = c.rgba;
    std::array<std::unique_ptr<Network>, 3> &nets = use_gpu ? impl_->gpu : impl_->cpu;
    double &infer_ms = c.infer_ms;
    std::string &per_model = c.per_model;
    const auto run = [&](Network &model, const std::vector<float> &input, int w, int h) {
        return fx::ReliefModelRun(input, w, h, [&](const float *image, int pw, int ph, float *out) {
            const Clock::time_point infer_start = Clock::now();
            const bool ok = model.Infer(image, pw, ph, out, failure);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - infer_start).count();
            infer_ms += ms;
            fmt::format_to(std::back_inserter(per_model), "{}{} {}x{} {:.0f} ms", per_model.empty() ? "" : ", ",
                           &model == nets[0].get()   ? "normal"
                           : &model == nets[1].get() ? "height"
                                                     : "roughness",
                           pw, ph, ms);
            return ok;
        });
    };
    // The crash guard around the GPU's first texture (backend.hpp MarkGpuRunning).
    if (use_gpu && impl_->gpu_unproven)
        MarkGpuRunning(impl_->cache, GpuLabel(Gpu()));
    // A map without its height (`with_height` off) of a class with one still carries the gloss of a gloss class in its
    // material (the draws take a relief texture's gloss from there, lighting.cpp), made as a gloss map's is.
    const std::optional<uint64_t> &key = c.key;
    c.height_class = key && fx::ReliefHasHeight(c.relief) && c.with_height;
    c.gloss_material =
        key && fx::ReliefHasHeight(c.relief) && !c.with_height && candidate.gloss != fx::GlossClass::None;
    c.gloss_wanted = candidate.gloss != fx::GlossClass::None && (c.height_class || c.gloss_material || c.gloss_key);
    const bool height_class = c.height_class;
    bool failed = false;
    if (key)
    {
        const std::vector<float> input = fx::ReliefModelInput(rgba, width, height, mw, mh);
        c.normal_planes = run(*nets[0], input, mw, mh);
        failed |= c.normal_planes.empty();
        if (height_class && !failed)
        {
            c.height_planes = run(*nets[1], input, mw, mh);
            failed |= c.height_planes.empty();
        }
    }
    if (c.gloss_wanted && !failed)
    {
        const std::vector<float> input = fx::ReliefModelInput(rgba, width, height, gw, gh);
        c.roughness_planes = run(*nets[2], input, gw, gh);
        failed |= c.roughness_planes.empty();
        // The gloss's grooves read the height at its own size: the map's halved, or the height network at the gloss's
        // size when the texture has no relief height.
        if (height_class && !failed)
            c.gloss_height = HalveTo(fx::ReliefModelHeight(c.height_planes, mw, mh), mw, mh, gw, gh);
        else if (!failed)
        {
            const std::vector<float> planes = run(*nets[1], input, gw, gh);
            failed |= planes.empty();
            if (!failed)
                c.gloss_height = fx::ReliefModelHeight(planes, gw, gh);
        }
    }
    impl_->stats.infer_seconds += infer_ms / 1000.0;
    if (use_gpu && impl_->gpu_unproven && !failed)
    {
        ClearGpuRunning(impl_->cache);
        impl_->gpu_unproven = false;
    }
    c.model_ms = c.lap(); // the inputs, padding and crops around the inferences, and the inferences
    if (failed && impl_->Yielded(use_gpu, candidate.name))
        return Outcome::Yielded;
    if (impl_->cancelled)
    {
        // A stopped inference's error varies (and its output, if any, is not the model's).
        failure = "cancelled";
        LogFailure(candidate.name, failure, true);
        return Outcome::Failed;
    }
    if (failed)
    {
        LogFailure(candidate.name, failure, false);
        return Outcome::Failed;
    }
    return std::nullopt;
}

// The computed texture handed to the finalizer.
std::unique_ptr<ComputedTexture> Session::Product(Compute &c)
{
    auto product = std::make_unique<ComputedTexture>();
    product->session = impl_->session;
    product->cancelled = impl_->finalize_cancelled;
    product->name = c.candidate->name;
    product->gloss_class = c.candidate->gloss;
    product->output = c.output;
    product->key = c.key;
    product->gloss_key = c.gloss_key;
    product->relief = c.relief;
    product->width = c.width;
    product->height = c.height;
    product->from_x2 = c.from_x2;
    product->mw = c.mw;
    product->mh = c.mh;
    product->gw = c.gw;
    product->gh = c.gh;
    product->height_class = c.height_class;
    product->gloss_material = c.gloss_material;
    product->gloss_wanted = c.gloss_wanted;
    product->provider = impl_->JobProvider();
    product->rgba = std::move(c.rgba);
    product->normal_planes = std::move(c.normal_planes);
    product->height_planes = std::move(c.height_planes);
    product->gloss_height = std::move(c.gloss_height);
    product->roughness_planes = std::move(c.roughness_planes);
    product->per_model = std::move(c.per_model);
    product->started = c.start;
    product->read_ms = c.read_ms;
    product->decode_ms = c.decode_ms;
    product->upscale_ms = c.upscale_ms;
    product->model_ms = c.model_ms;
    product->infer_ms = c.infer_ms;
    product->computed_at = Clock::now();
    return product;
}

ComputeResult Session::ComputeUpscale(const std::filesystem::path &textures, const std::filesystem::path &file,
                                      const UpscaleOutput &display, int source_size, std::string &failure)
{
    // The ×2 alone (fx/upscale.hpp): a texture whose maps do not come from it (larger than the map) or that has none
    // (the characters). Its class decides the padding and whether it gets one at all.
    const Impl::CpuJob job{*impl_};
    const Clock::time_point start = Clock::now();
    std::error_code error;
    const std::string name =
        fx::NormalizeTexturePath(std::filesystem::relative(file, textures, error).generic_string());
    TxFile tx;
    // From the level the game loaded as its top (0: the file's), the probe level below it.
    if (error)
        return Outcome::Skipped;
    if (!tx.Load(file, [&source_size](const TxFile &t) {
            if (source_size <= 0)
                source_size = static_cast<int>(std::max(t.width, t.height));
            int level = 0;
            while (level + 1 < static_cast<int>(t.levels) &&
                   static_cast<int>(std::max(t.width >> level, t.height >> level)) > source_size)
                ++level;
            return level;
        }))
    {
        if (!tx.unreadable)
            return Outcome::Skipped;
        failure = tx.Error();
        return Outcome::Failed;
    }
    const fx::ReliefVerdict verdict = fx::ClassifyTextureName(name);
    const fx::UpscaleTarget target =
        fx::ClassifyUpscale(name, fx::NameSuggestsHeightMap(name) ? fx::ReliefClass::HeightMap : verdict.relief, false);
    const int level = tx.first;
    const uint32_t lw = std::max(1u, tx.width >> level), lh = std::max(1u, tx.height >> level);
    if (target == fx::UpscaleTarget::None || static_cast<int>(std::max(lw, lh)) != source_size ||
        !fx::UpscaleSize(static_cast<int>(lw), static_cast<int>(lh)))
        return Outcome::Skipped;
    const std::optional<uint64_t> key = Key(Candidate{file, name}, tx, fx::UpscaleMethod(display.bc), source_size);
    if (!key)
        return Outcome::Skipped;
    if (fx::HasUpscaled(display.directory, *key))
        return Outcome::Existing;
    if (impl_->compute_admission && !impl_->compute_admission(size_t(lw) * lh * 256 + (size_t{32} << 20)))
    {
        failure = "cancelled";
        return Outcome::Failed;
    }
    std::vector<uint8_t> rgba(size_t(lw) * lh * 4), x2;
    const std::span<const uint8_t> bytes = tx.Level(level);
    if (bytes.size() < texture::d3d_level_bytes(tx.format, lw, lh) ||
        !texture::decode_rgba8(tx.format, bytes.data(), lw, lh, rgba.data()))
    {
        failure = "unreadable level " + std::to_string(level);
        return Outcome::Failed;
    }
    const double decode_ms = Seconds(start) * 1000.0;
    // The device of this texture: the game's state (device_choice.hpp), as for the relief textures.
    bool use_gpu = false;
    if (!impl_->PickDevice(use_gpu, failure))
        return Outcome::Failed;
    Upscaler &upscaler = use_gpu ? *impl_->gpu_upscaler : *impl_->cpu_upscaler;
    if (use_gpu && impl_->gpu_unproven)
        MarkGpuRunning(impl_->cache, GpuLabel(Gpu()));
    if (!upscaler.Run(rgba, static_cast<int>(lw), static_cast<int>(lh), fx::UpscaleWraps(target, verdict.relief), x2,
                      failure))
    {
        if (impl_->Yielded(use_gpu, name))
            return Outcome::Yielded;
        if (impl_->cancelled)
            failure = "cancelled";
        return Outcome::Failed;
    }
    if (use_gpu && impl_->gpu_unproven)
    {
        ClearGpuRunning(impl_->cache);
        impl_->gpu_unproven = false;
    }
    const double infer_ms = upscaler.infer_ms();
    if (impl_->cancelled)
    {
        failure = "cancelled";
        return Outcome::Failed;
    }
    auto product = std::make_unique<ComputedTexture>();
    product->kind = ComputedTexture::Kind::Upscale;
    product->session = impl_->session;
    product->cancelled = impl_->finalize_cancelled;
    product->name = name;
    product->key = key;
    product->display = display;
    product->target = target;
    product->width = static_cast<int>(lw);
    product->height = static_cast<int>(lh);
    product->rgba = std::move(x2);
    product->started = start;
    product->decode_ms = decode_ms;
    product->infer_ms = infer_ms;
    product->provider = impl_->JobProvider();
    product->computed_at = Clock::now();
    return product;
}

Finalizer::Finalizer() = default;
Finalizer::~Finalizer() = default;

// What Run()'s steps share: the product, the result, the material's CPU permit and the encoder's accounts.
struct Finalizer::Job
{
    ComputedTexture &p;
    FinalizeResult &result;
    std::atomic<bool> &cancelled;
    Clock::time_point finalize_start;
    fx::CpuAdmission::Permit material_permit;
    double encode_ms = 0.0, store_ms = 0.0;
    unsigned encode_threads = 1;

    fx::CpuAdmission::Permit acquire(unsigned count)
    {
        return fx::SharedCpuAdmission().acquire(count, [&] { return cancelled.load(); });
    }
};

FinalizeResult Finalizer::Run(std::unique_ptr<ComputedTexture> product)
{
    FinalizeResult result;
    if (!product || !product->cancelled)
    {
        result.failure = "no computed texture";
        return result;
    }
    auto &p = *product;
    result.session = p.session;
    result.provider = p.provider;
    auto &cancelled = *p.cancelled;
    auto &failure = result.failure;
    const auto finalize_start = Clock::now();
    if (cancelled)
    {
        failure = "cancelled";
        return result;
    }
    Job job{p, result, cancelled, finalize_start};
    job.material_permit = job.acquire(1);
    if (!job.material_permit)
    {
        failure = "cancelled";
        return result;
    }
    if (p.kind == ComputedTexture::Kind::Upscale)
        FinishUpscale(job);
    else
        FinishRelief(job);
    return result;
}

// A ×2: its file for the game's display. WriteDisplay is single-threaded; the material permit is its sole CPU permit.
void Finalizer::FinishUpscale(Job &job)
{
    ComputedTexture &p = job.p;
    FinalizeResult &result = job.result;
    std::string &failure = result.failure;
    result.upscale = true;
    if (job.cancelled || !WriteDisplay(p.display, *p.key, p.rgba, p.width * 2, p.height * 2, p.name))
    {
        failure = job.cancelled ? "cancelled" : "cannot write its file";
        return;
    }
    result.upscale_written = p.key;
    result.outcome = Outcome::Prepared;
    result.total_ms = Seconds(p.started) * 1000.0;
    result.finalize_ms = Seconds(job.finalize_start) * 1000.0;
    render::Log().info("×2 {} ({}): {}x{}, {:.0f} ms (read and decode {:.0f}, inference {:.0f}; finalize {:.0f}; {})",
                       p.name, fx::UpscaleTargetName(p.target), p.width, p.height, result.total_ms, p.decode_ms,
                       p.infer_ms, result.finalize_ms, p.provider);
}

// A relief: the gloss from the roughness, the relief map and the gloss map encoded and written.
void Finalizer::FinishRelief(Job &job)
{
    ComputedTexture &p = job.p;
    FinalizeResult &result = job.result;
    auto mark = job.finalize_start;
    const auto lap = [&mark] {
        const auto now = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
        return ms;
    };
    fx::GlossField gloss;
    if (p.gloss_wanted)
    {
        int cw = p.width, ch = p.height;
        const std::vector<uint8_t> colour = HalveTo(p.rgba, cw, ch, p.gw, p.gh);
        gloss = fx::GlossOfRoughness(fx::ReliefModelHeight(p.roughness_planes, p.gw, p.gh), p.gloss_height, colour,
                                     p.gw, p.gh, p.gloss_class);
    }
    const double gloss_ms = lap();
    fx::NormalMap map;
    if (p.key && !WriteRelief(job, gloss, map))
        return;
    if (p.gloss_key && !WriteGloss(job, gloss))
        return;
    const double write_ms = lap();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - p.started).count();
    result.total_ms = ms;
    result.finalize_ms = std::chrono::duration<double, std::milli>(Clock::now() - job.finalize_start).count();
    double mean_gloss = 0.0;
    for (const float g : gloss.gloss)
        mean_gloss += g;
    mean_gloss = gloss.gloss.empty() ? 0.0 : mean_gloss / static_cast<double>(gloss.gloss.size());
    render::Log().info(
        "relief {}: {}x{}{}{}, gloss {} {}x{} (mean {:.2f}), {:.0f} ms (read {:.0f}, decode {:.0f}, ×2 "
        "{:.0f}, models {:.0f} of which inference {:.0f}: {}; gloss {:.0f}, mips and material {:.0f}, EAC "
        "{:.0f} on {} threads, write {:.0f}; depth {:.1f} texels, parallax trust {:.0f}%; {})",
        p.name, p.key ? p.mw : 0, p.key ? p.mh : 0,
        !p.key           ? " (no relief)"
        : p.height_class ? ""
                         : " without its height",
        p.from_x2 ? " from its ×2" : "", fx::GlossClassName(p.gloss_class), gloss.width, gloss.height, mean_gloss, ms,
        p.read_ms, p.decode_ms, p.upscale_ms, p.model_ms, p.infer_ms, p.per_model, gloss_ms,
        std::max(0.0, write_ms - job.encode_ms - job.store_ms), job.encode_ms, job.encode_threads, job.store_ms,
        static_cast<double>(map.depth), 100.0 * static_cast<double>(map.trust_coverage), p.provider);
    result.outcome = Outcome::Prepared;
}

// EAC RG11 of `texels` (a map's of `source`'s size) on a team of encoder threads under their CPU permits.
bool Finalizer::Encode(Job &job, const fx::NormalMap &source, const std::vector<uint8_t> &texels,
                       std::vector<uint8_t> &encoded)
{
    const auto begin = Clock::now();
    // Release the one-thread material permit before acquiring the encoder team: never nested.
    job.material_permit.reset();
    const unsigned wanted = source.width > 512 || source.height > 512 ||
                                    size_t(texture::blocks_x(source.width)) * texture::blocks_x(source.height) < 256
                                ? 1u
                                : 4u;
    const auto permit = job.acquire(wanted); // the team the game's CPU share gives now
    if (permit && (!encoder_ || encoder_->limit() != *permit))
        encoder_ = std::make_unique<texture::EacEncoder>(*permit);
    const bool ok = static_cast<bool>(permit) && !texels.empty() && source.width > 0 && source.height > 0 &&
                    source.mip_count > 0 &&
                    encoder_->Encode(texture::EacFormat::Rg11, texels.data(), source.width, source.height,
                                     source.mip_count, encoded, job.cancelled);
    job.encode_ms += std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    job.encode_threads = std::max(job.encode_threads, encoder_->participants());
    if (!ok)
    {
        job.result.failure = job.cancelled ? "cancelled" : "invalid EAC input";
        LogFailure(job.p.name, job.result.failure, job.cancelled);
    }
    return ok;
}

// The relief map: the normals with the material (the height and the gloss, or the gloss only), encoded and written.
bool Finalizer::WriteRelief(Job &job, const fx::GlossField &gloss, fx::NormalMap &map)
{
    ComputedTexture &p = job.p;
    std::string &failure = job.result.failure;
    const int mw = p.mw, mh = p.mh, gw = p.gw, gh = p.gh;
    const auto &name = p.name;
    fx::NormalMap rg8 = fx::NormalMapOfNormals(fx::ReliefModelNormals(p.normal_planes, mw, mh), mw, mh);
    if (p.height_class)
    {
        // The material's G: the gloss stretched over the map (empty: no gloss class, G 0).
        const std::vector<uint8_t> g =
            gloss.gloss.empty() ? std::vector<uint8_t>{} : Stretch(gloss.gloss, gw, gh, mw, mh);
        // The parallax trust reads the colour at the map's size.
        int aw = p.width, ah = p.height;
        const std::vector<uint8_t> albedo = HalveTo(p.rgba, aw, ah, mw, mh);
        if (aw != mw || ah != mh ||
            !fx::AddReliefMaterial(rg8, p.relief, fx::ReliefModelHeight(p.height_planes, mw, mh), g, albedo))
            render::Log().info("relief {}: flat, no height", name);
    }
    else if (p.gloss_material && !fx::AddGlossMaterial(rg8, Stretch(gloss.gloss, gw, gh, mw, mh)))
        render::Log().error("relief {}: its gloss ({}x{}) does not fit the map {}x{}: no material", name, gw, gh, mw,
                            mh);
    map = {rg8.width, rg8.height, rg8.mip_count, fx::NormalMapFormat::EacRg11, {}};
    if (!Encode(job, rg8, rg8.rg, map.rg))
        return false;
    if (!rg8.material.empty())
    {
        if (!Encode(job, rg8, rg8.material, map.material))
            return false;
        map.depth = rg8.depth;
        map.trust_coverage = rg8.trust_coverage;
    }
    if (job.cancelled)
    {
        failure = "cancelled";
        return false;
    }
    const auto store_start = Clock::now();
    const bool written = fx::WriteReliefMap(p.output, *p.key, map);
    job.store_ms += std::chrono::duration<double, std::milli>(Clock::now() - store_start).count();
    if (!written)
    {
        failure = "cannot write " + fx::ReliefFile(p.output, *p.key).string();
        LogFailure(name, failure, false);
        return false;
    }
    job.result.relief_written = p.key;
    return true;
}

// The texture's own gloss map (R the metal mask, G the gloss), encoded and written.
bool Finalizer::WriteGloss(Job &job, const fx::GlossField &gloss)
{
    ComputedTexture &p = job.p;
    std::string &failure = job.result.failure;
    if (!job.material_permit)
        job.material_permit = job.acquire(1);
    if (!job.material_permit)
    {
        failure = "cancelled";
        return false;
    }
    const fx::NormalMap rg = fx::GlossMaterial(gloss);
    fx::NormalMap stored{rg.width, rg.height, rg.mip_count, fx::NormalMapFormat::EacRg11, {}};
    if (!Encode(job, rg, rg.rg, stored.rg))
        return false;
    if (job.cancelled)
    {
        failure = "cancelled";
        return false;
    }
    const auto store_start = Clock::now();
    const bool written = fx::WriteGlossMap(p.output, *p.gloss_key, stored);
    job.store_ms += std::chrono::duration<double, std::milli>(Clock::now() - store_start).count();
    if (!written)
    {
        failure = "cannot write " + fx::GlossFile(p.output, *p.gloss_key).string();
        render::Log().error("no gloss map for {}: {}", p.name, failure);
        return false;
    }
    job.result.gloss_written = p.gloss_key;
    return true;
}

void Session::SetComputeAdmission(std::function<bool(size_t)> admission)
{
    impl_->compute_admission = std::move(admission);
}

void Session::AccountFinalized(const FinalizeResult &result)
{
    if (result.session != impl_->session || result.outcome != Outcome::Prepared || result.upscale)
        return;
    ++impl_->stats.prepared;
    impl_->stats.max_texture_ms = std::max(impl_->stats.max_texture_ms, result.total_ms);
}

Outcome Session::Finalize(ComputeResult &&computed, std::string &failure)
{
    if (!computed.product)
        return computed.outcome;
    if (!impl_->synchronous_finalizer)
        impl_->synchronous_finalizer = std::make_unique<Finalizer>();
    auto result = impl_->synchronous_finalizer->Run(std::move(computed.product));
    AccountFinalized(result);
    failure = std::move(result.failure);
    return result.outcome;
}
Outcome Session::PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                             const std::filesystem::path &output, int map_size, bool height, std::string &failure,
                             const UpscaleOutput *display)
{
    return Finalize(ComputeFile(textures, file, output, map_size, height, failure, display), failure);
}
Outcome Session::Upscale(const std::filesystem::path &textures, const std::filesystem::path &file,
                         const UpscaleOutput &display, int source_size, std::string &failure)
{
    return Finalize(ComputeUpscale(textures, file, display, source_size, failure), failure);
}

} // namespace storm::relief
