// Where the relief networks run: ncnn on the GPU through Vulkan compute or on the CPU, both running the same graphs
// (fp16 weights, made by pnnx of the float models). One interface for both, for the relief maps (prepare.cpp) and the
// texture upscale.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace storm::relief
{
// One network on one device. Not thread-safe (Cancel aside).
class Network
{
  public:
    virtual ~Network() = default;
    // One image: three colour planes (planar, `width` x `height`, 0..1) in, three planes of `scale` times the size out
    // (`scale`: the network's own, 1 for the relief networks). A GPU network leaves the outer `halo` texels of the
    // output (the input's padding: what the network needs around a texel) unset. False on an error or after Cancel.
    virtual bool Infer(const float *image, int width, int height, float *planes, std::string &error) = 0;
    // Any thread: the running inference stops as soon as it can, and every later one.
    virtual void Cancel() = 0;
    // Worker only, CPU networks: `acquire()` is called before each run (a tile) and returns a permit for *permit
    // threads, the run's thread count, held for the run, or null, which fails the run (fx::CpuAdmission::acquire).
    // GPU networks ignore it.
    virtual void SetCpuAdmission(std::function<std::shared_ptr<const unsigned>()>)
    {
    }
    // Worker only, GPU networks: while `yield()` is true (the player sees the game's scene: device_choice.hpp) the
    // running inference stops at its next submission with the error "yielded"; unlike Cancel the network stays usable.
    // CPU networks ignore it.
    virtual void SetYield(std::function<bool()>)
    {
    }
    // "CPU (ncnn), 8 cores, ..." / "GPU Adreno (TM) 740 (Vulkan, ncnn), tiles of 176 + 24 texels".
    virtual std::string Describe() const = 0;
    // The largest inner tile it runs at once (the CPU's: within kCpuNetworkBytes); a caller that tiles its image itself
    // (the upscaler) stays within it, so the network does not cut its tiles again.
    virtual int TileCeiling() const
    {
        return 1 << 30;
    }
};

// The memory of a session's CPU networks (ncnn's blobs and workspace; they run one after the other): never more than
// this held at once, all of it returned to the system after each texture (CpuRuntime::EndJob). It fits the 4 GB minimum
// phone, where the game itself takes 0.6..0.9 GB: the tiles are sized from it (cpu_tiles.hpp), and an allocation past
// it fails the run (the tile is lowered, logged); a texture that does not fit at the smallest tile is not made
// (logged).
inline constexpr size_t kCpuNetworkBytes = size_t{160} << 20;

// The CPU side of ncnn for the networks of one session (they run one after the other): its thread count and tile size.
class CpuRuntime
{
  public:
    // `threads` cores for a run without a permit (ncnn's simpleomp pool, waiting threads sleeping); a run under a
    // permit takes the permit's threads. `tile`: the networks' largest inner tile, -1 cpu_tiles.hpp kMaxCpuTile (the
    // checks set one; 0 none): each run's tile is the largest within it and the memory bound. Null and `error` on
    // failure. Logged.
    static std::unique_ptr<CpuRuntime> Open(int threads, std::string &error, int tile = -1);
    virtual ~CpuRuntime() = default;
    // An ncnn graph (.ncnn.param text, .ncnn.bin weights; both must outlive the network) on the CPU, its padding
    // `halo` texels, its output `scale` times the input; the image runs in tiles with the halo around each.
    virtual std::unique_ptr<Network> Load(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo,
                                          int scale, std::string &error) = 0;
    // The most bytes its networks held at once since the last EndJob (at most kCpuNetworkBytes).
    virtual size_t JobPeak() const = 0;
    // The fewest and the most threads a run of its networks had since the last EndJob ({0, 0}: none ran).
    virtual std::pair<unsigned, unsigned> JobThreads() const = 0;
    // After each texture: every byte its networks kept for reuse goes back to the system.
    virtual void EndJob() = 0;
};

// The Vulkan compute device ncnn would use (its default one): created on first use, kept for the process.
struct GpuDevice
{
    bool available = false;
    std::string name, driver; // the device's name and driver version
    std::string why;          // when not available
};
const GpuDevice &Gpu();
// Closes the Vulkan instance when no GPU network is alive (the generator's idle release and shutdown, the system's
// low memory); the next Gpu() opens it again. True when it was closed now. It must be closed
// before the process exits: ncnn's own exit handler closes it after the driver's exit handlers have run, and crashes.
bool ReleaseGpu();

// An ncnn graph (.ncnn.param text, .ncnn.bin weights; both must outlive the network) on the GPU. The image is run in
// tiles of the inner area with `halo` texels around each (the network's reach: the tiles join without a seam), each
// tile one short GPU submission, so the game's frames are not held up. `scale`: the network's output scale. `yield`:
// SetYield's, from the calibration on (a yielded calibration fails the load).
std::unique_ptr<Network> LoadGpuNetwork(std::span<const uint8_t> param, std::span<const uint8_t> bin, int halo,
                                        int scale, std::function<bool()> yield, std::string &error);

// The crash guard of the GPU work, on storm/fx/gpu_crash_guard.hpp: `cache`'s marker holds GpuLabel of the GPU
// (kGpuOpening while Vulkan opens) while GPU work runs. One left at the next start, unless the system ended the process
// (fx::PreviousExit), sends that GPU and driver to the CPU until «Очистить кэш». kGpuPath versions the label: a crash
// recorded by another version of the GPU path or another driver is not this one's.
// GpuPossible: whether a session may use the GPU: the marker left by the previous run read, Vulkan opened under the
// guard unless it crashed while opening, no crash recorded for this GPU and driver. Logged.
bool GpuPossible(const std::filesystem::path &cache);
inline constexpr const char *kGpuPath = "relief GPU path 2 (staged submissions)";
inline constexpr const char *kGpuOpening = "opening Vulkan, relief GPU path 2 (staged submissions)";
std::string GpuLabel(const GpuDevice &gpu); // "<name>, driver <version>, <kGpuPath>"
void MarkGpuRunning(const std::filesystem::path &cache, const std::string &what);
void ClearGpuRunning(const std::filesystem::path &cache);
// The GPU (GpuLabel) or kGpuOpening that crashed a previous run, or empty.
std::string GpuCrash(const std::filesystem::path &cache);

} // namespace storm::relief
