#include "backend.hpp"
#include "cpu_tiles.hpp"
#include "storm/render/log.hpp"

#include "relief.hpp"
#include "storm/fx/gpu_crash_guard.hpp"

#include <command.h>
#include <cpu.h>
#include <gpu.h>
#include <layer.h>
#include <datareader.h>
#include <net.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace storm::relief
{
namespace
{
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

// Loads an ncnn graph: the param text (copied: ncnn reads it up to its end) and the weights through a reader bounded by
// their size (a short or damaged file fails instead of reading past it; the weights must be consumed to the byte).
bool LoadGraph(ncnn::Net &net, std::string &param_text, std::span<const uint8_t> param, std::span<const uint8_t> bin)
{
    param_text.assign(reinterpret_cast<const char *>(param.data()), param.size());
    const unsigned char *weights = bin.data();
    const ncnn::DataReaderFromMemory reader(weights, bin.size());
    return net.load_param_mem(param_text.c_str()) == 0 && net.load_model(reader) == 0 &&
           weights == bin.data() + bin.size();
}

// The memory of a session's CPU networks (ncnn::Allocator, the library's hook for its blobs and workspace; ncnn's own
// per-network pools keep every block they ever held until the network goes: README "Memory on the CPU"). Freed blocks
// are kept for the next layers and tiles of the same texture (a new block costs its page faults: twice the inference
// time), never more than `limit` bytes held at once (kept blocks go back to the system first; past it the allocation
// fails, ncnn's -100), and all go back after each texture (Release).
class CpuMemory final : public ncnn::Allocator
{
  public:
    explicit CpuMemory(size_t limit) : limit_(limit)
    {
    }
    ~CpuMemory() override
    {
        Release();
    }

    void *fastMalloc(size_t size) override
    {
        std::lock_guard lock(mutex_);
        // The smallest kept block that fits and is at most a third larger: a big block taken by a small blob would make
        // the next big one a new block.
        auto best = kept_.end();
        for (auto it = kept_.begin(); it != kept_.end(); ++it)
            if (it->first >= size && it->first - size <= it->first / 4 &&
                (best == kept_.end() || it->first < best->first))
                best = it;
        if (best != kept_.end())
        {
            void *ptr = best->second;
            Lend(ptr, best->first);
            kept_.erase(best);
            return ptr;
        }
        while (held_ + size > limit_ && !kept_.empty())
        {
            const auto largest = std::max_element(kept_.begin(), kept_.end());
            ncnn::fastFree(largest->second);
            held_ -= largest->first;
            kept_.erase(largest);
        }
        if (held_ + size > limit_)
        {
            refused_ = std::max(refused_, live_ + size); // what the run needed at least
            return nullptr;
        }
        void *ptr = ncnn::fastMalloc(size);
        if (!ptr)
            return nullptr;
        held_ += size;
        held_peak_ = std::max(held_peak_, held_);
        Lend(ptr, size);
        return ptr;
    }

    void fastFree(void *ptr) override
    {
        if (!ptr)
            return;
        std::lock_guard lock(mutex_);
        const auto it = lent_.find(ptr);
        if (it == lent_.end())
        {
            // ncnn returned a block this allocator never lent: its accounting (and the heap) can no longer be trusted.
            render::Fatal("relief CPU allocator: freeing {}, which it did not allocate", ptr);
        }
        kept_.emplace_back(it->second, ptr);
        live_ -= it->second;
        lent_.erase(it);
    }

    // Before a run: its peak of blocks in use and what a refused allocation needed start again.
    void StartRun()
    {
        std::lock_guard lock(mutex_);
        live_peak_ = live_;
        refused_ = 0;
    }
    // After a run: the most bytes in use at once, and at least what it needed when an allocation was refused (0: none).
    std::pair<size_t, size_t> Run() const
    {
        std::lock_guard lock(mutex_);
        return {live_peak_, refused_};
    }
    size_t HeldPeak() const
    {
        std::lock_guard lock(mutex_);
        return held_peak_;
    }
    // Between textures (no run going): every kept block back to the system.
    void Release()
    {
        std::lock_guard lock(mutex_);
        for (const auto &[size, ptr] : kept_)
            ncnn::fastFree(ptr);
        kept_.clear();
        held_ = live_;
        held_peak_ = held_;
    }

  private:
    void Lend(void *ptr, size_t size)
    {
        lent_.emplace(ptr, size);
        live_ += size;
        live_peak_ = std::max(live_peak_, live_);
    }

    mutable std::mutex mutex_;
    const size_t limit_;
    std::vector<std::pair<size_t, void *>> kept_;
    std::unordered_map<void *, size_t> lent_;
    size_t held_ = 0, held_peak_ = 0, live_ = 0, live_peak_ = 0, refused_ = 0;
};

// The bytes a texel each graph needed on this process's CPU (CpuMemory's peak of a run over its padded texels), for
// every session after the run that measured it: by the graph's .param text (its layers and blob sizes; the three relief
// networks share one).
class CpuCosts
{
  public:
    static double Get(const std::string &graph)
    {
        std::lock_guard lock(mutex());
        const auto it = costs().find(graph);
        return it == costs().end() ? kInitialCpuBytesPerTexel : it->second;
    }
    // True when it is the graph's first measurement or a larger one.
    static bool Raise(const std::string &graph, double bytes_per_texel)
    {
        std::lock_guard lock(mutex());
        const auto [it, added] = costs().emplace(graph, bytes_per_texel);
        if (!added && it->second >= bytes_per_texel)
            return false;
        it->second = bytes_per_texel;
        return true;
    }

  private:
    static std::mutex &mutex()
    {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<std::string, double> &costs()
    {
        static std::unordered_map<std::string, double> c;
        return c;
    }
};

// The fewest and the most threads the runs of a runtime's networks had since the last EndJob.
struct RunThreads
{
    std::atomic<unsigned> fewest{0}, most{0};
    void Ran(unsigned threads)
    {
        const unsigned was = fewest.load();
        fewest = was ? std::min(was, threads) : threads;
        most = std::max(most.load(), threads);
    }
};

// One ncnn graph on the CPU (the same fp16-weight graph the GPU runs; ncnn's NEON / fp16 kernels on arm64), its
// threads ncnn's simpleomp pool (cmake/ncnn.cmake: waiting threads sleep), its memory the runtime's CpuMemory. The
// image runs in equal tiles, the largest within the memory bound by the graph's measured bytes a texel (cpu_tiles.hpp),
// so a Cancel also stops between tiles.
class CpuNetwork final : public Network
{
  public:
    CpuNetwork(int threads, std::shared_ptr<RunThreads> ran, std::shared_ptr<CpuMemory> memory, int halo, int scale,
               int tile)
        : threads_(threads), ran_(std::move(ran)), memory_(std::move(memory)), halo_(halo), scale_(scale), tile_(tile)
    {
        net_.opt.use_vulkan_compute = false;
        net_.opt.num_threads = threads_;
        net_.opt.lightmode = true;                 // intermediate blobs freed as soon as they are consumed
        net_.opt.use_local_pool_allocator = false; // CpuMemory instead, set on each extractor
    }

    bool Open(std::span<const uint8_t> param, std::span<const uint8_t> bin, std::string &error)
    {
        if (!LoadGraph(net_, param_, param, bin))
        {
            error = "ncnn could not load the network for the CPU (a damaged graph?)";
            return false;
        }
        return true;
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
    {
        return InferCpuTiles(
            image, width, height, planes, halo_, scale_, tile_, cancelled_, error, [this] { return TileCeiling(); },
            [this] { return OverBound(); },
            [this](const float *in, int w, int h, float *out, std::string &e) { return Run(in, w, h, out, e); });
    }

    void SetCpuAdmission(std::function<std::shared_ptr<const unsigned>()> acquire) override
    {
        cpu_admission_ = std::move(acquire);
    }

    // One whole image (its output `scale_` times the size), on the threads its permit gives.
    bool Run(const float *image, int width, int height, float *planes, std::string &error)
    {
        const auto permit = cpu_admission_ ? cpu_admission_() : nullptr;
        if (cpu_admission_ && !permit)
        {
            error = "CPU admission cancelled";
            return false;
        }
        ncnn::Mat in(width, height, 3);
        if (in.empty())
        {
            error = kCpuOutOfMemory;
            return false;
        }
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < height; ++y)
                std::memcpy(in.channel(c).row(y), image + (static_cast<size_t>(c) * height + y) * width,
                            static_cast<size_t>(width) * sizeof(float));
        ncnn::Mat out;
        net_.opt.num_threads = permit ? static_cast<int>(*permit) : threads_; // the extractor's own, no reload
        ran_->Ran(static_cast<unsigned>(net_.opt.num_threads));
        memory_->StartRun();
        int status = -1;
        {
            ncnn::Extractor ex = net_.create_extractor();
            ex.set_blob_allocator(memory_.get());
            ex.set_workspace_allocator(memory_.get());
            status = ex.input("in0", in) != 0 ? -1 : ex.extract("out0", out);
        }
        Measured(width, height);
        if (status == -100)
        {
            error = kCpuOutOfMemory;
            return false;
        } // ncnn's "allocation failed" from any layer
        const int ow = width * scale_, oh = height * scale_;
        if (status != 0 || out.c != 3 || out.w != ow || out.h != oh)
        {
            error = "ncnn: CPU inference failed (status " + std::to_string(status) + ")";
            return false;
        }
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < oh; ++y)
                std::memcpy(planes + (static_cast<size_t>(c) * oh + y) * ow, out.channel(c).row(y),
                            static_cast<size_t>(ow) * sizeof(float));
        return true;
    }

    void Cancel() override
    {
        cancelled_ = true;
    }

    int TileCeiling() const override
    {
        const int bound = BoundedCpuTile(CpuCosts::Get(param_), kCpuNetworkBytes, halo_);
        return tile_ > 0 ? std::min(tile_, bound) : bound;
    }

    std::string Describe() const override
    {
        return "CPU (ncnn), " + std::to_string(std::thread::hardware_concurrency()) + " cores, tiles of up to " +
               std::to_string(TileCeiling()) + " + " + std::to_string(halo_) + " texels within " +
               std::to_string(kCpuNetworkBytes >> 20) + " MB";
    }

  private:
    // A run's bytes a texel, from runs of at least 128 x 128 texels (smaller ones carry ncnn's fixed buffers per texel)
    // or a refused one (what it needed at least): the graph's tiles from then on.
    void Measured(int width, int height)
    {
        const auto [peak, refused] = memory_->Run();
        const double texels = static_cast<double>(width) * height;
        if (refused == 0 && texels < 128.0 * 128.0)
            return;
        const double cost = static_cast<double>(std::max(peak, refused)) / texels;
        if (CpuCosts::Raise(param_, cost))
            render::Log().info(
                "relief CPU network (scale {}): {:.2f} KB a texel measured at {}x{}{}: tiles of up to {} + "
                "{} texels within {} MB",
                scale_, cost / 1024.0, width, height, refused ? " (an allocation past the bound refused)" : "",
                TileCeiling(), halo_, kCpuNetworkBytes >> 20);
    }
    std::string OverBound() const
    {
        return fmt::format(
            "the CPU network needs {:.2f} KB a texel: not even a {}-texel tile fits the {} MB memory bound",
            CpuCosts::Get(param_) / 1024.0, kMinCpuTile, kCpuNetworkBytes >> 20);
    }

    std::function<std::shared_ptr<const unsigned>()> cpu_admission_;
    int threads_;
    std::shared_ptr<RunThreads> ran_;
    std::shared_ptr<CpuMemory> memory_;
    int halo_, scale_, tile_;
    std::atomic<bool> cancelled_{false};
    std::string param_;
    ncnn::Net net_;
};

class NcnnCpuRuntime final : public CpuRuntime
{
  public:
    NcnnCpuRuntime(int threads, int tile)
        : threads_(threads), ran_(std::make_shared<RunThreads>()),
          memory_(std::make_shared<CpuMemory>(kCpuNetworkBytes)), tile_(tile)
    {
        // fp16 weights either way; the arithmetic in fp16 where the CPU has it (ARMv8.2 FP16), else in fp32.
#if defined(__aarch64__)
        const char *arithmetic = ncnn::cpu_support_arm_asimdhp() ? "fp16 (ARMv8.2 FP16)" : "fp32 (no ARMv8.2 FP16)";
#else
        const char *arithmetic = "fp32";
#endif
        render::Log().info(
            "relief CPU runtime: ncnn, at most {} threads a tile, {} arithmetic: the networks' memory within {} MB, "
            "tiles of up to {} texels by its bytes a texel",
            threads, arithmetic, kCpuNetworkBytes >> 20, tile_ ? std::to_string(tile_) : "any");
    }

    std::unique_ptr<Network> Load(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo, int scale,
                                  std::string &error) override
    {
        auto network = std::make_unique<CpuNetwork>(threads_, ran_, memory_, halo, scale, tile_);
        if (!network->Open(param, bin, error))
            return nullptr;
        return network;
    }

    size_t JobPeak() const override
    {
        return memory_->HeldPeak();
    }
    std::pair<unsigned, unsigned> JobThreads() const override
    {
        return {ran_->fewest.load(), ran_->most.load()};
    }
    void EndJob() override
    {
        memory_->Release();
        ran_->fewest = ran_->most = 0;
    }

  private:
    int threads_;
    std::shared_ptr<RunThreads> ran_;   // shared with its networks
    std::shared_ptr<CpuMemory> memory_; // shared with its networks: they run one after the other
    int tile_;
};

// The GPU: the image runs in tiles of the inner area, and each tile's graph in stages, one submission per stage, so a
// frame of the game waits at most for one stage (kSubmitMs of GPU work) while the tiles stay large (the halo around a
// small tile is mostly recomputed work: at 32 + 24 texels the GPU computes 6 texels per texel kept). A stage ends at a
// convolution's output; ncnn keeps the blobs the later stages need on the GPU between the submissions (no transfer in
// between: the first stage uploads the tile, the last downloads its result). The calibration (Calibrate) measures, per
// graph and GPU, a submission's fixed wall time (recording, the fence wait), the graph's GPU work per texel and each
// convolution's part of it; the tile is the one with the least wall time per inner texel whose largest convolution
// fits kSubmitMs (kMinTile at least, kMaxTile at most), its stages the fewest whose work fits kSubmitMs each.
constexpr int kMinTile = 32, kMaxTile = 512, kProbeSmall = 64, kProbeLarge = 256;
constexpr double kSubmitMs = 6.0;

struct Calibration
{
    double fixed_ms = 0.0;     // a submission's fixed wall time
    double ms_per_texel = 0.0; // the whole graph's GPU work per texel of a tile with its halo
    std::vector<double> share; // the part of that work of each segment (up to each cut, then up to the output)
    double max_share = 1.0;
    int tile = kMinTile;
};

std::atomic<int> gpu_networks{0}; // alive: ReleaseGpu keeps the instance while any is

class GpuNetwork final : public Network
{
  public:
    GpuNetwork(int halo, int scale, std::string gpu) : halo_(halo), scale_(scale), gpu_(std::move(gpu))
    {
        ++gpu_networks;
        net_.opt.use_vulkan_compute = true;
        net_.opt.num_threads = 1;
        net_.opt.lightmode = true;
    }

    ~GpuNetwork() override
    {
        --gpu_networks;
    }

    // The precision ncnn runs this graph in on this GPU (its options after loading: what the device supports).
    std::string Precision() const
    {
        const ncnn::Option &o = net_.opt;
        return std::string("fp16 storage ") + (o.use_fp16_storage ? "on" : "off") + ", packed " +
               (o.use_fp16_packed ? "on" : "off") + ", arithmetic " + (o.use_fp16_arithmetic ? "on" : "off") +
               " (fp16 weights)";
    }

    bool Open(std::span<const uint8_t> param, std::span<const uint8_t> bin, std::string &error)
    {
        if (!LoadGraph(net_, param_, param, bin) || net_.input_indexes().size() != 1 ||
            net_.output_indexes().size() != 1)
        {
            error = "ncnn could not load the network for the GPU (a damaged graph?)";
            return false;
        }
        input_ = net_.input_indexes()[0];
        output_ = net_.output_indexes()[0];
        // The cuts between stages: every convolution's output but the graph's own, in graph order.
        for (const ncnn::Layer *layer : net_.layers())
            if (layer->type == "Convolution" && layer->tops.size() == 1 && layer->tops[0] != output_)
                cuts_.push_back(layer->tops[0]);
        return Calibrate(error);
    }

    static double Texels(int inner, int halo)
    {
        return static_cast<double>(inner + 2 * halo) * (inner + 2 * halo);
    }

    // The submissions' ends for a tile: the cuts after which the work so far would exceed kSubmitMs (the output ends
    // the last).
    std::vector<int> Stages(const Calibration &c, int tile) const
    {
        const double work = c.ms_per_texel * Texels(tile, halo_);
        std::vector<int> ends;
        double stage = 0.0;
        for (size_t i = 0; i < c.share.size(); ++i)
        {
            if (i > 0 && stage > 0.0 && stage + c.share[i] * work > kSubmitMs)
            {
                ends.push_back(cuts_[i - 1]);
                stage = 0.0;
            }
            stage += c.share[i] * work;
        }
        return ends;
    }

    // One tile (planar input with its halo) through the graph: its upload, one submission per stage (`ends`, then the
    // output), its download. `walls`: each stage's wall time (the transfers' are left out: their GPU work is a copy,
    // their wall time mostly the CPU's staging copies and conversions, which hold no frame of the game).
    bool RunStages(const ncnn::Mat &in, const std::vector<int> &ends, ncnn::Mat &out, std::vector<double> &walls)
    {
        walls.clear();
        const ncnn::VulkanDevice *device = net_.vulkan_device();
        ncnn::VkAllocator *blobs = device->acquire_blob_allocator();
        ncnn::VkAllocator *staging = device->acquire_staging_allocator();
        bool ok = true;
        {
            ncnn::Extractor ex = net_.create_extractor();
            ex.set_blob_vkallocator(blobs);
            ex.set_workspace_vkallocator(blobs);
            ex.set_staging_vkallocator(staging);
            const auto submit = [&](int blob, bool timed) {
                if (cancelled_ || Yielded())
                    return false;
                const Clock::time_point start = Clock::now();
                ncnn::VkCompute cmd(device);
                ncnn::VkMat mat;
                if (ex.extract(blob, mat, cmd) != 0 || cmd.submit_and_wait() != 0)
                    return false;
                if (timed)
                    walls.push_back(Ms(start));
                return true;
            };
            ok = ex.input(input_, in) == 0 && submit(input_, false);
            for (size_t i = 0; ok && i < ends.size(); ++i)
                ok = submit(ends[i], true);
            ok = ok && submit(output_, true) && ex.extract(output_, out) == 0;
        }
        device->reclaim_blob_allocator(blobs);
        device->reclaim_staging_allocator(staging);
        return ok;
    }

    // The calibration of this graph on this GPU, measured once per graph (the relief's three networks share one): the
    // graph with a submission at every cut at two tile sizes, each stage the faster of two runs after a warm-up (the
    // GPU's allocations). A segment's work is its growth between the sizes (the fixed part cancels); the fixed part is
    // what is left of the small runs.
    bool Calibrate(std::string &error)
    {
        static std::mutex mutex;
        static std::vector<std::pair<std::string, Calibration>> known;
        const std::string key = param_ + "|" + std::to_string(halo_) + "|" + std::to_string(scale_);
        {
            std::lock_guard lock(mutex);
            for (const auto &[graph, calibration] : known)
                if (graph == key)
                {
                    calibration_ = calibration;
                    tile_ = calibration.tile;
                    return true;
                }
        }
        const auto run = [&](int inner, std::vector<double> &best) {
            const int size = inner + 2 * halo_;
            ncnn::Mat in(size, size, 3);
            in.fill(0.5f);
            best.assign(cuts_.size() + 1, 1e30);
            std::vector<double> walls;
            for (int i = 0; i < 3; ++i)
            {
                ncnn::Mat out;
                if (!RunStages(in, cuts_, out, walls) || walls.size() != best.size())
                {
                    error = "ncnn could not run the network";
                    return false;
                }
                if (i > 0)
                    for (size_t s = 0; s < walls.size(); ++s)
                        best[s] = std::min(best[s], walls[s]);
            }
            return true;
        };
        std::vector<double> small, large;
        if (!run(kProbeSmall, small) || !run(kProbeLarge, large))
            return false;
        const double grown = Texels(kProbeLarge, halo_) - Texels(kProbeSmall, halo_);
        Calibration c;
        double work = 0.0, fixed = 0.0;
        for (size_t s = 0; s < small.size(); ++s)
        {
            c.share.push_back(std::max(large[s] - small[s], 0.0));
            work += c.share.back();
        }
        if (work <= 0.0)
            c.share.assign(small.size(), 1.0), work = static_cast<double>(small.size()); // no growth seen: equal parts
        c.ms_per_texel = std::max(work / grown, 1e-9);
        c.max_share = 0.0;
        for (double &share : c.share)
        {
            share /= work;
            c.max_share = std::max(c.max_share, share);
        }
        for (size_t s = 0; s < small.size(); ++s)
            fixed += std::max(small[s] - c.share[s] * c.ms_per_texel * Texels(kProbeSmall, halo_), 0.0);
        c.fixed_ms = fixed / static_cast<double>(small.size());
        // The tile of the least wall time per inner texel whose largest segment fits a submission.
        double best = 1e30;
        for (int tile = kMinTile; tile <= kMaxTile; tile += 32)
        {
            const double graph = c.ms_per_texel * Texels(tile, halo_);
            if (tile > kMinTile && c.max_share * graph > kSubmitMs)
                break;
            const double cost = (graph + static_cast<double>(Stages(c, tile).size() + 1) * c.fixed_ms) /
                                (static_cast<double>(tile) * tile);
            if (cost <= best)
                best = cost, c.tile = tile;
        }
        const size_t stages = Stages(c, c.tile).size() + 1;
        render::Log().info("relief GPU tiles: {} + {} texels in {} submissions (graph {:.1f} ms of GPU work a tile, at "
                           "most {:.1f} ms a submission, {:.1f} ms fixed a submission; {} cuts measured at {} and {} "
                           "px: {:.1f} and {:.1f} ms in all)",
                           c.tile, halo_, stages, c.ms_per_texel * Texels(c.tile, halo_),
                           c.max_share * c.ms_per_texel * Texels(c.tile, halo_), c.fixed_ms, cuts_.size(), kProbeSmall,
                           kProbeLarge, Sum(small), Sum(large));
        calibration_ = c;
        tile_ = c.tile;
        std::lock_guard lock(mutex);
        known.emplace_back(key, c);
        return true;
    }

    static double Sum(const std::vector<double> &values)
    {
        double sum = 0.0;
        for (const double v : values)
            sum += v;
        return sum;
    }

    bool Infer(const float *image, int width, int height, float *planes, std::string &error) override
    {
        const auto infer = [&](const float *in, int iw, int ih, float *out) {
            ncnn::Mat tile(iw, ih, 3);
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < ih; ++y)
                    std::memcpy(tile.channel(c).row(y), in + (static_cast<size_t>(c) * ih + y) * iw,
                                static_cast<size_t>(iw) * sizeof(float));
            ncnn::Mat result;
            // The stages of this tile's own size (a tile at the image's edge is smaller: fewer, shorter stages).
            std::vector<double> walls;
            if (!RunStages(tile, Stages(calibration_, std::max(iw, ih) - 2 * halo_), result, walls) || result.c != 3 ||
                result.w != iw * scale_ || result.h != ih * scale_)
            {
                error = cancelled_ ? "cancelled" : Yielded() ? "yielded" : "ncnn inference failed";
                return false;
            }
            const int ow = iw * scale_, oh = ih * scale_;
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < oh; ++y)
                    std::memcpy(out + (static_cast<size_t>(c) * oh + y) * ow, result.channel(c).row(y),
                                static_cast<size_t>(ow) * sizeof(float));
            return true;
        };
        return RunTiles(image, width, height, planes, halo_, scale_, tile_, cancelled_, error, infer);
    }

    void Cancel() override
    {
        cancelled_ = true;
    }

    void SetYield(std::function<bool()> yield) override
    {
        yield_ = std::move(yield);
    }
    bool Yielded() const
    {
        return yield_ && yield_();
    }

    std::string Describe() const override
    {
        return "GPU " + gpu_ + " (Vulkan, ncnn), tiles of " + std::to_string(tile_) + " + " + std::to_string(halo_) +
               " texels";
    }

  private:
    int halo_, scale_;
    std::string gpu_;
    int tile_ = kMinTile;
    std::function<bool()> yield_;
    Calibration calibration_;
    std::string param_;
    ncnn::Net net_;
    int input_ = -1, output_ = -1;
    std::vector<int> cuts_; // the blobs a stage may end at (Open)
    std::atomic<bool> cancelled_{false};
};

} // namespace

