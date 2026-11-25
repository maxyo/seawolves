// storm::save::checkFile (src/libs/core/src/save_file.hpp), the check the app runs on a save the player restores:
// saves made by the engine's own writer, damaged in the ways a copy or a foreign file damages them.
#include "save_file.hpp"

#include <catch2/catch.hpp>

#include <cstdlib>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace storm::save;

namespace
{
fs::path scratch(const char *name)
{
    const char *tmp = std::getenv("TMPDIR");
    const fs::path dir = fs::path(tmp ? tmp : "/tmp") / "save_check";
    fs::create_directories(dir);
    return dir / name;
}

Bytes engineSave()
{
    const std::string state(20000, 's'), description = "Charles · Saint-Pierre";
    Bytes image;
    std::string error;
    REQUIRE(buildState("save", state.data(), static_cast<uint32_t>(state.size()), image, error));
    REQUIRE(appendExtData(image, "save", description.data(), static_cast<uint32_t>(description.size()), error));
    return image;
}

fs::path put(const char *name, const Bytes &image)
{
    const auto path = scratch(name);
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(image.data(), std::streamsize(image.size()));
    return path;
}

void setWord(Bytes &image, size_t at, uint32_t value)
{
    std::memcpy(image.data() + at, &value, sizeof(value));
}

// The reason a file is refused for; fails the check when it is accepted.
std::string refused(const fs::path &path)
{
    std::string error;
    CHECK_FALSE(checkFile(path, error));
    return error;
}
} // namespace

TEST_CASE("a save the engine wrote passes and is left as it was")
{
    const auto image = engineSave();
    const auto path = put("good", image);
    const auto written = fs::last_write_time(path);
    std::string error;
    CHECK(checkFile(path, error));
    Bytes after;
    REQUIRE(readFile(path, after, error));
    CHECK(after == image);
    CHECK(fs::last_write_time(path) == written);

    const std::string state = "state only";
    Bytes plain;
    REQUIRE(buildState("save", state.data(), uint32_t(state.size()), plain, error));
    CHECK(checkFile(put("state-only", plain), error));
}

TEST_CASE("a truncated save is refused, naming the size field that runs past the end")
{
    const auto image = engineSave();
    uint32_t packedSize;
    std::memcpy(&packedSize, image.data() + sizeof(Header) + 4, 4);
    const size_t stateEnd = sizeof(Header) + 8 + packedSize;
    CHECK_THAT(refused(put("cut-state", Bytes(image.begin(), image.begin() + stateEnd - 5))),
               Catch::Contains("packedSize") && Catch::Contains("past the end"));
    CHECK_THAT(refused(put("cut-description", Bytes(image.begin(), image.end() - 3))),
               Catch::Contains("extPackedSize") && Catch::Contains("past the end"));
    CHECK_THAT(refused(put("cut-header", Bytes(image.begin(), image.begin() + 20))), Catch::Contains("file size"));
}

TEST_CASE("a damaged header is refused, naming the field")
{
    const auto image = engineSave();
    auto raw = image;
    setWord(raw, sizeof(Header), 0);
    CHECK_THAT(refused(put("raw-size", raw)), Catch::Contains("rawSize 0"));
    auto offset = image;
    setWord(offset, offsetof(Header, dwExtDataOffset), 0xfffffff0u);
    CHECK_THAT(refused(put("ext-offset", offset)), Catch::Contains("extOffset"));
    auto extSize = image;
    setWord(extSize, offsetof(Header, dwExtDataSize), kMaxSectionSize + 1);
    CHECK_THAT(refused(put("ext-size", extSize)), Catch::Contains("extSize"));
    auto small = image; // the state inflates to more than its header declares
    setWord(small, sizeof(Header), 100);
    CHECK_THAT(refused(put("raw-size-small", small)), Catch::Contains("rawSize 100"));
}

TEST_CASE("damaged data, a foreign file, a missing and an oversized one are refused")
{
    auto flipped = engineSave();
    flipped[sizeof(Header) + 8 + 4] ^= 0x55;
    CHECK_THAT(refused(put("flipped", flipped)), Catch::Contains("packedSize"));
    const std::string text = "hello, not a save at all, long enough for a header....";
    CHECK_FALSE(refused(put("text", Bytes(text.begin(), text.end()))).empty());
    CHECK_FALSE(refused(scratch("missing")).empty());
    const auto huge = put("huge", engineSave());
    fs::resize_file(huge, kMaxFileSize + 1); // sparse: nothing is written
    CHECK_THAT(refused(huge), Catch::Contains("file size"));
}
