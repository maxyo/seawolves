#include "strings_list.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <catch2/catch.hpp>

TEST_CASE("input_std.strings")
{
    static_assert(!std::is_copy_constructible_v<STRINGS_LIST>);
    STRINGS_LIST strings;
    REQUIRE(strings.GetString(0) == nullptr);
    REQUIRE(strings.GetStringCode(nullptr) == INVALID_ORDINAL_NUMBER);
    bool nullRejected = false;
    try
    {
        strings.AddString(nullptr);
    }
    catch (const std::runtime_error &)
    {
        nullRejected = true;
    }
    REQUIRE(nullRejected);
    REQUIRE(strings.MakeHashValue("abc") == 0x6783);
    REQUIRE(strings.MakeHashValue("ABC") == 0x6783);

    // Odd payload size guards the existing arbitrary-byte data contract.
    strings.SetStringDataSize(3);
    strings.AddString("First.C");
    char *first = strings.GetString(0);
    std::array<unsigned char, 3> data{255, 255, 255};
    REQUIRE(strings.GetStringData(0, data.data()));
    REQUIRE((data == std::array<unsigned char, 3>{0, 0, 0}));
    data = {0x00, 0x80, 0xff};
    REQUIRE(strings.SetStringData(0, data.data()));
    REQUIRE(!strings.AddUnicalString("FIRST.c"));
    REQUIRE(strings.GetStringCode("first.C") == 0);

    // Force several table growths; callers keep pointers into file names.
    for (int i = 0; i < 4096; ++i)
        strings.AddString(("script-" + std::to_string(i)).c_str());
    REQUIRE(strings.GetString(0) == first);
    REQUIRE(std::strcmp(first, "First.C") == 0);
    REQUIRE(strings.GetStringsCount() == 4097);
    std::array<unsigned char, 3> roundtrip{};
    REQUIRE((strings.GetStringData(0, roundtrip.data()) && roundtrip == data));
    const auto count = strings.GetStringsCount();
    REQUIRE(!strings.GetStringData(count, roundtrip.data()));
    REQUIRE(!strings.SetStringData(0, nullptr));

    // Compiler offsets are opaque binary payloads; same-size changes preserve
    // entries, changing payload size invalidates them, as before.
    strings.SetStringDataSize(3);
    REQUIRE(strings.GetStringsCount() == count);
    strings.SetStringDataSize(sizeof(uint32_t));
    REQUIRE(strings.GetStringsCount() == 0);
    strings.AddString("jump");
    uint32_t offset = 0x89abcdef, restored = 0;
    strings.SetStringData(0, &offset);
    strings.GetStringData(0, &restored);
    REQUIRE(restored == offset);
    strings.Release();
    strings.Release();
    REQUIRE(strings.GetStringsCount() == 0);
    strings.AddString("new-segment");
    strings.GetStringData(0, &restored);
    REQUIRE(restored == 0);
    strings.Release(); // The old empty-list release leaked its pointer table.
    strings.AddString("");
    REQUIRE(strings.GetStringCode("") == 0);
}
