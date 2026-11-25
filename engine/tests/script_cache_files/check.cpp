#include "script_cache_files.hpp"

#include <iostream>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using namespace storm::script_cache;

static void put(const fs::path &path, const std::string &bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

static std::string slurp(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

TEST_CASE("script_cache_files")
{
    const fs::path work = fs::temp_directory_path();
    const fs::path cache = work / "Cache";
    uint64_t key = 0;

    // State: first start, damaged state file, reset stamps the key and empties the folder.
    REQUIRE(readState(cache, key) == StateFile::Missing);
    fs::create_directories(cache / "PROGRAM");
    put(cache / kStateFile, "abc");
    put(cache / "PROGRAM" / "seadogs.b", "old");
    REQUIRE(readState(cache, key) == StateFile::Damaged);
    REQUIRE(resetFolder(cache, 0x1122334455667788ull).empty());
    REQUIRE((readState(cache, key) == StateFile::Read && key == 0x1122334455667788ull));
    REQUIRE(!fs::exists(cache / "PROGRAM"));

    // A changed key (scripts edited, other engine build) is a plain reset too.
    put(cache / "stale.b", "x");
    REQUIRE(resetFolder(cache, 42).empty());
    REQUIRE((readState(cache, key) == StateFile::Read && key == 42 && !fs::exists(cache / "stale.b")));

    // Reset that cannot create the folder reports why.
    put(work / "file", "");
    REQUIRE(!resetFolder(work / "file" / "Cache", 1).empty());

    // Entries: round trip, ordinary miss, and every kind of damage.
    const auto entry = cache / "PROGRAM\\interface\\save_load.b"; // segment names keep backslashes on Android
    std::vector<char> payload;
    REQUIRE(readEntry(entry, payload) == Entry::Missing);
    const std::string body = std::string("bytecode\0with\0zeros", 19);
    REQUIRE(writeEntry(entry, body).empty());
    REQUIRE(!fs::exists(entry.string() + ".tmp"));
    REQUIRE((readEntry(entry, payload) == Entry::Read && std::string(payload.begin(), payload.end()) == body));

    const auto good = slurp(entry);
    auto flipped = good;
    flipped.back() ^= 1;
    put(entry, flipped);
    REQUIRE(readEntry(entry, payload) == Entry::Damaged);
    put(entry, good.substr(0, good.size() - 3)); // torn write
    REQUIRE(readEntry(entry, payload) == Entry::Damaged);
    put(entry, good.substr(0, sizeof(EntryHeader))); // header only
    REQUIRE(readEntry(entry, payload) == Entry::Damaged);
    put(entry, ""); // empty file
    REQUIRE(readEntry(entry, payload) == Entry::Damaged);
    auto magic = good;
    magic[7] = '1'; // previous layout
    put(entry, magic);
    REQUIRE(readEntry(entry, payload) == Entry::Damaged);

    // Rewrite over a damaged entry recovers it.
    REQUIRE(writeEntry(entry, body).empty());
    REQUIRE(readEntry(entry, payload) == Entry::Read);

    // Write into an unwritable place reports why and leaves nothing behind.
    REQUIRE(!writeEntry(work / "file" / "x.b", body).empty());

    std::cout << "script cache files: state missing/damaged/reset, entry round trip, torn/flipped/empty/old-magic "
                 "entries detected PASS\n";
}
