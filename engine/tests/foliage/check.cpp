// The foliage readers over small invented files (good ones, and each kind of broken one refused with its reason) and
// the selection: the same trees every time, a larger share only adds, each tree in one cell.
#include <foliage_files.hpp>
#include <selection.hpp>
#include <zip_archive.hpp>

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <catch2/catch.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace
{
using nlohmann::json;
using namespace storm::foliage;
using Bytes = std::vector<uint8_t>;

std::filesystem::path TempPath(const std::string &name)
{
    const char *tmp = std::getenv("TMPDIR");
    return std::filesystem::path(tmp ? tmp : "/tmp") / name;
}

void Put16(Bytes &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(Bytes &out, uint32_t v)
{
    Put16(out, v & 0xffff);
    Put16(out, v >> 16);
}

Bytes Deflate(const Bytes &in)
{
    z_stream s{};
    deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    Bytes out(deflateBound(&s, static_cast<uLong>(in.size())));
    s.next_in = const_cast<uint8_t *>(in.data());
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());
    deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

// A zip of `entries`: deflated, except names ending in .dds (stored, as the game's archives keep textures).
std::filesystem::path WriteZip(const std::string &file, const std::map<std::string, Bytes> &entries)
{
    Bytes zip, directory;
    for (const auto &[name, data] : entries)
    {
        const bool stored = name.ends_with(".dds");
        const Bytes packed = stored ? data : Deflate(data);
        const uint32_t crc = crc32(0, data.data(), static_cast<uInt>(data.size()));
        const uint32_t offset = static_cast<uint32_t>(zip.size());
        for (Bytes *out : {&zip, &directory})
        {
            const bool central = out == &directory;
            Put32(*out, central ? 0x02014b50 : 0x04034b50);
            if (central)
                Put16(*out, 20);
            Put16(*out, 20);
            Put16(*out, 0);
            Put16(*out, stored ? 0 : 8);
            Put32(*out, 0);
            Put32(*out, crc);
            Put32(*out, static_cast<uint32_t>(packed.size()));
            Put32(*out, static_cast<uint32_t>(data.size()));
            Put16(*out, static_cast<uint32_t>(name.size()));
            Put16(*out, 0);
            if (central)
            {
                Put16(*out, 0);
                Put16(*out, 0);
                Put16(*out, 0);
                Put32(*out, 0);
                Put32(*out, offset);
            }
            out->insert(out->end(), name.begin(), name.end());
        }
        zip.insert(zip.end(), packed.begin(), packed.end());
    }
    const uint32_t directory_offset = static_cast<uint32_t>(zip.size());
    zip.insert(zip.end(), directory.begin(), directory.end());
    Put32(zip, 0x06054b50);
    Put32(zip, 0);
    Put16(zip, static_cast<uint32_t>(entries.size()));
    Put16(zip, static_cast<uint32_t>(entries.size()));
    Put32(zip, static_cast<uint32_t>(directory.size()));
    Put32(zip, directory_offset);
    Put16(zip, 0);
    const auto path = TempPath(file);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(zip.data()), zip.size());
    return path;
}

Bytes MeshFile(const std::vector<MeshVertex> &vertices, const std::vector<uint32_t> &indices, uint32_t stride = 40)
{
    Bytes out{'B', 'F', 'L', 'M', '0', '0', '0', '1'};
    Put32(out, static_cast<uint32_t>(vertices.size()));
    Put32(out, static_cast<uint32_t>(indices.size()));
    Put32(out, stride);
    const auto *v = reinterpret_cast<const uint8_t *>(vertices.data());
    out.insert(out.end(), v, v + vertices.size() * sizeof(MeshVertex));
    for (const uint32_t i : indices)
        Put32(out, i);
    return out;
}

Bytes Attributes(size_t vertices, uint32_t floats = kAttributeChannels * 2)
{
    Bytes out{'B', 'F', 'L', 'A', '0', '0', '0', '1'};
    Put32(out, static_cast<uint32_t>(vertices));
    Put32(out, floats);
    for (size_t i = 0; i < vertices * floats; ++i)
    {
        const float value = 0.25f * static_cast<float>(i);
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        Put32(out, raw);
    }
    return out;
}

MeshVertex Vertex(float x, float y, float z)
{
    return MeshVertex{{x, y, z}, {0, 1, 0}, {x, y}, 1.f, 0xffffffffu};
}

// An invented two-level tree: a bark triangle, then a billboard quad.
struct Tree
{
    json manifest;
    std::map<std::string, Bytes> entries;

    Tree()
    {
        json channels = json::array();
        for (const std::string_view name : kChannelNames)
            channels.push_back(name);
        const auto material = [](const char *name, bool billboard, const char *texture) {
            return json{{"Name", name},           {"TwoSided", false},      {"FlipBackfaceNormals", true},
                        {"VertexOpacity", false}, {"Billboard", billboard}, {"UserData", ""},
                        {"Color", texture},       {"NormalGloss", texture}, {"SubsurfaceAo", texture},
                        {"AlphaCutoff", 0.4}};
        };
        manifest = {
            {"Version", 1},
            {"Kind", "foliage"},
            {"Coordinates", "RH_Y_UP_METERS"},
            {"Name", "Invented_Fern"},
            {"Cooked", true},
            {"LodScreenHeights", {0.4, 0.1}},
            {"AttributeChannels", channels},
            {"AttributeCoordinates", "SPEEDTREE_SOURCE_Z_UP"},
            {"Materials", {material("Stem", false, "textures/a.dds"), material("Card", true, "textures/b.dds")}},
            {"Meshes",
             {{{"Name", "LOD0/Stem"},
               {"Lod", 0},
               {"Material", 0},
               {"Geometry", "meshes/0.bin"},
               {"Attributes", "attributes/0.bin"}},
              {{"Name", "LOD1/Card"},
               {"Lod", 1},
               {"Material", 1},
               {"Geometry", "meshes/1.bin"},
               {"Attributes", "attributes/1.bin"}}}},
            {"Textures",
             {{{"Path", "textures/a.dds"}, {"Sha256", "aa"}, {"Bytes", 8}},
              {{"Path", "textures/b.dds"}, {"Sha256", "bb"}, {"Bytes", 12}}}}};
        entries["meshes/0.bin"] = MeshFile({Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(0, 2, 0)}, {0, 1, 2});
        entries["attributes/0.bin"] = Attributes(3);
        entries["meshes/1.bin"] =
            MeshFile({Vertex(-1, 0, 0), Vertex(1, 0, 0), Vertex(1, 2, 0), Vertex(-1, 2, 0)}, {0, 1, 2, 0, 2, 3});
        entries["attributes/1.bin"] = Attributes(4);
        entries["textures/a.dds"] = Bytes(8, 'a');
        entries["textures/b.dds"] = Bytes(12, 'b');
    }

    std::optional<Asset> read(std::string &error) const
    {
        auto files = entries;
        const std::string text = manifest.dump();
        files["manifest.json"] = Bytes(text.begin(), text.end());
        const auto archive = storm::zip::Archive::Open(WriteZip("tree.bmfoliage", files), error);
        REQUIRE(archive);
        return ReadAsset(*archive, error);
    }
};

std::string Refusal(const Tree &tree)
{
    std::string error;
    const auto asset = tree.read(error);
    CHECK_FALSE(asset);
    return error;
}

json PlaceJson(size_t count, float spacing = 7.f)
{
    json instances = json::array();
    for (size_t i = 0; i < count; ++i)
    {
        const float x = spacing * static_cast<float>(i % 97), z = spacing * static_cast<float>(i / 97);
        instances.push_back({{"id", "tree-" + std::to_string(i * 7919)},
                             {"assetId", "x"},
                             {"resource", i % 3 ? "foliage/Oak_A.bmfoliage" : "foliage/Palm_B.bmfoliage"},
                             {"transform", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 2, z, 1}}});
    }
    return {{"version", 1}, {"coordinates", "ENGINE_Y_UP"}, {"location", "Invented"}, {"instances", instances}};
}

