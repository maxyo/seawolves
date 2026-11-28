// A save is one write of the whole record (CoreImpl::SaveState, SetSaveData): what the slot holds when the save
// stops at each step. A load checks the whole file before the running game is erased (CoreImpl::ReadStateFile):
// which records it refuses, and that the game runs on after a refusal. A script call cut at its limit ends the
// session, and no save is made after it.
#include "compiler.h"
#include "core_impl.h"
#include "game_profile.hpp"
#include "load_watch.hpp"
#include "platform/storage.hpp"
#include "save_file.hpp"
#include "save_state.hpp"
#include "save_trash.hpp"
#include "vma.hpp"

#include <catch2/catch.hpp>

#include <csignal>
#include <random>
#include <sys/resource.h>

namespace fs = std::filesystem;
using storm::save::Bytes;

namespace
{
fs::path SavesFolder()
{
    return fs::temp_directory_path() / "saves";
}

constexpr char kProgram[] = R"(
int gCounter = 7;
string gFailedRecord = "";
string gFailedReason = "";

#event_handler("AndroidSaveFailed", "AndroidSaveFailed");

void AndroidSaveFailed()
{
	gFailedRecord = GetEventData();
	gFailedReason = GetEventData();
}

string gLoadFailedRecord = "";
string gLoadFailedReason = "";

#event_handler("AndroidLoadFailed", "AndroidLoadFailed");

void AndroidLoadFailed()
{
	gLoadFailedRecord = GetEventData();
	gLoadFailedReason = GetEventData();
}

void OnLoad()
{
}

int Bump()
{
	gCounter = gCounter + 1;
	return gCounter;
}

int Spin()
{
	while (true)
	{
		gCounter = gCounter + 1;
	}
	return 0;
}
)";

void ChooseGame()
{
    static bool chosen = false;
    if (chosen)
        return;
    chosen = true;
    for (auto *c : __STORM_CLASSES_REGISTRY) // as CoreImpl::Init: FindVMA looks classes up by hash
        c->SetHash(MakeHashValue(c->GetName()));
    const fs::path game = fs::temp_directory_path() / "game";
    fs::create_directories(game / "PROGRAM");
    std::ofstream(game / "PROGRAM" / "main.c") << kProgram;
    fs::current_path(game);
    storm::setGameProfile(storm::GameProfile::Legacy);
    storm::storage::select({.game = game, .saves = SavesFolder()});
    fio->ScanResourcePaths();
}

// The program above in a compiler of its own, with an empty saves folder.
struct Game
{
    const bool chosen = (ChooseGame(), true); // the game's folders before the compiler exists
    std::vector<std::string> diagnostics;
    COMPILER compiler;

    Game()
    {
        fs::remove_all(SavesFolder());
        fs::create_directories(SavesFolder() / "Hero");
        core_internal.Compiler = &compiler;
        compiler.ConfigureCompileCheck(&diagnostics, true);
        compiler.SetProgramDirectory("PROGRAM");
        REQUIRE(compiler.BC_LoadSegment("main.c"));
    }
    ~Game()
    {
        core_internal.ProcessSaveLoad(); // nothing of this case waits in the core
        storm::save::trash().rollback();
        compiler.Release();
        core_internal.Compiler = nullptr;
    }

    std::string Text(const char *variable)
    {
        return static_cast<VDATA *>(core_internal.GetScriptVariable(variable))->GetString();
    }
    int Run(const char *function)
    {
        std::string value;
        REQUIRE(compiler.RunScriptFunction(function, value));
        return std::stoi(value);
    }
};

constexpr char kSlot[] = "SAVE\\Hero\\Port Royal";

fs::path SlotFile()
{
    return SavesFolder() / "Hero" / "Port Royal";
}

Bytes Description(unsigned seed, size_t size = 3000)
{
    Bytes data(size);
    std::mt19937 random(seed);
    for (char &byte : data)
        byte = static_cast<char>(random());
    return data;
}

Bytes File(const fs::path &path)
{
    Bytes data;
    std::string error;
    REQUIRE(storm::save::readFile(path, data, error));
    return data;
}

