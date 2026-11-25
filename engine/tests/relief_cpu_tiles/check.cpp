// The CPU networks' tiling (src/libs/relief/cpu_tiles.hpp) with a stencil in place of ncnn: tiled output equals one
// run's, allocation failures restart at smaller tiles, other failures and cancels are never retried.
#include "cpu_tiles.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace storm::relief;

namespace
{
// A network that runs a local stencil (reads the halo, catches layout errors, at either output scale) and fails on
// request the way ncnn does.
struct Network
{
    Network(int halo_texels, int output_scale, int start_tile)
        : halo(halo_texels), scale(output_scale), tile(start_tile)
    {
    }
    int halo, scale, tile;
    std::atomic<bool> cancelled{false};
    int ceiling = 1 << 30; // the memory bound's tile
    int calls = 0, fail_at = 0, failures = 0, fail_larger = 0;
    bool unknown = false, cancel_on_failure = false, cancel_on_success = false, scratch = false;
    std::vector<std::pair<int, int>> shapes;

    bool Run(const float *in, int w, int h, float *out, std::string &error)
    {
        ++calls;
        shapes.emplace_back(w, h);
        if ((fail_at && calls == fail_at) || (fail_larger && std::max(w - 2 * halo, h - 2 * halo) > fail_larger))
        {
            ++failures;
            if (cancel_on_failure)
                cancelled = true;
            if (scratch)
                throw std::bad_alloc();
            error = unknown ? "ncnn: CPU inference failed (status -1)" : kCpuOutOfMemory;
            return false;
        }
        const int ow = w * scale, oh = h * scale;
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < oh; ++y)
                for (int x = 0; x < ow; ++x)
                {
                    const int ix = x / scale, iy = y / scale;
                    const auto at = [&](int xx, int yy) {
                        return in[(size_t(c) * h + std::clamp(yy, 0, h - 1)) * w + std::clamp(xx, 0, w - 1)];
                    };
                    out[(size_t(c) * oh + y) * ow + x] =
                        at(ix, iy) * .5f + (at(ix - 1, iy) + at(ix + 1, iy) + at(ix, iy - 1) + at(ix, iy + 1)) * .125f;
                }
        if (cancel_on_success)
            cancelled = true;
        return true;
    }

    bool Infer(const float *image, int w, int h, float *planes, std::string &error)
    {
        return InferCpuTiles(
            image, w, h, planes, halo, scale, tile, cancelled, error,
            [this] { return tile > 0 ? std::min(tile, ceiling) : ceiling; }, [] { return std::string("over"); },
            [this](const float *in, int iw, int ih, float *out, std::string &e) { return Run(in, iw, ih, out, e); });
    }
};

std::vector<float> Image(int w, int h)
{
    std::vector<float> v(size_t(w) * h * 3);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = float((i * 29) % 251) / 251;
    return v;
}

// The inner texels (the outer halo of a tiled output stays unset) are equal.
bool EqualInner(const std::vector<float> &a, const std::vector<float> &b, int w, int h, int halo, int scale)
{
    for (int c = 0; c < 3; ++c)
        for (int y = halo * scale; y < (h - halo) * scale; ++y)
            for (int x = halo * scale; x < (w - halo) * scale; ++x)
            {
                const size_t i = (size_t(c) * h * scale + y) * w * scale + x;
                if (a[i] != b[i])
                    return false;
            }
    return true;
}
} // namespace

TEST_CASE("tile sizes")
{
    // 2.5 KB a texel in 160 MB with 24 texels of halo; 30 KB a texel in 64 MB fits no tile.
    CHECK(BoundedCpuTile(2.5 * 1024, size_t(160) << 20, 24) == 184);
    CHECK(BoundedCpuTile(30 * 1024, size_t(64) << 20, 24) == 0);
    CHECK(BalancedTile(1024, 176) == 176);
    CHECK(BalancedTile(512, 240) == 176);
    CHECK(BalancedTile(100, 256) == 100);
    CHECK(SmallerCpuTile(0, 1072, 560, 24) == 256);
    CHECK(SmallerCpuTile(256, 112, 96, 24) == 32);
    CHECK(SmallerCpuTile(32, 80, 80, 24) == 0);
}

