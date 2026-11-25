#pragma once

#include "storm/render/log.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace storm::relief
{
// How the networks' images run in tiles (RunTiles), and the CPU networks' tile sizes. The CPU networks' memory bound
// (backend.hpp kCpuNetworkBytes) decides their tiles: ncnn's blobs and workspace of one
// run grow with its padded texels (measured: 2.3..2.6 KB a texel with fp32 storage on arm64 and x86-64, at tiles of
// 128..256 texels and 2..8 threads), so the largest tile within the bound follows from the bytes a texel the network
// measured. The model, padding and output stay unchanged; only ncnn's rounding differs between tile sizes.
inline constexpr int kMinCpuTile = 32;  // below it the halo is most of the work: the texture is skipped, logged
inline constexpr int kMaxCpuTile = 256; // above it a tile saves little halo work
// The bytes a texel before a graph has run on this process's CPU (then its own measurement): above every one measured.
inline constexpr double kInitialCpuBytesPerTexel = 2.75 * 1024.0;
// Headroom over the measured bytes a texel (ncnn picks its kernels by the blob's size; more threads, more workspace).
inline constexpr double kCpuTileMargin = 1.15;

// The largest inner tile (a multiple of 8) whose padded run fits `limit` bytes at `bytes_per_texel`; 0 when not even
// kMinCpuTile does.
inline int BoundedCpuTile(double bytes_per_texel, size_t limit, int halo)
{
    const double side = std::sqrt(static_cast<double>(limit) / (bytes_per_texel * kCpuTileMargin));
    const int inner = (static_cast<int>(side) - 2 * halo) / 8 * 8;
    return inner < kMinCpuTile ? 0 : inner;
}

// `extent` texels in equal tiles of at most `ceiling` (no thin tile at the end that is mostly halo), a multiple of 8.
inline int BalancedTile(int extent, int ceiling)
{
    if (extent <= ceiling)
        return extent;
    const int count = (extent + ceiling - 1) / ceiling;
    return std::min(ceiling, ((extent + count - 1) / count + 7) / 8 * 8);
}

// The CPU network's error when ncnn could not allocate (its layers return -100, or the input did not fit).
inline constexpr const char *kCpuOutOfMemory = "ncnn: CPU inference out of memory";

inline bool CpuAllocationFailure(std::string_view error)
{
    // Only an allocation failure is retried at a smaller tile; unknown execution/shape/model errors are not hidden.
    return error == kCpuOutOfMemory;
}

inline int SmallerCpuTile(int current, int width, int height, int halo)
{
    const int largest_inner = std::max(width - 2 * halo, height - 2 * halo);
    // A small image may already be smaller than the configured tile. The retry must actually reduce its shape.
    const int effective = current > 0 ? std::min(current, largest_inner) : largest_inner;
    if (effective <= kMinCpuTile)
        return 0;
    const int candidate = std::min(kMaxCpuTile, std::max(kMinCpuTile, effective / 2 / 32 * 32));
    return candidate < effective ? candidate : 0;
}

// Runs `image` (planar, width x height, the outer `halo` texels its padding) in tiles of `tile` inner texels (0 or a
// tile covering the inner area: one run), each with its halo; `infer` gets a tile's planes and their size and writes
// `scale` times as many. Their inner texels go into `planes` (the outer halo of the output stays unset); `cancelled`
// stops between tiles.
inline bool RunTiles(const float *image, int width, int height, float *planes, int halo, int scale, int tile,
                     const std::atomic<bool> &cancelled, std::string &error,
                     const std::function<bool(const float *, int, int, float *)> &infer)
{
    const int inner_w = width - 2 * halo, inner_h = height - 2 * halo;
    if (inner_w <= 0 || inner_h <= 0)
    {
        error = "image smaller than its halo";
        return false;
    }
    const size_t in_plane = static_cast<size_t>(width) * height;
    const int out_w = width * scale;
    const size_t out_plane = static_cast<size_t>(out_w) * height * scale;
    std::vector<float> in, out;
    for (int y0 = 0; y0 < inner_h;)
    {
        const int th = std::min(tile > 0 ? tile : inner_h, inner_h - y0);
        for (int x0 = 0; x0 < inner_w;)
        {
            if (cancelled)
            {
                error = "cancelled";
                return false;
            }
            const int tw = std::min(tile > 0 ? tile : inner_w, inner_w - x0);
            // The tile with its halo, from the padded image (the halo always lies inside it).
            const int iw = tw + 2 * halo, ih = th + 2 * halo;
            in.resize(static_cast<size_t>(iw) * ih * 3);
            out.resize(in.size() * scale * scale);
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < ih; ++y)
                    std::memcpy(in.data() + (static_cast<size_t>(c) * ih + y) * iw,
                                image + c * in_plane + static_cast<size_t>(y0 + y) * width + x0,
                                static_cast<size_t>(iw) * sizeof(float));
            if (!infer(in.data(), iw, ih, out.data()))
                return false;
            // The tile's inner texels into the output (its halo's are the neighbours' or the border's).
            const int sh = halo * scale, ow = iw * scale, oh = ih * scale;
            for (int c = 0; c < 3; ++c)
                for (int y = 0; y < th * scale; ++y)
                    std::memcpy(planes + c * out_plane + static_cast<size_t>((y0 + halo) * scale + y) * out_w +
                                    static_cast<size_t>((x0 + halo) * scale),
                                out.data() + (static_cast<size_t>(c) * oh + sh + y) * ow + sh,
                                static_cast<size_t>(tw * scale) * sizeof(float));
            x0 += tw;
        }
        y0 += th;
    }
    return true;
}

// A CPU network's inference of one image (`run` one whole image or tile): in one run when it fits the network's
// ceiling (`ceiling()`: its tile within the memory bound, 0 when none fits: `over_bound()` is the error), else in
// equal tiles. An allocation failure restarts the image at a strictly smaller tile (SmallerCpuTile), down to its
// smallest; that tile stays in `learned_tile` for the network's next images.
inline bool InferCpuTiles(const float *image, int width, int height, float *planes, int halo, int scale,
                          int &learned_tile, const std::atomic<bool> &cancelled, std::string &error,
                          const std::function<int()> &ceiling, const std::function<std::string()> &over_bound,
                          const std::function<bool(const float *, int, int, float *, std::string &)> &run)
{
    for (;;)
    {
        if (cancelled)
        {
            error = "cancelled";
            return false;
        }
        const int bound = ceiling();
        if (bound == 0)
        {
            error = over_bound();
            return false;
        }
        const int tile = BalancedTile(std::max(width, height) - 2 * halo, bound);
        bool allocation_failure = false;
        try
        {
            const bool ok =
                width - 2 * halo <= tile && height - 2 * halo <= tile
                    ? run(image, width, height, planes, error)
                    : RunTiles(image, width, height, planes, halo, scale, tile, cancelled, error,
                               [&](const float *in, int w, int h, float *out) { return run(in, w, h, out, error); });
            if (ok)
            {
                if (cancelled)
                {
                    error = "cancelled";
                    return false;
                }
                return true;
            }
            allocation_failure = CpuAllocationFailure(error);
        }
        catch (const std::bad_alloc &)
        {
            error = "CPU inference tile scratch allocation failed";
            allocation_failure = true;
        }
        if (cancelled || !allocation_failure)
            return false;
        const int next = SmallerCpuTile(bound, width, height, halo);
        if (!next)
            return false;
        render::Log().info("relief CPU allocation failed at tile {} of {}x{} (scale {}): again at {}", tile, width,
                           height, scale, next);
        learned_tile = next;
        error.clear();
    }
}
} // namespace storm::relief
