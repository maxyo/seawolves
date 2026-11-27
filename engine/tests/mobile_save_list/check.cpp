// The save list in the touch UI and what the game's list reads of a record: a record's description and picture
// (save_record.hpp, save_thumbnail.hpp), its facts (SaveFactsCache, ParseSaveDescription), the list of every
// profile's records with stable row identities (SaveListCapability), and a row chosen through the game's own script
// (SelectSaveTarget, RestoreSaveTarget, ProfileToChoose).
#include "core_fake.hpp"
#include "mobile_save_facts.hpp"
#include "mobile_save_list.hpp"
#include "save_record.hpp"

#include <catch2/catch.hpp>
#include <spdlog/sinks/callback_sink.h>

#include <map>

static storm::test::FakeCore fake;
Core &core = fake;

using namespace storm::mobile;
namespace thumb = storm::save::thumbnail;

namespace
{
// Errors logged (spdlog level err): a bad record is reported loudly.
int errors = 0;
const bool logger = [] {
    auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [](const spdlog::details::log_msg &message) { errors += message.level == spdlog::level::err; });
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("check", sink));
    return true;
}();

// The record files: existence and write time (the bytes are fake.saveData).
class Files final : public VFILE_SERVICE
{
    [[noreturn]] static void unmodelled(const char *what)
    {
        throw std::logic_error(std::string("Files: the check does not model ") + what);
    }

  public:
    std::map<std::string, std::filesystem::file_time_type> stamps;

    bool _FileOrDirectoryExists(const char *path) override
    {
        return fake.saveData.contains(path);
    }
    std::filesystem::file_time_type _GetLastWriteTime(const char *path) override
    {
        return stamps.at(path);
    }
    std::time_t _ToTimeT(std::filesystem::file_time_type) override
    {
        return 1700000000;
    }

    std::fstream _CreateFile(const char *, std::ios::openmode) override
    {
        unmodelled("_CreateFile");
    }
    void _CloseFile(std::fstream &) override
    {
        unmodelled("_CloseFile");
    }
    void _SetFilePointer(std::fstream &, std::streamoff, std::ios::seekdir) override
    {
        unmodelled("_SetFilePointer");
    }
    bool _DeleteFile(const char *) override
    {
        unmodelled("_DeleteFile");
    }
    bool _WriteFile(std::fstream &, const void *, std::streamsize) override
    {
        unmodelled("_WriteFile");
    }
    bool _ReadFile(std::fstream &, void *, std::streamsize) override
    {
        unmodelled("_ReadFile");
    }
    std::vector<std::string> _GetPathsOrFilenamesByMask(const char *, const char *, bool, bool, bool, bool) override
    {
        unmodelled("_GetPathsOrFilenamesByMask");
    }
    std::vector<std::filesystem::path> _GetFsPathsByMask(const char *, const char *, bool, bool, bool, bool) override
    {
        unmodelled("_GetFsPathsByMask");
    }
    void _FlushFileBuffers(std::fstream &) override
    {
        unmodelled("_FlushFileBuffers");
    }
    std::string _GetCurrentDirectory() override
    {
        unmodelled("_GetCurrentDirectory");
    }
    std::string _GetExecutableDirectory() override
    {
        unmodelled("_GetExecutableDirectory");
    }
    std::uintmax_t _GetFileSize(const char *) override
    {
        unmodelled("_GetFileSize");
    }
    void _SetCurrentDirectory(const char *) override
    {
        unmodelled("_SetCurrentDirectory");
    }
    bool _CreateDirectory(const char *) override
    {
        unmodelled("_CreateDirectory");
    }
    std::uintmax_t _RemoveDirectory(const char *) override
    {
        unmodelled("_RemoveDirectory");
    }
    bool LoadFile(const char *, char **, uint32_t *) override
    {
        unmodelled("LoadFile");
    }
    std::unique_ptr<INIFILE> CreateIniFile(const char *, bool) override
    {
        unmodelled("CreateIniFile");
    }
    std::unique_ptr<INIFILE> OpenIniFile(const char *) override
    {
        unmodelled("OpenIniFile");
    }
    void ScanResourcePaths() override
    {
        unmodelled("ScanResourcePaths");
    }
    std::string ConvertPathResource(const char *) override
    {
        unmodelled("ConvertPathResource");
    }
    const std::string *MemoryFile(const char *) override
    {
        unmodelled("MemoryFile");
    }
    uint64_t GetPathFingerprint(const std::filesystem::path &) override
    {
        unmodelled("GetPathFingerprint");
    }
} files;

