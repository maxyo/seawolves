#include "storm/fx/relief_store.hpp"
#include "storm/fx/upscale.hpp"
#include <fstream>
#include <filesystem>
#include <vector>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::fx;
TEST_CASE("texture_cache_codec")
{
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::vector<uint8_t> bytes{1, 2, 3, 4};
    REQUIRE(WriteUpscaled(dir, 1, bytes));
    REQUIRE(ReadUpscaled(dir, 1) == bytes);
    std::string error;
    REQUIRE((!ReadUpscaled(dir, 1, &error, 1) && error == "cache read reservation changed" && HasUpscaled(dir, 1)));
    auto badtarget = UpscaleFile(dir, 2);
    std::filesystem::create_directory(badtarget);
    std::ofstream(badtarget / "keep") << "original";
    REQUIRE((!WriteUpscaled(dir, 2, bytes) && "rename failure must not report success after cleanup"));
    REQUIRE(std::filesystem::exists(badtarget / "keep"));
    REQUIRE(!std::filesystem::exists(badtarget.string() + ".tmp"));
    std::ofstream(UpscaleFile(dir, 1), std::ios::binary | std::ios::app).put('x');
    REQUIRE(!ReadUpscaled(dir, 1));
    NormalMap map;
    map.width = map.height = 4;
    map.mip_count = 3;
    map.format = NormalMapFormat::Rg8;
    map.rg.resize(NormalMapBytes(4, 4, map.format), 7);
    REQUIRE(WriteReliefMap(dir, 3, map));
    auto valid = ReadReliefMap(dir, 3);
    REQUIRE((valid && valid->rg == map.rg));
    REQUIRE((!ReadReliefMap(dir, 3, &error, 1) && error == "cache read reservation changed" && HasReliefMap(dir, 3)));
    std::ofstream(ReliefFile(dir, 3), std::ios::binary | std::ios::app).put('x');
    REQUIRE(!ReadReliefMap(dir, 3));
    REQUIRE(WriteReliefMap(dir, 3, map));
    std::filesystem::resize_file(ReliefFile(dir, 3), std::filesystem::file_size(ReliefFile(dir, 3)) - 1);
    REQUIRE(!ReadReliefMap(dir, 3));
    // Valid legacy bytes survive format-preserving rewrite/read.
    REQUIRE(WriteReliefMap(dir, 3, map));
    REQUIRE(ReadReliefMap(dir, 3));
    std::cout << "PASS actual legacy codec roundtrip/EOF/truncation/reservation/rename failure\n";
}