bool Save(const Bytes &description)
{
    Bytes data = description;
    return core_internal.SaveState(kSlot) &&
           core_internal.SetSaveData(kSlot, data.data(), static_cast<int32_t>(data.size()));
}

// The record with this description is whole: its state and its description both read back.
bool Whole(const fs::path &path, const Bytes &description)
{
    const Bytes file = File(path);
    Bytes raw, data;
    std::string error;
    return storm::save::readState(file, raw, error) && storm::save::readExtData(file, data, error) &&
           data == description;
}

// The record's file with another head of its state: the program directory and the segments it names.
void RewriteProgram(const std::optional<std::string> &directory, const std::vector<std::string> &segments)
{
    const Bytes file = File(SlotFile());
    Bytes raw, description;
    std::string error;
    REQUIRE(storm::save::readState(file, raw, error));
    REQUIRE(storm::save::readExtData(file, description, error));
    // The head as the state lays it out (save_state.hpp): directory, name codec, segments.
    storm::save::StateReader in(raw.data(), raw.size());
    std::optional<std::string> text;
    uint32_t count = 0;
    REQUIRE(in.String(text));
    const size_t codec = in.Position();
    REQUIRE(in.VDword(count));
    for (uint32_t n = 0; n < count; ++n)
        REQUIRE(in.String(text));
    const size_t codec_end = in.Position();
    REQUIRE(in.VDword(count));
    for (uint32_t n = 0; n < count; ++n)
        REQUIRE(in.String(text));
    const size_t variables = in.Position();

    Bytes changed;
    const auto string = [&](const std::string &value) {
        changed.push_back(static_cast<char>(value.size() + 1));
        changed.insert(changed.end(), value.c_str(), value.c_str() + value.size() + 1);
    };
    if (directory)
        string(*directory);
    else
        changed.push_back(0);
    changed.insert(changed.end(), raw.begin() + codec, raw.begin() + codec_end);
    changed.push_back(static_cast<char>(segments.size()));
    for (const std::string &segment : segments)
        string(segment);
    changed.insert(changed.end(), raw.begin() + variables, raw.end());

    Bytes image;
    REQUIRE(storm::save::buildState("save", changed.data(), static_cast<uint32_t>(changed.size()), image, error));
    REQUIRE(storm::save::appendExtData(image, "save", description.data(), static_cast<uint32_t>(description.size()),
                                       error));
    std::ofstream(SlotFile(), std::ios::binary | std::ios::trunc).write(image.data(), image.size());
}

// The record's file with its state cut to `size` bytes.
void CutState(size_t size)
{
    Bytes raw, image;
    std::string error;
    REQUIRE(storm::save::readState(File(SlotFile()), raw, error));
    REQUIRE(size < raw.size());
    REQUIRE(storm::save::buildState("save", raw.data(), static_cast<uint32_t>(size), image, error));
    std::ofstream(SlotFile(), std::ios::binary | std::ios::trunc).write(image.data(), image.size());
}

size_t StateSize()
{
    Bytes raw;
    std::string error;
    REQUIRE(storm::save::readState(File(SlotFile()), raw, error));
    return raw.size();
}

// What the save folder shows to a session that starts now (FILE_SERVICE lists a folder after Trash::recover).
std::vector<std::string> ListedByNextSession()
{
    storm::save::Trash next;
    next.recover(SavesFolder() / "Hero");
    std::vector<std::string> names;
    for (const auto &entry : fs::directory_iterator(SavesFolder() / "Hero"))
        if (!storm::save::isReservedName(entry.path().filename().string()))
            names.push_back(entry.path().filename().string());
    return names;
}
} // namespace

TEST_CASE("a save is the bytes the state write followed by the description rewrite produced")
{
    Game game;
    const Bytes description = Description(1);
    REQUIRE(Save(description));
    const Bytes file = File(SlotFile());

    Bytes raw, composed;
    std::string error;
    REQUIRE(storm::save::readState(file, raw, error));
    REQUIRE(storm::save::buildState("save", raw.data(), static_cast<uint32_t>(raw.size()), composed, error));
    REQUIRE(storm::save::appendExtData(composed, "save", description.data(), static_cast<uint32_t>(description.size()),
                                       error));
    REQUIRE((file == composed));
    REQUIRE(ListedByNextSession() == std::vector<std::string>{"Port Royal"});
    REQUIRE(!storm::platform::saveInProgress());
}