std::unique_ptr<CpuRuntime> CpuRuntime::Open(int threads, std::string &error, int tile)
{
    if (threads < 1)
    {
        error = "no CPU threads for the relief networks";
        return nullptr;
    }
    return std::make_unique<NcnnCpuRuntime>(threads, tile < 0 ? kMaxCpuTile : tile);
}

namespace
{
std::mutex gpu_mutex;
GpuDevice gpu_device;
bool gpu_open = false;
} // namespace

const GpuDevice &Gpu()
{
    std::lock_guard lock(gpu_mutex);
    if (!gpu_open)
    {
        gpu_open = true;
        gpu_device = {};
        if (ncnn::create_gpu_instance() != 0 || ncnn::get_gpu_count() <= 0)
        {
            gpu_device.why = "no Vulkan compute device (ncnn found none: no Vulkan driver, or no queue with compute)";
            return gpu_device;
        }
        const ncnn::GpuInfo &info = ncnn::get_gpu_info(ncnn::get_default_gpu_index());
        gpu_device.available = true;
        gpu_device.name = info.device_name();
        const uint32_t v = info.driver_version();
        gpu_device.driver =
            std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
    }
    return gpu_device;
}

bool ReleaseGpu()
{
    std::lock_guard lock(gpu_mutex);
    if (!gpu_open || gpu_networks > 0)
        return false;
    if (gpu_device.available)
        ncnn::destroy_gpu_instance();
    gpu_open = false;
    return true;
}

