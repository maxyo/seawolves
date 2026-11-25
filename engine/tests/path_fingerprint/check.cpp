#include "platform/path_fingerprint.hpp"

#include <fstream>
#include <iostream>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using storm::fingerprint::tree;

static void put(const fs::path &path, const std::string &text, fs::file_time_type time)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::trunc) << text;
    fs::last_write_time(path, time);
}

// What the engine used before: order-free sum of modification times.
static uint64_t legacySum(const fs::path &root)
{
    uint64_t sum = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file())
            sum += fs::last_write_time(entry.path()).time_since_epoch().count();
    return sum;
}

TEST_CASE("path_fingerprint")
{
    const fs::path root = fs::temp_directory_path() / "PROGRAM";
    const auto t1 = fs::file_time_type::clock::now() - std::chrono::hours(48);
    const auto t2 = t1 + std::chrono::hours(1);
    put(root / "seadogs.c", "void Main(){}", t1);
    put(root / "interface/menu.c", "int a;", t2);
    put(root / "interface/save.c", "int b;", t2);
    const auto base = tree(root);
    REQUIRE((base != 0 && base == tree(root)));
    REQUIRE(tree(root / "missing") == 0);
    REQUIRE(tree(fs::temp_directory_path() / "PROGRAM\\") ==
            0); // unresolved Windows spelling: caller must resolve first

    // Same content under another root: identical (relative names only).
    const fs::path copy = fs::temp_directory_path() / "copy";
    put(copy / "seadogs.c", "void Main(){}", t1);
    put(copy / "interface/save.c", "int b;", t2);
    put(copy / "interface/menu.c", "int a;", t2);
    REQUIRE(tree(copy) == base);

    // Edit that keeps the timestamp (archive extraction, mod manager).
    const auto legacy = legacySum(root);
    put(root / "seadogs.c", "void Main(){ Trace(1); }", t1);
    REQUIRE(legacySum(root) == legacy);
    const auto edited = tree(root);
    REQUIRE(edited != base);

    // Rename, and timestamps swapped between two files: invisible to a sum.
    fs::rename(root / "interface/menu.c", root / "interface/menu2.c");
    REQUIRE((legacySum(root) == legacy && tree(root) != edited));
    fs::rename(root / "interface/menu2.c", root / "interface/menu.c");
    REQUIRE(tree(root) == edited);
    fs::last_write_time(root / "seadogs.c", t2);
    fs::last_write_time(root / "interface/menu.c", t1);
    REQUIRE((legacySum(root) == legacy && tree(root) != edited));

    // Added and removed files, plain mtime change, single-file root.
    const auto before = tree(root);
    put(root / "new.c", "", t1);
    REQUIRE(tree(root) != before);
    fs::remove(root / "new.c");
    REQUIRE(tree(root) == before);
    fs::last_write_time(root / "interface/save.c", t2 + std::chrono::seconds(1));
    REQUIRE(tree(root) != before);
    REQUIRE(tree(root / "seadogs.c") != 0);
    std::cout << "path fingerprint: rename/same-mtime edit/swap/add/remove detected PASS\n";
}
