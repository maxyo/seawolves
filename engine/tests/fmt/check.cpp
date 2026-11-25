// The pinned fmt: formatting results and float formatting under ASan+UBSan (fmt 8 copied overlapping ranges in
// write_significand, a memcpy-param-overlap that stopped every ASan engine run at the first sea scene).
#include <fmt/format.h>
#include <cstdlib>
#include <random>

#include <catch2/catch.hpp>
enum class Kind
{
    a = 3
};
TEST_CASE("fmt")
{
    REQUIRE(fmt::format(FMT_STRING("{:x}"), 255) == "ff");
    REQUIRE(fmt::format(FMT_STRING("{:08x}"), 255) == "000000ff");
    REQUIRE(fmt::format("{} {:.2f}", "value", 1.25) == "value 1.25");
    REQUIRE(fmt::format("{}", fmt::underlying(Kind::a)) == "3");
    REQUIRE(fmt::format("{} {} {:g}", 0.1f, 1e-7, 123456789.0f) == "0.1 1e-07 1.23457e+08");
    // Every float shape (shortest, fixed, exponent, padded) over many magnitudes.
    std::mt19937 random(1);
    std::uniform_real_distribution<double> value(-1e6, 1e6);
    size_t length = 0;
    for (int i = 0; i < 100000; ++i)
    {
        const double v = value(random) * (i % 7 == 0 ? 1e-9 : 1.0);
        const float f = static_cast<float>(v);
        length += fmt::format("{} {:.3f} {:g} {:e} {:10.4f} {}", v, v, f, f, f, static_cast<float>(i) / 3.0f).size();
    }
    REQUIRE(length > 0);
}