// A record's extension: the description, its terminator, then the 128×128 picture (empty: no picture).
std::vector<uint8_t> extension(const std::string &description, bool picture = true)
{
    const uint32_t pictureBytes = picture ? thumb::kBytes : 0;
    std::vector<uint8_t> data(8 + description.size() + 1 + pictureBytes);
    const auto word = [&](size_t at, uint32_t n) {
        for (int i = 0; i < 4; ++i)
            data[at + i] = uint8_t(n >> (8 * i));
    };
    word(0, uint32_t(description.size() + 1));
    word(4, pictureBytes);
    std::copy(description.begin(), description.end(), data.begin() + 8);
    if (picture)
    {
        const size_t pixels = 8 + description.size() + 1;
        data[pixels] = 17;
        data[pixels + 1] = 83;
        data[pixels + 2] = 201;
        data[pixels + 3] = 255;
        data.back() = 129;
    }
    return data;
}

// The renderer's textures as the save list's pictures use them.
struct Renderer
{
    std::map<int32_t, std::vector<uint8_t>> textures;
    int32_t next = 0;
    bool fail = false, failLock = false;
    static constexpr int pitch = thumb::kWidth * 4 + 64;
    int32_t TextureCreate(UINT width, UINT height, UINT, uint32_t, D3DFORMAT format, D3DPOOL)
    {
        if (fail)
            return -1;
        REQUIRE(format == D3DFMT_A8R8G8B8);
        textures[next] = std::vector<uint8_t>(size_t(pitch) * height, 0xEE);
        REQUIRE(width * 4 <= pitch);
        return next++;
    }
    bool TextureLock(int32_t id, uint32_t, D3DLOCKED_RECT &rect, uint32_t)
    {
        if (failLock)
            return false;
        rect.Pitch = pitch;
        rect.pBits = textures.at(id).data();
        return true;
    }
    void TextureUnlock(int32_t, uint32_t)
    {
    }
    bool TextureRelease(int32_t id)
    {
        textures.erase(id);
        return true;
    }
};

// The game's save list script: its current profile's files, the selected one, the page (g_nFirstSaveIndex) and the
// selection (g_nCurrentSaveIndex) its events set.
SaveListSnapshot live;
std::map<std::string, std::vector<std::string>> profiles; // the profile folders on disk
int switches = 0;
bool scrolls = true, clicks = true; // the script's events do what they should
DATA &page = fake.scripts.integer("g_nFirstSaveIndex", 0);
DATA &selection = fake.scripts.integer("g_nCurrentSaveIndex", 0);
int32_t value(DATA &data)
{
    int32_t v = -1;
    REQUIRE(data.Get(v));
    return v;
}
void select(int32_t index)
{
    selection.Set(index);
    live.selected = index;
}
const bool scripts = [] {
    fake.messageEvent = [](std::string_view name, MESSAGE &message) -> VDATA * {
        if (name == "ScrollPosChange")
        {
            REQUIRE(message.GetFormat() == "fs");
            const float position = message.Float();
            REQUIRE(message.String() == "SAVESCROLL");
            if (!scrolls)
                return nullptr;
            page.Set(int32_t(int(float(live.files.size() / 5) * position) * 5));
            select(value(page));
            return nullptr;
        }
        REQUIRE(name == "eventSaveClick");
        REQUIRE(message.GetFormat() == "ls");
        REQUIRE(message.Long() == ACTION_MOUSECLICK);
        const std::string node = message.String();
        REQUIRE(node.rfind("SAVEIMG", 0) == 0);
        if (clicks)
            select(value(page) + std::stoi(node.substr(7)) - 1);
        return nullptr;
    };
    return true;
}();
auto rowFacts = [](size_t index) { return std::map<std::string, std::string>{{"row", std::to_string(index)}}; };
} // namespace

VFILE_SERVICE *fio = &files;

namespace storm::mobile
{
std::optional<SaveListSnapshot> ReadSaveList()
{
    return live;
}
bool ChooseSaveProfile(const std::string &profile)
{
    if (!profiles.contains(profile))
        return false;
    ++switches;
    live.profile = profile;
    live.files = profiles[profile];
    live.selected = 0;
    return true;
}
} // namespace storm::mobile

