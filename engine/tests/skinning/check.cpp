#include "skinning.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include <catch2/catch.hpp>

using namespace storm::skinning;

struct alignas(16) Bone
{
    float matrix[16];
};

static void fail(const char *what, size_t vertex)
{
    FAIL(what << " at vertex " << vertex);
}

// Heap blocks of exactly count elements: ASan reports any read or write past the last vertex.
template <typename T> static std::unique_ptr<T[]> exact(size_t count)
{
    return std::unique_ptr<T[]>(new T[count]);
}

static void fill(Source *src, size_t count, Bone *bones, size_t boneCount, unsigned seed, bool extremes)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f), big(-1.0e6f, 1.0e6f), weight(0.0f, 1.0f);
    // clang-format off
    const float specials[] = {0.0f, -0.0f, 1.0f, -1.0f, std::numeric_limits<float>::min(),
                              std::numeric_limits<float>::denorm_min(), 3.0e38f, -3.0e38f,
                              std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    // clang-format on
    const auto value = [&](std::uniform_real_distribution<float> &d) {
        if (extremes && rng() % 5 == 0)
            return specials[rng() % (sizeof(specials) / sizeof(specials[0]))];
        return d(rng);
    };
    for (size_t b = 0; b < boneCount; ++b)
        for (float &m : bones[b].matrix)
            m = value(rng() % 4 ? unit : big);
    for (size_t i = 0; i < count; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            src[i].pos[k] = value(big);
            src[i].nrm[k] = value(unit);
        }
        src[i].weight = extremes && rng() % 7 == 0 ? (rng() % 2 ? 0.0f : 1.0f) : weight(rng);
        // Upper 16 bits are unused by the engine and must be ignored.
        src[i].boneid = (rng() % boneCount) | ((rng() % boneCount) << 8) | (rng() & 0xffff0000u);
        src[i].color = static_cast<int32_t>(rng());
        src[i].tu0 = unit(rng);
        src[i].tv0 = unit(rng);
    }
}

// Bitwise equality, except that any NaN equals any NaN: which payload/sign a
// NaN carries after NaN*x+NaN is not specified by IEEE 754 across ISAs.
static bool same(float a, float b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b);
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

static void compare(const Target *a, const Target *b, size_t count, const char *what)
{
    for (size_t i = 0; i < count; ++i)
    {
        for (int k = 0; k < 3; ++k)
            if (!same(a[i].pos[k], b[i].pos[k]) || !same(a[i].nrm[k], b[i].nrm[k]))
                fail(what, i);
        if (a[i].color != b[i].color || !same(a[i].tu, b[i].tu) || !same(a[i].tv, b[i].tv))
            fail(what, i);
    }
}

static void parity(size_t count, unsigned seed, bool extremes)
{
    constexpr size_t boneCount = 256;
    auto bones = exact<Bone>(boneCount);
    auto src = exact<Source>(count);
    auto scalar = exact<Target>(count), simd = exact<Target>(count);
    fill(src.get(), count, bones.get(), boneCount, seed, extremes);
    std::memset(static_cast<void *>(scalar.get()), 0xa5, count * sizeof(Target));
    std::memset(static_cast<void *>(simd.get()), 0x5a, count * sizeof(Target));
    skinScalar(src.get(), scalar.get(), bones.get(), sizeof(Bone), count);
#if defined(STORM_SKINNING_NEON) || defined(STORM_SKINNING_SSE2)
    skinSimd(src.get(), simd.get(), bones.get(), sizeof(Bone), count);
    compare(scalar.get(), simd.get(), count, "scalar vs SIMD");
#endif
    skin(src.get(), simd.get(), bones.get(), sizeof(Bone), count);
    compare(scalar.get(), simd.get(), count, "scalar vs dispatch");
}

TEST_CASE("skinning")
{
    // Hand-checked vertex: identity blended with a translation, weight 0.25.
    Bone bones[2] = {};
    for (int i = 0; i < 16; i += 5)
        bones[0].matrix[i] = bones[1].matrix[i] = 1.0f;
    bones[1].matrix[12] = 8.0f;
    bones[1].matrix[13] = 4.0f;
    Source in{{1.0f, 2.0f, 3.0f}, 0.25f, 0x0100u, {0.0f, 1.0f, 0.0f}, 0x11223344, 0.5f, 0.75f};
    Target out{};
    skin(&in, &out, bones, sizeof(Bone), 1);
    // M = I with translation (6, 3, 0); x row negated: x' = -(1 + 6), y' = 2 + 3, z' = 3.
    if (out.pos[0] != -7.0f || out.pos[1] != 5.0f || out.pos[2] != 3.0f)
        fail("hand-checked position", 0);
    if (out.nrm[0] != -6.0f || out.nrm[1] != 4.0f || out.nrm[2] != 0.0f)
        fail("hand-checked normal (translation is applied to normals, as before)", 0);
    if (out.color != 0x11223344 || out.tu != 0.5f || out.tv != 0.75f)
        fail("attributes", 0);

    for (size_t count : {size_t(1), size_t(2), size_t(3), size_t(7), size_t(64), size_t(1000), size_t(20000)})
        for (unsigned seed = 1; seed <= 5; ++seed)
        {
            parity(count, seed * 977u + static_cast<unsigned>(count), false);
            parity(count, seed * 131u + static_cast<unsigned>(count), true);
        }
}
