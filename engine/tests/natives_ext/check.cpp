// The parts of Caribbean Legend's built-ins that need no VM (src/libs/core/src/natives_logic.*): hash, hrand,
// SmartSort keys and Sprintf.
#include "natives_logic.hpp"

#include <set>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

using namespace storm::natives;

TEST_CASE("natives_ext.hash")
{
    REQUIRE(hashText("alpha") == hashText("alpha"));
    REQUIRE(hashText("alpha") != hashText("alphb"));
}

TEST_CASE("natives_ext.hrand")
{
    std::set<int32_t> seen;
    for (int i = 0; i < 2000; ++i)
    {
        const int32_t v = hashRandom(9, "seed", "site" + std::to_string(i), "");
        REQUIRE((v >= 0 && v <= 9));
        seen.insert(v);
    }
    REQUIRE(seen.size() == 10); // every value of [0, 9]
    REQUIRE(hashRandom(1000, "s", "f@1", "t") == hashRandom(1000, "s", "f@1", "t"));
    int sites = 0;
    for (int i = 0; i < 20; ++i)
        sites += hashRandom(100000, "s", "f@" + std::to_string(i), "t") != hashRandom(100000, "s", "g@7", "t");
    REQUIRE(sites >= 18); // a plain tag: each call site its own number
    REQUIRE(hashRandom(100000, "s", "f@1", "&t") == hashRandom(100000, "s", "g@7", "&t")); // '&': shared
    int seeds = 0;
    for (int i = 0; i < 20; ++i)
        seeds += hashRandom(100000, "day1", "f@" + std::to_string(i), "") !=
                 hashRandom(100000, "day2", "f@" + std::to_string(i), "");
    REQUIRE(seeds >= 18); // another seed, other numbers
    REQUIRE((hashRandom(0, "s", "x", "") == 0 && hashRandom(-5, "s", "x", "") == 0));
}

TEST_CASE("natives_ext.sort_keys")
{
    SortType type{};
    REQUIRE((parseSortType("int", type) && type == SortType::Int));
    REQUIRE(!parseSortType("bogus", type));
    const auto less = [](std::string_view a, std::string_view b, int32_t offset, SortType sort) {
        return sortKeyLess(makeSortKey(a, offset, sort), makeSortKey(b, offset, sort), sort);
    };
    REQUIRE(less("ico_2", "ico_10", 4, SortType::Int)); // the offset skips a prefix
    REQUIRE(less("apple", "Banana", 0, SortType::String));
    REQUIRE(less("1.5", "1.75", 0, SortType::Float));
    REQUIRE(less("31.12.1650", "01.01.1651", 0, SortType::Date)); // day.month.year: the year first
    REQUIRE(less("1650-12-31", "1651-01-01", 0, SortType::Date));
    REQUIRE(less("05.03.1650 10:15", "05.03.1650 10:20", 0, SortType::Date));
    REQUIRE(!less("05.03.1650", "05.03.1650", 0, SortType::Date));
}

TEST_CASE("natives_ext.sprintf")
{
    std::string error;
    REQUIRE(formatScript("%d apples and %s", {int32_t{3}, std::string("pears")}, error) == "3 apples and pears");
    REQUIRE(formatScript("%5.2f|%-4d|%03d|%x", {1.5f, int32_t{7}, int32_t{5}, int32_t{255}}, error) ==
            " 1.50|7   |005|ff");
    REQUIRE(formatScript("100%%", {}, error) == "100%");
    REQUIRE(error.empty());
    REQUIRE(formatScript("%d and %d", {int32_t{1}}, error) == "%d and %d"); // too few arguments: unchanged, said
    REQUIRE(!error.empty());
}