TEST_CASE("a record's picture is read only at its exact size; the description stays readable without it")
{
    for (const std::string description : {"old@*@12:00  01.10.1654@1 hours 0 min.@russian",
                                          "new@*@12:01  01.10.1654@1 hours 1 min.@russian@hero@place"})
    {
        auto bytes = extension(description);
        const auto original = bytes;
        const auto pixels = thumb::Pixels(bytes);
        REQUIRE(pixels.size() == thumb::kBytes);
        CHECK((pixels[0] == 17 && pixels[1] == 83 && pixels[2] == 201 && pixels[3] == 255 && pixels.back() == 129));
        CHECK(bytes == original);
        CHECK_FALSE(thumb::Decode(original).picture_problem);
        // A picture of another size is dropped and reported; the description stays readable.
        bytes.pop_back();
        CHECK(thumb::Pixels(bytes).empty());
        CHECK(thumb::Decode(bytes).valid);
        CHECK(thumb::Decode(bytes).description == description);
        CHECK(thumb::Decode(bytes).picture_problem);
        bytes = original;
        bytes.push_back(0);
        CHECK(thumb::Pixels(bytes).empty());
        CHECK(thumb::Decode(bytes).picture_problem);
        bytes = original;
        bytes[5] ^= 1;
        CHECK(thumb::Pixels(bytes).empty());
        CHECK(thumb::Decode(bytes).description == description);
        CHECK(thumb::Decode(bytes).picture_problem);
        bytes = extension(description, false); // a record without a picture
        CHECK(thumb::Pixels(bytes).empty());
        CHECK(thumb::Decode(bytes).valid);
        CHECK(thumb::Decode(bytes).description == description);
        CHECK_FALSE(thumb::Decode(bytes).picture_problem);
        bytes = original;
        bytes[0] = bytes[1] = bytes[2] = bytes[3] = 255;
        CHECK(thumb::Pixels(bytes).empty());
        bytes = original;
        bytes[8 + description.size()] = 1; // no terminator
        CHECK(thumb::Pixels(bytes).empty());
        CHECK_FALSE(thumb::Decode(bytes).valid);
    }
    CHECK(thumb::Pixels({}).empty());
}

TEST_CASE("a picture's source names the record, its opening and its write time, inside the save folder only")
{
    const auto preview = thumb::Source("SAVE\\player\\Сен-Пьер улицы", 9, 123);
    CHECK(thumb::Path(preview) == "SAVE\\player\\Сен-Пьер улицы");
    CHECK(thumb::Written(preview) == 123u);
    CHECK(preview != thumb::Source("SAVE\\other\\Сен-Пьер улицы", 9, 123));
    CHECK(preview != thumb::Source("SAVE\\player\\Сен-Пьер улицы", 10, 123));
    CHECK(preview != thumb::Source("SAVE\\player\\Сен-Пьер улицы", 9, 124));
    CHECK(thumb::Source("SAVE\\..\\outside", 9, 123).empty());
    CHECK(thumb::Source("SAVE\\player\\../outside", 9, 123).empty());
    CHECK(thumb::Path("save-preview/9/123/2e2e/61").empty());
    CHECK(thumb::Path("save-preview/9/123/61/2f61").empty());
    CHECK(thumb::Path("save-preview/9/123/61/zz").empty());
    CHECK(thumb::Path(preview + "/extra").empty());
    CHECK_FALSE(thumb::Written("save-preview/9/99999999999999999999/61/62"));
}