TEST_CASE("an allocation failure restarts the whole image at a smaller tile")
{
    for (int scale : {1, 4})
    {
        CAPTURE(scale);
        const int w = 349, h = 237, halo = 24;
        const auto input = Image(w, h);
        std::vector<float> whole_out(size_t(w) * h * 3 * scale * scale, -7), out(whole_out.size(), -9);
        std::string error;
        Network whole(halo, scale, 0);
        REQUIRE(whole.Infer(input.data(), w, h, whole_out.data(), error));
        REQUIRE(whole.calls == 1);

        Network retry(halo, scale, 0);
        retry.fail_larger = 64;
        REQUIRE(retry.Infer(input.data(), w, h, out.data(), error));
        CHECK(error.empty());
        CHECK(retry.tile == 64);
        CHECK(retry.failures > 0);
        CHECK(retry.failures <= 4);
        CHECK(EqualInner(whole_out, out, w, h, halo, scale));
        // The learned tile is kept: the next image runs without a failure.
        const int failures = retry.failures;
        REQUIRE(retry.Infer(input.data(), w, h, out.data(), error));
        CHECK(retry.failures == failures);

        // A late tile's failure restarts every tile: no texel of the failed attempt stays.
        Network partial(halo, scale, 128);
        partial.fail_at = 2;
        std::fill(out.begin(), out.end(), -9.0f);
        REQUIRE(partial.Infer(input.data(), w, h, out.data(), error));
        CHECK(EqualInner(whole_out, out, w, h, halo, scale));
    }
}

TEST_CASE("only allocation failures are retried, cancels stop")
{
    const int w = 160, h = 144, halo = 24;
    const auto in = Image(w, h);
    std::vector<float> out(in.size());
    std::string error;

    Network unknown(halo, 1, 0);
    unknown.fail_at = 1;
    unknown.unknown = true;
    CHECK_FALSE(unknown.Infer(in.data(), w, h, out.data(), error));
    CHECK(unknown.calls == 1);

    Network cancelled(halo, 1, 0);
    cancelled.fail_at = 1;
    cancelled.cancel_on_failure = true;
    CHECK_FALSE(cancelled.Infer(in.data(), w, h, out.data(), error));
    CHECK(cancelled.calls == 1);
    CHECK(cancelled.tile == 0); // a cancelled image teaches the network no tile

    Network racing(halo, 1, 0);
    racing.cancel_on_success = true;
    CHECK_FALSE(racing.Infer(in.data(), w, h, out.data(), error));
    CHECK(error == "cancelled");

    Network stopped(halo, 1, 0);
    stopped.cancelled = true;
    CHECK_FALSE(stopped.Infer(in.data(), w, h, out.data(), error));
    CHECK(stopped.calls == 0);

    Network exhausted(halo, 1, 256);
    exhausted.fail_larger = 1;
    CHECK_FALSE(exhausted.Infer(in.data(), w, h, out.data(), error));
    CHECK(exhausted.calls <= 5);
    CHECK(exhausted.tile == 32);
    CHECK_FALSE(error.empty());

    Network scratch(halo, 1, 0);
    scratch.fail_at = 1;
    scratch.scratch = true;
    CHECK(scratch.Infer(in.data(), w, h, out.data(), error));
    CHECK(scratch.failures == 1);
}

TEST_CASE("the memory bound's tile")
{
    const int w = 160, h = 144, halo = 24;
    const auto in = Image(w, h);
    std::vector<float> out(in.size());
    std::string error;
    // An image no tile fits: refused at once, never run.
    Network over(halo, 1, 0);
    over.ceiling = 0;
    CHECK_FALSE(over.Infer(in.data(), w, h, out.data(), error));
    CHECK(over.calls == 0);
    CHECK(error == "over");
    // Every tile within the bound.
    Network bounded(halo, 1, 0);
    bounded.ceiling = 40;
    REQUIRE(bounded.Infer(in.data(), w, h, out.data(), error));
    for (const auto &[sw, sh] : bounded.shapes)
    {
        CHECK(sw - 2 * halo <= 40);
        CHECK(sh - 2 * halo <= 40);
    }
}
