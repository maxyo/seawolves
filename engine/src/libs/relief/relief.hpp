// Relief maps from PBRify's models (fx/relief_model.hpp) through ncnn, made from the player's game files into the
// relief directory the game reads (fx/relief_store.hpp): the maps its current scene draws, while it runs. See
// README.md.
#pragma once

#include "device_choice.hpp"
#include "storm/fx/relief_store.hpp"
#include "storm/fx/upscale.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::relief
{
struct LoadingRetry;
struct ComputedTexture;
struct ComputeResult;
struct FinalizeResult;

enum class Outcome
{
    Prepared,
    Existing, // its map is there already
    Skipped,  // no prepared map for it: unknown format, too small, a grey height map (the game derives it), no rule
    Failed,   // logged with the reason
    // stopped on the GPU because the player sees the game's scene (Session::SetGame): to be made again, on the CPU
    Yielded,
};

// The model files: ncnn graphs, the same on the GPU and the CPU. All are required.
struct Models
{
    std::span<const uint8_t> normal_param, normal_bin;       // pbrify-normal.ncnn.param, .bin
    std::span<const uint8_t> height_param, height_bin;       // pbrify-height.ncnn.param, .bin
    std::span<const uint8_t> roughness_param, roughness_bin; // pbrify-roughness.ncnn.param, .bin
    std::span<const uint8_t> upscale_param, upscale_bin;     // pbrify-upscale.ncnn.param, .bin (fx/upscale.hpp)
};

// Where the game's ×2 textures go (fx/upscale.hpp: the upscale directory) and the GPU tier they are encoded for.
struct UpscaleOutput
{
    std::filesystem::path directory;
    bool bc = false; // BC1/BC3 (GPUs with S3TC), else ETC2
};
// The model files' names, in Models' order.
inline constexpr const char *kModelFiles[] = {
    "pbrify-normal.ncnn.param",    "pbrify-normal.ncnn.bin",    "pbrify-height.ncnn.param",  "pbrify-height.ncnn.bin",
    "pbrify-roughness.ncnn.param", "pbrify-roughness.ncnn.bin", "pbrify-upscale.ncnn.param", "pbrify-upscale.ncnn.bin"};

// The models loaded once for many textures, ncnn on the GPU or the CPU: each texture's device follows the game's state
// (SetGame, device_choice.hpp). One thread at a time (Cancel aside).
class Session
{
  public:
    struct Stats
    {
        int prepared = 0;
        double infer_seconds = 0.0, max_texture_ms = 0.0;
        std::string provider;
        bool gpu = false;                             // the last texture's networks ran on the GPU (else on the CPU)
        DeviceReason device_why = DeviceReason::None; // why the last texture took its device
        bool gpu_possible = false;                    // a GPU the session may use (its graphs, no crash recorded)
        size_t cpu_memory_peak =
            0; // the last texture's CPU networks' memory peak, bytes (backend.hpp kCpuNetworkBytes)
    };
    // `threads`: the most threads a CPU network's tile runs on (a tile under fx::CpuAdmission gets its permit's); 0 =
    // every core. `cache`: where the GPU crash guard keeps its record (the cache root's relief directory). Null and
    // `error` on failure.
    static std::unique_ptr<Session> Open(const Models &models, int threads, const std::filesystem::path &cache,
                                         std::string &error);
    ~Session();
    // One texture file under `textures` (the game's RESOURCE/Textures; the key does not depend on where the game
    // lies), unless its map is there already. A texture up to the map's size is upscaled first and its maps are made
    // from its ×2 (fx::ReliefMapsFromUpscale); with `display` that ×2 is also kept for the game (fx/upscale.hpp).
    // `height`: the map with PBRify's height (fx::ReliefMethod); without it the height network does not run at the
    // map's size (only at the gloss's, for a gloss class's gloss).
    Outcome PrepareFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                        const std::filesystem::path &output, int map_size, bool height, std::string &failure,
                        const UpscaleOutput *display = nullptr);
    // The game's ×2 of one texture file (fx::ClassifyUpscale by its path: Skipped for a texture that gets none), made
    // from its level whose larger side is `source_size` (the texture as the game loaded it; 0 its top level), unless it
    // is there already.
    Outcome Upscale(const std::filesystem::path &textures, const std::filesystem::path &file,
                    const UpscaleOutput &display, int source_size, std::string &failure);
    // Compute owner only. A non-null product is pending CPU finalization, never Stored/Prepared.
    // Owns every buffer crossing the handoff; the finalizer never accesses Session or its networks.
    ComputeResult ComputeFile(const std::filesystem::path &textures, const std::filesystem::path &file,
                              const std::filesystem::path &output, int map_size, bool height, std::string &failure,
                              const UpscaleOutput *display = nullptr);
    ComputeResult ComputeUpscale(const std::filesystem::path &textures, const std::filesystem::path &file,
                                 const UpscaleOutput &display, int source_size, std::string &failure);
    void AccountFinalized(const FinalizeResult &result); // compute owner, originating session only
    // Worker only; before decode/model work, with a conservative workspace estimate (the networks' own memory aside).
    // False cancels this job. The callback holds no network mutex or admission permits.
    void SetComputeAdmission(std::function<bool(size_t)> admission);
    // Any thread (the game's shutdown): the texture being prepared fails at once, and every later one.
    void Cancel();
    // Worker only, before the first texture: the game's state, which decides each texture's device
    // (device_choice.hpp). `scene_shown`: the player sees the game's scene (no texture starts on the GPU, and one on it
    // stops at its next submission: Outcome::Yielded); `gpu_free`: the game leaves the GPU free. Both are called on the
    // worker thread. Without them a texture runs on the GPU only in a loading window (SetLoadingRetry).
    void SetGame(std::function<bool()> gpu_free, std::function<bool()> scene_shown);
    // Worker only: one scoped job, the place's loading window (loading_retry.hpp).
    void SetLoadingRetry(std::shared_ptr<LoadingRetry> retry);
    const Stats &stats() const;

  private:
    struct Impl;
    explicit Session(std::unique_ptr<Impl> impl);
    // PrepareFile and Upscale: a computed product finalized on this thread.
    Outcome Finalize(ComputeResult &&computed, std::string &failure);
    // The steps of ComputeFile (prepare.cpp); a result is the texture's outcome before its product.
    struct Compute;
    std::optional<ComputeResult> ReadSource(Compute &c, const std::filesystem::path &textures,
                                            const std::filesystem::path &file);
    std::optional<ComputeResult> DecodeSource(Compute &c);
    std::optional<ComputeResult> UpscaleSource(Compute &c);
    std::optional<ComputeResult> RunModels(Compute &c);
    std::unique_ptr<ComputedTexture> Product(Compute &c);
    std::unique_ptr<Impl> impl_;
};

} // namespace storm::relief