std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo,
                                        int scale, std::function<bool()> yield, std::string &error)
{
    if (!Gpu().available)
    {
        error = Gpu().why;
        return nullptr;
    }
    const Clock::time_point start = Clock::now();
    render::Log().info("relief GPU model: opening/calibrating scale {}, halo {}", scale, halo);
    auto network = std::make_unique<GpuNetwork>(halo, scale, Gpu().name);
    network->SetYield(std::move(yield)); // the calibration's submissions too
    if (!network->Open(param, bin, error))
    {
        if (network->Yielded())
            render::Log().info("relief GPU model: scale {} stopped after {:.0f} ms: the scene shows", scale, Ms(start));
        else
            render::Log().error("relief GPU model: scale {} failed after {:.0f} ms: {}", scale, Ms(start), error);
        return nullptr;
    }
    render::Log().info("relief GPU model: scale {} ready after {:.0f} ms; {}", scale, Ms(start), network->Precision());
    return network;
}

namespace
{
// The relief's files of the shared guard: relief-gpu-running.txt, relief-gpu-crashed.txt.
fx::GpuCrashGuard Guard(const std::filesystem::path &cache)
{
    return fx::GpuCrashGuard(cache, "relief-gpu");
}
} // namespace

std::string GpuLabel(const GpuDevice &gpu)
{
    return gpu.name + ", driver " + gpu.driver + ", " + kGpuPath;
}