TEST_CASE("a process that dies between the state and the description keeps the previous save")
{
    Game game;
    const Bytes previous_description = Description(2);
    REQUIRE(Save(previous_description));
    const Bytes previous = File(SlotFile());

    SECTION("the scripts replace the slot in place")
    {
        game.Run("Bump");
        REQUIRE(core_internal.SaveState(kSlot));
        CHECK(storm::platform::saveInProgress());
        CHECK((File(SlotFile()) == previous));
    }
    SECTION("the scripts delete the slot first")
    {
        REQUIRE(fio->_DeleteFile(kSlot));
        game.Run("Bump");
        REQUIRE(core_internal.SaveState(kSlot));
        CHECK(!fs::exists(SlotFile()));
    }
    REQUIRE(ListedByNextSession() == std::vector<std::string>{"Port Royal"});
    REQUIRE((File(SlotFile()) == previous));
}

TEST_CASE("a save that fails keeps the previous save and tells the scripts")
{
    Game game;
    const Bytes previous_description = Description(3);
    REQUIRE(Save(previous_description));
    const Bytes previous = File(SlotFile());
    REQUIRE(fio->_DeleteFile(kSlot)); // as the game's save screen replaces a slot
    game.Run("Bump");

    SECTION("the description never comes")
    {
        REQUIRE(core_internal.SaveState(kSlot));
    }
    SECTION("the description is empty")
    {
        REQUIRE(core_internal.SaveState(kSlot));
        Bytes none(1);
        CHECK(!core_internal.SetSaveData(kSlot, none.data(), 0));
    }
    SECTION("the file cannot grow to the record's size: a short write, then a refused one")
    {
        Bytes description = Description(4, 64 * 1024);
        REQUIRE(core_internal.SaveState(kSlot));
        rlimit before{};
        REQUIRE(getrlimit(RLIMIT_FSIZE, &before) == 0);
        std::signal(SIGXFSZ, SIG_IGN);
        const rlimit small{4096, before.rlim_max};
        REQUIRE(setrlimit(RLIMIT_FSIZE, &small) == 0);
        const bool saved =
            core_internal.SetSaveData(kSlot, description.data(), static_cast<int32_t>(description.size()));
        REQUIRE(setrlimit(RLIMIT_FSIZE, &before) == 0);
        CHECK(!saved);
    }
    SECTION("the description names another record")
    {
        REQUIRE(core_internal.SaveState(kSlot));
        Bytes description = Description(5);
        CHECK(!core_internal.SetSaveData("SAVE\\Hero\\Tortuga", description.data(),
                                         static_cast<int32_t>(description.size())));
        CHECK(!fs::exists(SavesFolder() / "Hero" / "Tortuga"));
    }
    core_internal.ProcessSaveLoad();
    CHECK(!storm::platform::saveInProgress());
    CHECK(game.Text("gFailedRecord") == kSlot);
    CHECK(!game.Text("gFailedReason").empty());
    CHECK(ListedByNextSession() == std::vector<std::string>{"Port Royal"});
    CHECK((File(SlotFile()) == previous));
    CHECK(!fs::exists(storm::save::withSuffix(SlotFile(), storm::save::kTempSuffix)));
}

TEST_CASE("a description without a state changes no record")
{
    Game game;
    const Bytes description = Description(6);
    REQUIRE(Save(description));
    const Bytes previous = File(SlotFile());
    Bytes other = Description(7);
    CHECK(!core_internal.SetSaveData(kSlot, other.data(), static_cast<int32_t>(other.size())));
    CHECK((File(SlotFile()) == previous));
}

TEST_CASE("the save that replaces a slot makes the slot's delete final")
{
    Game game;
    REQUIRE(Save(Description(8)));
    REQUIRE(fio->_DeleteFile(kSlot));
    game.Run("Bump");
    const Bytes description = Description(9);
    REQUIRE(Save(description));
    REQUIRE(Whole(SlotFile(), description));
    REQUIRE(!fs::exists(storm::save::withSuffix(SlotFile(), storm::save::kDeletedSuffix)));
}

