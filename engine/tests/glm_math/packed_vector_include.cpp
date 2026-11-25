// Deliberately the first include: geometry headers include this under pack(1).
#pragma pack(push, 1)
#include "storm_glm.h"
struct PackedVectors
{
    char prefix;
    glm::vec3 f;
    glm::dvec3 d;
    glm::mat4 m;
    glm::quat q;
};
struct PackingSentinel
{
    char prefix;
    double value;
};
#pragma pack(pop)

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <catch2/catch.hpp>

// GLM is defined with the native packing whatever the includer's pack state is...
static_assert(alignof(glm::vec3) == alignof(float) && sizeof(glm::vec3) == 12);
static_assert(alignof(glm::dvec3) == alignof(double) && sizeof(glm::dvec3) == 24);
static_assert(alignof(glm::mat4) == alignof(float) && sizeof(glm::mat4) == 64);
static_assert(alignof(storm::math::ProjectedPoint) == alignof(float) && sizeof(storm::math::ProjectedPoint) == 16);
// ...while the includer's own structures keep their packing afterwards.
static_assert(offsetof(PackedVectors, f) == 1 && offsetof(PackedVectors, d) == 13 && offsetof(PackedVectors, m) == 37 &&
              offsetof(PackedVectors, q) == 101);
static_assert(sizeof(PackedVectors) == 117 && alignof(PackedVectors) == 1);
static_assert(alignof(PackingSentinel) == 1 && sizeof(PackingSentinel) == 9);
static_assert(alignof(glm::vec4) == alignof(float) && sizeof(glm::vec4) == 16);
static_assert(alignof(glm::quat) == alignof(float) && sizeof(glm::quat) == 16);
static_assert(alignof(std::max_align_t) >= alignof(long double));

// GLM types keep their natural alignment, so a GLM member of a packed
// structure may sit at an odd address. Constructing it there or binding a
// reference to it (which every GLM operator does) is undefined behaviour; such
// members are moved in and out by value with memcpy.
template <typename T> T load(const void *base, std::size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const char *>(base) + offset, sizeof(T));
    return value;
}
template <typename T> void store(void *base, std::size_t offset, const T &value)
{
    std::memcpy(static_cast<char *>(base) + offset, &value, sizeof(T));
}
TEST_CASE("glm_math.packed_vector_include")
{
    // Storage laid out as PackedVectors, never constructed as one: that would run
    // GLM's constructors at odd addresses. The alignment makes the offsets odd addresses.
    alignas(16) unsigned char packed[sizeof(PackedVectors)]{};
    const std::size_t fAt = offsetof(PackedVectors, f), dAt = offsetof(PackedVectors, d),
                      mAt = offsetof(PackedVectors, m);
    store(packed, fAt, glm::vec3(3.0f, 4.0f, 12.0f));
    store(packed, dAt, glm::dvec3(2.0, 3.0, 6.0));
    auto f = load<glm::vec3>(packed, fAt);
    auto d = load<glm::dvec3>(packed, dAt);
    REQUIRE(glm::length(f) == 13.0f);
    REQUIRE(std::abs(storm::math::lengthXZ(f) - std::sqrt(153.0f)) < 1e-5f);
    REQUIRE((glm::dot(f, f) == 169.0f && storm::math::lengthSquared(f) == 169.0f));
    REQUIRE((glm::dot(d, d) == 49.0 && storm::math::lengthSquared(d) == 49.0));
    const auto fn = storm::math::normalizeOrZero(f);
    const auto dn = glm::normalize(d);
    REQUIRE(std::abs(glm::dot(fn, fn) - 1.0f) < 1e-6f);
    REQUIRE(std::abs(glm::dot(dn, dn) - 1.0) < 1e-12);
    f += glm::vec3(1.0f, 2.0f, 3.0f);
    d *= glm::dvec3(2.0, 3.0, 4.0);
    store(packed, fAt, f);
    store(packed, dAt, d);
    f = load<glm::vec3>(packed, fAt);
    d = load<glm::dvec3>(packed, dAt);
    REQUIRE((f.x == 4 && f.y == 6 && f.z == 15));
    REQUIRE((d.x == 4 && d.y == 9 && d.z == 24));
    const auto fc = glm::cross(f, glm::vec3(1.0f, 0.0f, 0.0f));
    const auto dc = glm::cross(d, glm::dvec3(1.0, 0.0, 0.0));
    REQUIRE((fc.x == 0 && fc.y == 15 && fc.z == -6));
    REQUIRE((dc.x == 0 && dc.y == 24 && dc.z == -9));
    // A matrix at an odd address round-trips by value as well.
    const glm::mat4 m = storm::math::eulerMatrix(.3f, -.4f, .5f, 7.0f, 8.0f, 9.0f);
    store(packed, mAt, m);
    const auto back = load<glm::mat4>(packed, mAt);
    REQUIRE(std::memcmp(&back, &m, sizeof(m)) == 0);
    const auto moved = storm::math::transformPoint(back, glm::vec3(0.0f));
    REQUIRE((moved.x == 7 && moved.y == 8 && moved.z == 9));
    REQUIRE(packed[offsetof(PackedVectors, prefix)] == 0);
    // Serialized matrices at odd addresses are moved the same way.
    alignas(16) unsigned char bytes[66]{};
    float fixture[16];
    for (int i = 0; i < 16; ++i)
        fixture[i] = float(i + 1);
    std::memcpy(bytes + 1, fixture, sizeof(fixture));
    auto matrix = load<glm::mat4>(bytes, 1);
    REQUIRE((matrix[2][3] == 12.0f && matrix[3][0] == 13.0f));
    matrix[2][3] = 42.0f;
    store(bytes, 1, matrix);
    std::memcpy(fixture, bytes + 1, sizeof(fixture));
    REQUIRE((fixture[11] == 42.0f && bytes[0] == 0 && bytes[65] == 0));
    std::puts("PASS: first include under pack(1), packing restoration and odd-address math");
}