Place ReadGood(const json &document)
{
    std::string error;
    auto place = ReadPlace(document.dump(), error);
    INFO(error);
    REQUIRE(place);
    return *place;
}
} // namespace

TEST_CASE("an invented tree asset is read whole")
{
    std::string error;
    const auto asset = Tree().read(error);
    INFO(error);
    REQUIRE(asset);
    CHECK(asset->name == "Invented_Fern");
    CHECK(asset->lods == 2);
    REQUIRE(asset->meshes.size() == 2);
    CHECK(asset->meshes[1].indices == std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
    CHECK(asset->meshes[0].vertices[2].position[1] == 2.f);
    CHECK(asset->meshes[1].attributes.size() == 4 * kAttributeChannels * 2);
    CHECK(asset->meshes[1].attributes[5] == 1.25f);
    CHECK(asset->materials[1].billboard);
    CHECK(asset->materials[0].alpha_cutoff == 0.4f);
}

TEST_CASE("a broken tree asset is refused with its reason")
{
    Tree tree;
    SECTION("mesh magic")
    {
        tree.entries["meshes/0.bin"][3] = 'X';
        CHECK_THAT(Refusal(tree), Catch::Contains("not a BFLM0001"));
    }
    SECTION("mesh size")
    {
        tree.entries["meshes/0.bin"].push_back(0);
        CHECK_THAT(Refusal(tree), Catch::Contains("do not fill"));
    }
    SECTION("vertex stride")
    {
        tree.entries["meshes/0.bin"] = MeshFile({Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(0, 2, 0)}, {0, 1, 2}, 36);
        CHECK_THAT(Refusal(tree), Catch::Contains("stride of 36"));
    }
    SECTION("index past the vertices")
    {
        tree.entries["meshes/0.bin"] = MeshFile({Vertex(0, 0, 0), Vertex(1, 0, 0), Vertex(0, 2, 0)}, {0, 1, 3});
        CHECK_THAT(Refusal(tree), Catch::Contains("index past"));
    }
    SECTION("attributes for another vertex count")
    {
        tree.entries["attributes/1.bin"] = Attributes(3);
        CHECK_THAT(Refusal(tree), Catch::Contains("attributes/1.bin"));
    }
    SECTION("attribute channels")
    {
        tree.manifest["AttributeChannels"][4] = "st_something_else";
        CHECK_THAT(Refusal(tree), Catch::Contains("AttributeChannels"));
    }
    SECTION("a texture the archive lacks")
    {
        tree.entries.erase("textures/b.dds");
        CHECK_THAT(Refusal(tree), Catch::Contains("textures/b.dds"));
    }
    SECTION("a billboard material on a mesh level")
    {
        tree.manifest["Meshes"][0]["Material"] = 1;
        CHECK_THAT(Refusal(tree), Catch::Contains("billboard"));
    }
    SECTION("a gap in the levels")
    {
        tree.manifest["Meshes"][1]["Lod"] = 2;
        CHECK_THAT(Refusal(tree), Catch::Contains("levels"));
    }
    SECTION("vertex opacity")
    {
        tree.manifest["Materials"][0]["VertexOpacity"] = true;
        CHECK_THAT(Refusal(tree), Catch::Contains("vertex opacity"));
    }
    SECTION("another frame")
    {
        tree.manifest["Coordinates"] = "LH_Z_UP";
        CHECK_THAT(Refusal(tree), Catch::Contains("Coordinates"));
    }
}

TEST_CASE("a zip entry that fails its CRC is refused")
{
    std::map<std::string, Bytes> files{{"x.dds", Bytes(64, 'x')}};
    const auto path = WriteZip("crc.zip", files);
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(30 + 5 + 10); // inside the stored data of the only entry
        file.put('y');
    }
    std::string error;
    const auto archive = storm::zip::Archive::Open(path, error);
    REQUIRE(archive);
    CHECK_FALSE(archive->read("x.dds", error));
    CHECK_THAT(error, Catch::Contains("CRC"));
}