// A load the player asked for: LoadEngineState, then the frame's step. True when the load was accepted.
bool Load()
{
    storm::loadWatch().Requested(kSlot);
    if (!core_internal.InitiateStateLoading(kSlot))
        return false;
    core_internal.ProcessSaveLoad();
    return true;
}

TEST_CASE("a save of this program loads")
{
    Game game;
    REQUIRE(Save(Description(10)));
    SECTION("as written")
    {
    }
    SECTION("with its program directory spelled another way")
    {
        RewriteProgram("program/", {"MAIN.C"});
    }
    REQUIRE(game.Run("Bump") == 8);
    REQUIRE(Load());
    REQUIRE(game.Run("Bump") == 8);
    REQUIRE(game.compiler.BC_SegmentIsLoaded("main.c"));
}

TEST_CASE("a save the running program cannot take is refused with the game still running")
{
    Game game;
    REQUIRE(Save(Description(11)));
    const size_t size = StateSize();
    REQUIRE(game.Run("Bump") == 8);
    bool late = false; // the file changes between LoadEngineState and the frame that applies it

    SECTION("another program directory")
    {
        RewriteProgram("OTHER\\", {"main.c"});
    }
    SECTION("no program directory")
    {
        RewriteProgram(std::nullopt, {"main.c"});
    }
    SECTION("a directory outside the game")
    {
        RewriteProgram("..\\..\\elsewhere\\", {"main.c"});
    }
    SECTION("a segment the game does not run")
    {
        RewriteProgram("PROGRAM\\", {"missing.c"});
    }
    SECTION("a segment outside the program")
    {
        RewriteProgram("PROGRAM\\", {"main.c", "..\\..\\elsewhere.c"});
    }
    SECTION("no segment")
    {
        RewriteProgram("PROGRAM\\", {});
    }
    SECTION("a state cut inside its head")
    {
        CutState(40);
    }
    SECTION("a state cut inside its variables")
    {
        CutState(size - size / 3);
    }
    SECTION("a state cut before its last byte")
    {
        CutState(size - 1);
    }
    SECTION("a file cut inside the packed state")
    {
        fs::resize_file(SlotFile(), sizeof(storm::save::Header) + 2 * sizeof(uint32_t) + 20);
    }
    SECTION("a segment the game does not run, in a file that changed after the load was accepted")
    {
        storm::loadWatch().Requested(kSlot);
        REQUIRE(core_internal.InitiateStateLoading(kSlot));
        RewriteProgram("PROGRAM\\", {"missing.c"});
        core_internal.ProcessSaveLoad();
        late = true;
    }
    if (!late)
    {
        CHECK(!Load());
        core_internal.ProcessSaveLoad();
    }
    CHECK(game.Text("gLoadFailedRecord") == kSlot);
    CHECK(!game.Text("gLoadFailedReason").empty());
    CHECK(game.Run("Bump") == 9); // the program and its state are as they were
}

TEST_CASE("a script call over its limit stops the scripts and every save after it")
{
    Game game;
    REQUIRE(Save(Description(12)));
    const Bytes previous = File(SlotFile());
    game.compiler.SetScriptCallLimit(1 << 20);
    REQUIRE(game.Run("Bump") == 8);
    REQUIRE(game.compiler.ScriptsStopped().empty());
    REQUIRE_NOTHROW(core_internal.EndStoppedSession());

    std::string value;
    CHECK(!game.compiler.RunScriptFunction("Spin", value));
    // The session ends with the reason: the frame loop shows what its frame throws.
    CHECK(!game.compiler.ScriptsStopped().empty());
    CHECK_THROWS_WITH(core_internal.EndStoppedSession(), Catch::Matchers::Contains("did not finish"));
    // The half-run program neither runs on nor is saved.
    CHECK(!game.compiler.RunScriptFunction("Bump", value));
    CHECK(!Save(Description(13)));
    core_internal.ProcessSaveLoad();
    CHECK((File(SlotFile()) == previous));
    CHECK(ListedByNextSession() == std::vector<std::string>{"Port Royal"});
}