TEST_CASE("a record's description for the script: valid UTF-8, cut at a code point, the save's bytes untouched")
{
    const std::string legacy = "SAVE\\player\\legacy";
    char output[128];
    for (size_t bytes = 0; bytes < 9; ++bytes)
    {
        fake.saveData[legacy] = std::vector<uint8_t>(bytes, 255);
        output[0] = 'x';
        CHECK_FALSE(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
        CHECK(output[0] == 0);
    }
    auto record = extension("Сен-Пьер@*@12:00  01.10.1654@1 hours 0 min.@russian");
    fake.saveData[legacy] = record;
    CHECK(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
    CHECK(std::string(output).starts_with("Сен-Пьер"));
    CHECK(fake.saveData[legacy] == record);
    char small[4];
    CHECK(storm::ReadSaveDescription(legacy.c_str(), sizeof(small), small));
    CHECK(std::string(small) == "С");
    char one = 'x';
    CHECK_FALSE(storm::ReadSaveDescription(legacy.c_str(), 1, &one));
    CHECK(one == 0);
    CHECK_FALSE(storm::ReadSaveDescription(nullptr, sizeof(output), output));
    output[0] = 'x';
    CHECK_FALSE(storm::ReadSaveDescription(legacy.c_str(), 0, output)); // no room: not even a terminator
    CHECK(output[0] == 'x');
    CHECK_FALSE(storm::ReadSaveDescription(legacy.c_str(), 2, nullptr));
    fake.saveData[legacy] = extension("Сен-Пьер@*@12:00  01.10.1654@1 hours 0 min.@russian", false);
    CHECK(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
    CHECK(std::string(output).starts_with("Сен-Пьер"));
    const auto invalid = extension(std::string("a") + char(255) + "b");
    fake.saveData[legacy] = invalid;
    CHECK(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
    CHECK(utf8::IsValidUtf8(output));
    CHECK(fake.saveData[legacy] == invalid);
    for (int kind = 0; kind < 4; ++kind)
    {
        INFO(kind);
        auto bad = extension("description");
        if (kind == 0)
            bad[0] = bad[1] = bad[2] = bad[3] = 255;
        if (kind == 1)
            bad[8 + 11] = 1; // no description terminator
        if (kind == 2)
            bad.pop_back();
        if (kind == 3)
            bad.push_back(0);
        fake.saveData[legacy] = bad;
        output[0] = 'x';
        const int before = errors;
        if (kind < 2)
        {
            CHECK_FALSE(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
            CHECK(output[0] == 0);
        }
        else // a picture of another size: reported, the description (and so loading) kept
        {
            CHECK(storm::ReadSaveDescription(legacy.c_str(), sizeof(output), output));
            CHECK(std::string(output) == "description");
        }
        CHECK(errors == before + 1);
    }
    fake.saveData.erase(legacy);
}

TEST_CASE("every record with a readable description gets a picture texture, an empty one without a picture")
{
    Renderer renderer;
    char path[] = "SAVE\\player\\shot";
    char *text = nullptr;
    const auto pictured = extension("pictured@*@12:00  01.10.1654@1 hours 0 min.@russian");
    fake.saveData[path] = pictured;
    int32_t id = storm::SaveRecordTexture(&renderer, path, &text);
    REQUIRE(id >= 0);
    REQUIRE(text);
    CHECK(std::string(text).starts_with("pictured@"));
    CHECK(renderer.textures[id][0] == 17);
    CHECK(renderer.textures[id][3] == 255);
    CHECK(renderer.textures[id][size_t(Renderer::pitch) * (thumb::kHeight - 1) + thumb::kWidth * 4 - 1] == 129);
    CHECK(renderer.textures[id][thumb::kWidth * 4] == 0xEE); // the row's padding is not the picture's
    delete[] text;
    renderer.TextureRelease(id);

    fake.saveData[path] = extension("pictured@*@12:00  01.10.1654@1 hours 0 min.@russian", false);
    id = storm::SaveRecordTexture(&renderer, path, &text);
    REQUIRE(id >= 0);
    REQUIRE(text);
    CHECK(std::string(text).starts_with("pictured@"));
    bool empty = true;
    for (uint32_t row = 0; row < thumb::kHeight; ++row)
        for (uint32_t x = 0; x < thumb::kWidth * 4; ++x)
            empty = empty && renderer.textures[id][size_t(row) * Renderer::pitch + x] == 0;
    CHECK(empty);
    delete[] text;
    renderer.TextureRelease(id);

    auto broken = pictured;
    broken[0] = broken[1] = broken[2] = broken[3] = 255;
    fake.saveData[path] = broken;
    const int before = errors;
    CHECK(storm::SaveRecordTexture(&renderer, path, &text) == -1); // no description: corrupted, said
    CHECK_FALSE(text);
    CHECK(errors == before + 1);
    CHECK(renderer.textures.empty());
    fake.saveData[path] = pictured;
    renderer.fail = true;
    CHECK(storm::SaveRecordTexture(&renderer, path, &text) == -1);
    CHECK_FALSE(text);
    CHECK(errors == before + 2);
    renderer.fail = false;
    renderer.failLock = true; // made but not writable: released, not handed out
    CHECK(storm::SaveRecordTexture(&renderer, path, &text) == -1);
    CHECK_FALSE(text);
    CHECK(renderer.textures.empty());
    CHECK(errors == before + 3);
    renderer.failLock = false;
    fake.saveData.erase(path);
    CHECK(storm::SaveRecordTexture(&renderer, path, &text) == -1);
    CHECK_FALSE(text);
}

TEST_CASE("a record's facts are read once per write time of its file")
{
    fake.saveDataReads = 0;
    const std::string record = "SAVE\\player\\old";
    fake.saveData[record] = extension("old@*@12:00  01.10.1654@1 hours 0 min.@russian");
    files.stamps[record] = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(123));
    SaveFactsCache cache;
    const auto &first = cache.get(record);
    CHECK(first.present);
    CHECK(first.thumbnail);
    CHECK(first.readable);
    CHECK(first.facts.location == "old");
    CHECK(first.facts.playMinutes == 60);
    CHECK_FALSE(first.facts.created.empty());
    CHECK(first.written == 123u);
    CHECK(fake.saveDataReads == 1);
    cache.get(record);
    CHECK(fake.saveDataReads == 1);

    files.stamps[record] += std::filesystem::file_time_type::duration(1);
    fake.saveData[record] = extension("new@*@12:01  01.10.1654@1 hours 1 min.@russian@hero@place");
    const auto &changed = cache.get(record);
    CHECK(changed.thumbnail);
    CHECK(changed.facts.hero == "hero");
    CHECK(fake.saveDataReads == 2);

    files.stamps[record] += std::filesystem::file_time_type::duration(1);
    fake.saveData[record] = std::vector<uint8_t>{255, 255, 255, 255, 0, 0, 1, 0, 'x'};
    int before = errors;
    const auto &bad = cache.get(record);
    CHECK(bad.present);
    CHECK_FALSE(bad.thumbnail);
    CHECK_FALSE(bad.readable);
    CHECK(bad.facts.location.empty());
    CHECK(fake.saveDataReads == 3);
    CHECK(errors == before + 1);

    files.stamps[record] += std::filesystem::file_time_type::duration(1);
    fake.saveData[record] = extension("text only@*@12:00  01.10.1654@1 hours 0 min.@russian", false);
    const auto &descriptionOnly = cache.get(record);
    CHECK_FALSE(descriptionOnly.thumbnail);
    CHECK(descriptionOnly.readable);
    CHECK(descriptionOnly.facts.location == "text only");
    CHECK(fake.saveDataReads == 4);

    fake.saveData.erase(record);
    CHECK_FALSE(cache.get(record).present);
    CHECK(fake.saveDataReads == 4); // a missing file is not read
    // A file written at the clock's epoch and then removed is not still there.
    files.stamps[record] = {};
    fake.saveData[record] = extension("epoch@*@12:00  01.10.1654@1 hours 0 min.@russian");
    CHECK(cache.get(record).present);
    fake.saveData.erase(record);
    CHECK(cache.get(record).facts.location.empty());
    CHECK(fake.saveDataReads == 5);
}

TEST_CASE("a description gives the hero, the location, the game's date and time and the play time")
{
    auto f = ParseSaveDescription(
        "Сен-Пьер магазин 1@face_1,*@13:25  01.10.1654@0 hours 25 min.@russian@Шарль де Мор@Сен-Пьер магазин");
    CHECK(f.hero == "Шарль де Мор");
    CHECK(f.location == "Сен-Пьер магазин");
    CHECK(f.gameDate == "1654-10-01");
    CHECK(f.gameTime == "13:25");
    CHECK(f.playMinutes == 25);
    f = ParseSaveDescription("Мартиника - море@*@09:05  07.10.1654@2days - 50 hours 3 min.@russian");
    CHECK(f.hero.empty());
    CHECK(f.location == "Мартиника - море");
    CHECK(f.gameDate == "1654-10-07");
    CHECK(f.playMinutes == 50 * 60 + 3);
    CHECK(SavePlayMinutes("1days - 2 hours 0 min.") == 26 * 60);
    CHECK(SavePlayMinutes("garbage") == -1);
    f = ParseSaveDescription("broken");
    CHECK(f.location.empty());
    CHECK(f.playMinutes == -1);
    f = ParseSaveDescription("x@*@12:00  01.13.1654@1 hours 1 min.@russian");
    CHECK(f.gameDate.empty());
    f = ParseSaveDescription("Label@*@12:00  01.10.1654@1 hours 1 min.@russian@hero@");
    CHECK(f.location == "Label");
    CHECK(f.hero == "hero");
    f = ParseSaveDescription("x@*@99:99  45.13.1654@1 hours 1 min.@russian");
    CHECK(f.gameDate.empty());
    CHECK(f.location == "x");
}

TEST_CASE("the save list: stable rows, actions only for the list they were made for, slots chosen through the "
          "game's script")
{
    switches = 0;
    SaveListCapability cap;
    live = {};
    live.revision = 4;
    live.profile = "existing";
    live.saving = true;
    for (int i = 0; i < 37; ++i)
        live.files.push_back("Save " + std::to_string(i));
    profiles["existing"] = live.files;
    const auto own = [&] {
        live.records.clear();
        for (const auto &file : live.files)
            live.records.push_back({live.profile, file});
    };
    own();
    std::vector<MobileWidget> rows;
    cap.collect(live, true, rows, rowFacts);
    REQUIRE(rows.size() == 39);
    CHECK(rows[0].node == "ANDROID_SAVES");
    CHECK(rows[0].value == "37");
    rows.erase(rows.begin()); // the list, then the records
    CHECK(rows[37].unavailableReason == "new");
    CHECK(rows[37].context.at("row") == "37");
    CHECK(rows[3].context.at("row") == "3");
    CHECK(rows[37].actions == std::vector<std::string>{"activate"});
    CHECK(rows[3].actions == std::vector<std::string>{"activate", "delete"});
    const auto token = [&](int index, const char *verb) { return std::string(verb) + "@4:" + rows[index].rowId; };

    // A distant slot: the page scrolled, then the slot clicked.
    auto target = cap.accept(live, 31, token(31, "delete"));
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(value(page) == 30);
    CHECK(value(selection) == 31);
    select(34); // the script's delayed initial selection while the confirmation is open
    CHECK(RestoreSaveTarget(*target));
    CHECK(value(selection) == 31);
    live.records[31].file = "replacement";
    CHECK_FALSE(RestoreSaveTarget(*target));
    CHECK_FALSE(cap.accept(live, 31, token(31, "delete")));
    own();
    live.revision = 5;
    CHECK_FALSE(RestoreSaveTarget(*target));
    live.revision = 4;
    page.Set(0);
    CHECK_FALSE(RestoreSaveTarget(*target)); // the slot is not on the script's page
    page.Set(30);

    target = cap.accept(live, 0, token(0, "activate"));
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(value(page) == 0);
    CHECK(value(selection) == 0);
    target = cap.accept(live, 37, token(37, "activate")); // the new slot
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(value(page) == 35);
    CHECK(value(selection) == 37);
    // The script did not get there: no target.
    target = cap.accept(live, 3, token(3, "activate"));
    REQUIRE(target);
    scrolls = false;
    CHECK_FALSE(SelectSaveTarget(*target));
    scrolls = true;
    clicks = false;
    CHECK_FALSE(SelectSaveTarget(*target));
    clicks = true;
    CHECK(SelectSaveTarget(*target));
    CHECK_FALSE(cap.accept(live, 37, token(37, "delete")));
    CHECK_FALSE(cap.accept(live, 2, token(1, "delete")));
    CHECK(switches == 0);

    // The same list keeps its rows' identities.
    std::vector<MobileWidget> again;
    cap.collect(live, true, again, rowFacts);
    CHECK(again[4].rowId == rows[3].rowId);
    rows.clear();
    cap.collect(live, false, rows, rowFacts);
    for (const auto &row : rows)
    {
        CHECK_FALSE(row.enabled);
        CHECK(row.actions.empty());
    }
}

TEST_CASE("loading lists every profile's records; a record of another profile makes that profile current first")
{
    switches = 0;
    SaveListCapability cap;
    live = {};
    live.profile = "existing";
    live.files = profiles["existing"] = {"Save 0", "Save 1"};
    profiles["old build"] = {"Old 0", "Old 1", "Old 2"};
    live.revision = 6;
    select(0);
    live.records = {{"old build", "Old 0"},
                    {"existing", "Save 0"},
                    {"old build", "Old 1"},
                    {"existing", "Save 1"},
                    {"old build", "Old 2"}};
    std::vector<MobileWidget> rows;
    cap.collect(live, true, rows, rowFacts);
    rows.erase(rows.begin());
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].label == "Old 0");
    CHECK(rows[1].label == "Save 0");
    CHECK(rows[1].value == "1");
    CHECK(rows[0].value == "0");
    const auto token = [&](int index, const char *verb) { return std::string(verb) + "@6:" + rows[index].rowId; };

    auto target = cap.accept(live, 2, token(2, "activate"));
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(switches == 1);
    CHECK(live.profile == "old build");
    CHECK(value(selection) == 1);
    CHECK(RestoreSaveTarget(*target));
    CHECK(value(selection) == 1);

    // Its own profile current again: a record of it needs no switch.
    ChooseSaveProfile("existing");
    switches = 0;
    target = cap.accept(live, 3, token(3, "delete"));
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(switches == 0);
    CHECK(value(selection) == 1);
    CHECK(cap.accept(live, 4, token(4, "delete")));
    CHECK_FALSE(cap.accept(live, 5, token(4, "activate")));

    // The game's profile name may be spelled unlike its folder ("Player" for "player"): ASCII case is folded.
    CHECK(SameProfile("Player", "player"));
    CHECK_FALSE(SameProfile("Player", "player2"));
    CHECK_FALSE(SameProfile("Плеер", "плеер"));
    live.profile = "EXISTING";
    target = cap.accept(live, 3, token(3, "delete"));
    REQUIRE(target);
    CHECK(SelectSaveTarget(*target));
    CHECK(switches == 0);
    CHECK(value(selection) == 1);
    CHECK(RestoreSaveTarget(*target));
    live.profile = "existing";

    // A record of the same name in another profile is not the current profile's record.
    profiles["old build"].push_back("Save 0");
    live.records.push_back({"old build", "Save 0"});
    rows.clear();
    cap.collect(live, true, rows, rowFacts);
    rows.erase(rows.begin());
    target = cap.accept(live, 5, std::string("delete@6:") + rows[5].rowId);
    REQUIRE(target);
    CHECK_FALSE(RestoreSaveTarget(*target));
    live.records.pop_back();
    profiles["old build"].pop_back();

    // A record whose profile folder is gone cannot be chosen.
    live.records.push_back({"vanished", "X"});
    rows.clear();
    cap.collect(live, true, rows, rowFacts);
    rows.erase(rows.begin());
    target = cap.accept(live, 5, std::string("activate@6:") + rows[5].rowId);
    REQUIRE(target);
    CHECK_FALSE(SelectSaveTarget(*target));

    live.records.clear();
    live.files.clear();
    rows.clear();
    cap.collect(live, true, rows, rowFacts);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].node == "ANDROID_SAVES");
    CHECK(rows[0].value == "0");
}