TEST_CASE("the settings keep the keys drawn with and refuse what is missing or out of range")
{
    json settings = {{"enabled", true},    {"draw_distance", 700},         {"impostor_distance", 100},
                     {"lod_scale", 1.0},   {"island_draw_distance", 5000}, {"island_density", 0.69},
                     {"leaf_motion", 1.23}};
    std::string error;
    const auto good = ReadSettings(settings.dump(), error);
    REQUIRE(good);
    CHECK(good->island_density == Approx(0.69f));
    CHECK(good->draw_distance == 700.f);
    settings.erase("island_density");
    CHECK_FALSE(ReadSettings(settings.dump(), error));
    CHECK_THAT(error, Catch::Contains("lacks island_density"));
    settings["island_density"] = 1.5;
    CHECK_FALSE(ReadSettings(settings.dump(), error));
    CHECK_THAT(error, Catch::Contains("outside 0..1"));
    settings["island_density"] = "high";
    CHECK_FALSE(ReadSettings(settings.dump(), error));
    CHECK_THAT(error, Catch::Contains("mistyped island_density"));
}

TEST_CASE("a place's trees are read and a broken instance is refused by its index")
{
    const Place place = ReadGood(PlaceJson(5));
    CHECK(place.assets == std::vector<std::string>{"foliage/palm_b.bmfoliage", "foliage/oak_a.bmfoliage"});
    REQUIRE(place.instances.size() == 5);
    CHECK(place.instances[1].asset == 1);
    CHECK(place.instances[3].transform[3] == glm::vec4(21, 2, 0, 1));
    CHECK(place.instances[2].rank == Rank("tree-15838"));

    std::string error;
    json broken = PlaceJson(5);
    broken["instances"][3]["transform"].erase(15);
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    CHECK_THAT(error, Catch::Contains("instance 3 has a transform of 15"));
    broken = PlaceJson(5);
    broken["instances"][2]["transform"][3] = 0.5;
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    CHECK_THAT(error, Catch::Contains("instance 2 has a transform that is not affine"));
    broken = PlaceJson(5);
    broken["instances"][4]["resource"] = "trees/Oak.gm";
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    CHECK_THAT(error, Catch::Contains("instance 4 names"));
    broken = PlaceJson(5);
    broken["instances"][0]["id"] = "";
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    CHECK_THAT(error, Catch::Contains("empty id"));
    broken = PlaceJson(5);
    broken["coordinates"] = "ENGINE_Z_UP";
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    broken = PlaceJson(5);
    broken["version"] = 2;
    CHECK_FALSE(ReadPlace(broken.dump(), error));
    CHECK_THAT(error, Catch::Contains("version 2"));
}