void MarkGpuRunning(const std::filesystem::path &cache, const std::string &what)
{
    Guard(cache).mark(what);
}

void ClearGpuRunning(const std::filesystem::path &cache)
{
    Guard(cache).clear();
}

std::string GpuCrash(const std::filesystem::path &cache)
{
    return Guard(cache).crashed().what;
}

bool GpuPossible(const std::filesystem::path &cache)
{
    // The system's word on how the previous process ended (the app writes it into the cache root, the relief
    // directory's parent): a marker left by a process the system killed for memory or stopped is not a crash.
    if (const fx::GpuCrashGuard::Record left = Guard(cache).take_leftover(fx::PreviousExit(cache.parent_path()));
        !left.empty() && !left.counted)
        render::Log().info("relief GPU work was running when the previous run ended, but {}: the GPU stays possible",
                           left.why);
    else if (!left.empty())
    {
        if (left.what == kGpuOpening)
            render::Log().error("relief GPU inference crashed the previous run while Vulkan opened: using the CPU");
        else
            render::Log().error("relief GPU inference crashed the previous run on {}: using the CPU", left.what);
    }
    const std::string crash = GpuCrash(cache);
    if (crash == kGpuOpening)
    {
        render::Log().warn("relief device: CPU (Vulkan crashed a previous run while it opened)");
        return false;
    }
    MarkGpuRunning(cache, kGpuOpening);
    const GpuDevice &gpu = Gpu();
    ClearGpuRunning(cache);
    if (!crash.empty())
    {
        if (crash == GpuLabel(gpu))
        {
            render::Log().warn("relief device: CPU (GPU inference crashed a previous run on {})", crash);
            return false;
        }
        Guard(cache).forget(); // another GPU or driver since
    }
    if (!gpu.available)
    {
        render::Log().info("relief device: CPU ({})", gpu.why);
        return false;
    }
    return true;
}

} // namespace storm::relief