TEST_CASE("one SetCurrentProfile per opening: the game's own or the chooser's answer, never both")
{
    const auto opening = [](bool chooser, const std::string &game, bool listed, const std::string &newest) {
        int sets = chooser ? 0 : 1;
        std::optional<std::string> current = chooser ? std::nullopt : std::optional<std::string>(game);
        for (int frame = 0; frame < 5; ++frame)
        {
            const std::string target = ProfileToChoose(chooser, game, listed, newest, current);
            if (!target.empty())
            {
                ++sets;
                current = target;
                chooser = false;
            }
        }
        return std::make_pair(sets, current);
    };
    CHECK(opening(false, "player", false, "QA_Skip") == std::make_pair(1, std::optional<std::string>("player")));
    CHECK(opening(false, "player", true, "QA_Skip") == std::make_pair(1, std::optional<std::string>("player")));
    CHECK(opening(true, "player", true, "QA_Skip") == std::make_pair(1, std::optional<std::string>("player")));
    CHECK(opening(true, "player", false, "QA_Skip") == std::make_pair(1, std::optional<std::string>("QA_Skip")));
    CHECK(opening(true, "player", false, "").first == 0);
    // After a record of another profile was deleted the opening's profile is made current again, once.
    CHECK(ProfileToChoose(false, "player", true, "x", std::string("QA_Skip")) == "player");
    CHECK(ProfileToChoose(false, "Player", true, "x", std::string("player")).empty());
    CHECK(ProfileToChoose(false, "player", false, "x", std::string("QA_Skip")).empty());
    CHECK(ProfileToChoose(false, "player", true, "x", std::nullopt).empty());
}