namespace
{
std::set<uint32_t> Drawn(const Place &place, const Cells &cells, float share)
{
    std::set<uint32_t> drawn;
    for (uint32_t c = 0; c + 1 < cells.begin.size(); ++c)
        for (const uint32_t i : cells.drawn(place, c, RankLimit(share)))
            drawn.insert(i);
    return drawn;
}
} // namespace

TEST_CASE("a share draws the same trees each time and a larger share only adds")
{
    const Place place = ReadGood(PlaceJson(5000, 3.f));
    const Cells cells = BuildCells(place, 64.f);

    // Each tree in exactly one cell, and inside it.
    std::vector<int> seen(place.instances.size());
    bool inside = true;
    for (uint32_t c = 0; c + 1 < cells.begin.size(); ++c)
    {
        for (uint32_t k = cells.begin[c]; k < cells.begin[c + 1]; ++k)
        {
            const glm::vec4 at = place.instances[cells.order[k]].transform[3];
            const uint32_t column = c % cells.columns, row = c / cells.columns;
            inside = inside && at.x >= cells.origin.x + cells.size * column &&
                     at.x < cells.origin.x + cells.size * (column + 1) && at.z >= cells.origin.y + cells.size * row &&
                     at.z < cells.origin.y + cells.size * (row + 1);
            ++seen[cells.order[k]];
        }
    }
    CHECK(inside);
    CHECK(std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }));

    CHECK(Drawn(place, cells, 0.f).empty());
    CHECK(Drawn(place, cells, 1.f).size() == place.instances.size());
    std::set<uint32_t> previous;
    for (float share = 0.05f; share <= 1.001f; share += 0.05f)
    {
        const std::set<uint32_t> drawn = Drawn(place, cells, share);
        CHECK(std::includes(drawn.begin(), drawn.end(), previous.begin(), previous.end()));
        // The ranks spread evenly: the drawn share stays within 2 % of the trees of the asked one.
        CHECK(std::abs(static_cast<double>(drawn.size()) / place.instances.size() - share) < 0.02);
        previous = drawn;
    }

    // The same trees from a second reading and a second grid.
    const Place again = ReadGood(PlaceJson(5000, 3.f));
    CHECK(Drawn(again, BuildCells(again, 64.f), 0.37f) == Drawn(place, cells, 0.37f));
    // And by tree, whatever the cell size.
    CHECK(Drawn(place, BuildCells(place, 256.f), 0.37f) == Drawn(place, cells, 0.37f));
}
