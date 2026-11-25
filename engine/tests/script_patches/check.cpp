// Host checks for platform/script_patches.hpp: apply, base mismatch, all-or-nothing
// fixes, set selection, damaged layers, cache invalidation and memory-only results.
#include "platform/path_fingerprint.hpp"
#include "platform/resource_overlay.hpp"
#include "platform/script_patches.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using namespace storm::script_patches;
using Paths = std::unordered_map<std::string, std::string>;

namespace
{
void write(const fs::path &path, std::string_view data)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}

std::string read(const fs::path &path)
{
    std::string data;
    REQUIRE(detail::readFile(path, data));
    return data;
}

bool throwsLayerError(const std::function<void()> &call, std::string_view expected)
{
    try
    {
        call();
    }
    catch (const LayerError &error)
    {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos)
            return true;
        std::cerr << "unexpected LayerError: " << error.what() << "\n";
        return false;
    }
    return false;
}

// The player's game: CRLF scripts, as the real games ship them.
const std::string kIdentity = "#define VERSION \"1.0\"\r\n";
const std::string kDialog = "void A()\r\n{\r\n\tSetTimeScale(0.0);\r\n\tB();\r\n}\r\n";
const std::string kDialogFixed = "void A()\r\n{\r\n\tSetTimeScale(0.0);\r\n\tResetCounter();\r\n\tB();\r\n}\r\n";
const std::string kDialogPatch = "--- a/PROGRAM/dialog.c\n+++ b/PROGRAM/dialog.c\n@@ -2,3 +2,4 @@\n {\r\n"
                                 " \tSetTimeScale(0.0);\r\n+\tResetCounter();\r\n \tB();\r\n";
const std::string kQuest = "int Q()\r\n{\r\n\treturn 1;\r\n}";
const std::string kQuestFixed = "int Q()\r\n{\r\n\treturn 2;\r\n}";
const std::string kQuestPatch = "--- a/PROGRAM/quests/q.c\n+++ b/PROGRAM/quests/q.c\n@@ -3,2 +3,2 @@\n-\treturn 1;\r\n"
                                "+\treturn 2;\r\n }\n\\ No newline at end of file\n";
const std::string kSea = "float S = 1.0;\n";
const std::string kSeaFixed = "float S = 2.0;\n";
const std::string kSeaPatch =
    "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-float S = 1.0;\n+float S = 2.0;\n";

struct Fixture
{
    fs::path game, layer;
    Paths paths;
    Derived derived;

    explicit Fixture(const fs::path &root) : game(root / "game"), layer(root / "layer")
    {
        fs::remove_all(root);
        write(game / "PROGRAM/version.h", kIdentity);
        write(game / "PROGRAM/dialog.c", kDialog);
        write(game / "PROGRAM/quests/q.c", kQuest);
        write(game / "PROGRAM/sea_ai/s.c", kSea);
        write(layer / "sets/test-1.0/dialog.patch", kDialogPatch);
        write(layer / "sets/test-1.0/q.patch", kQuestPatch);
        write(layer / "sets/test-1.0/s.patch", kSeaPatch);
        writeIndex();
        reindex();
    }

    // A fix that touches two scripts (time-scale: dialog.c + quests/q.c) and an
    // independent one (sea).
    void writeIndex(const std::string &extra = "")
    {
        write(layer / "sets/test-1.0/index.txt",
              "storm-script-patches\t1\n"
              "set\ttest-1.0\tTest Game\t1.0\n"
              "identify\tPROGRAM/version.h\t" +
                  sha256(kIdentity) +
                  "\n"
                  "target\tPROGRAM/dialog.c\t" +
                  sha256(kDialog) + "\t" + sha256(kDialogFixed) +
                  "\tdialog.patch\n"
                  "target\tPROGRAM/quests/q.c\t" +
                  sha256(kQuest) + "\t" + sha256(kQuestFixed) +
                  "\tq.patch\n"
                  "target\tPROGRAM/sea_ai/s.c\t" +
                  sha256(kSea) + "\t" + sha256(kSeaFixed) +
                  "\ts.patch\n"
                  "fix\ttime-scale\tPROGRAM/dialog.c,PROGRAM/quests/q.c\tGPL-3.0\thttps://example.org/src\tTime scale\n"
                  "fix\tsea\tPROGRAM/sea_ai/s.c\tGPL-3.0\thttps://example.org/src\tSea\n" +
                  extra);
    }

    // As the file service's resource index: lowercase relative key -> real path.
    void reindex()
    {
        paths.clear();
        for (const auto &entry : fs::recursive_directory_iterator(game))
            if (entry.is_regular_file())
                paths[storm::AsciiLower(entry.path().lexically_relative(game).generic_string())] =
                    entry.path().string();
    }

    Report apply()
    {
        return applyLayer(layer, paths, derived);
    }
    // As FILE_SERVICE: a patched script from memory, any other file from disk.
    std::string resolved(const std::string &key)
    {
        const auto &path = paths.at(key);
        const auto memory = derived.find(path);
        return memory != derived.end() ? memory->second : read(path);
    }
};

void checkSha256()
{
    REQUIRE(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(sha256(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

void checkWhitelist()
{
    for (const char *path :
         {"PROGRAM/dialog.c", "program/quests/quests_reaction.c", "PROGRAM/dialogs/russian/x.c",
          "program/dialogs/english/Quest/a.c", "program/Loc_ai/LAi_utilites.c", "program/sea_ai/sea.c",
          "program/weather/whr.h", "program/battle_interface/BattleInterface.c"})
    {
        REQUIRE(isPatchTargetPath(path));
        REQUIRE(!storm::android::isUiOverlayPath(path)); // the two layers never claim the same file
    }
    // program/interface/ is shared with the UI overlay: applyLayer refuses an overlaid target
    // (checkOverlaidTarget).
    REQUIRE(isPatchTargetPath("PROGRAM/interface/ship.c"));
    // The app's set patches interface.c (autosave-rotation, interface-exit-time-reset).
    REQUIRE(isPatchTargetPath("PROGRAM/interface/interface.c"));
    REQUIRE(isPatchTargetPath("PROGRAM/characters/RPGUtilite.c"));
    REQUIRE(isPatchTargetPath("PROGRAM/Encounters/encounters_map.c"));
    REQUIRE(isPatchTargetPath("program/encounters/ENCOUNTERS_MAP.C"));
    REQUIRE(!storm::android::isUiOverlayPath("PROGRAM/Encounters/encounters_map.c"));
    // Exact reviewed root scripts (loading completion): only these two names, never other root files.
    REQUIRE(isPatchTargetPath("PROGRAM/seadogs.c"));
    REQUIRE(isPatchTargetPath("program/reload.c"));
    REQUIRE(!storm::android::isUiOverlayPath("PROGRAM/seadogs.c"));
    // The PGG script only, not the rest of program/scripts/.
    REQUIRE(isPatchTargetPath("PROGRAM/scripts/PsHero.c"));
    REQUIRE(!isPatchTargetPath("PROGRAM/scripts/food.c"));
    REQUIRE(!isPatchTargetPath("PROGRAM/interface/../scripts/PsHero.c"));
    for (const char *path : {"program/controls/init.c", "program/dialogs/x.c", "program/encounters/encounters.c",
                             "program/encounters/encounters_init.c", "program/encounters/encounters_map.h",
                             "program/encounters/encounters_map.c/other.c", "program/utils.c", "program/seadogs.h",
                             "program/battle_interface/bi.txt", "program/quests/q.txt", "../program/dialog.c",
                             "/program/dialog.c", "program/quests/../seadogs.c", "resource/ini/x.ini"})
        REQUIRE(!isPatchTargetPath(path));
}

void checkUnifiedDiff()
{
    REQUIRE(applyUnifiedDiff(kDialog, kDialogPatch, "PROGRAM/dialog.c") == kDialogFixed);
    REQUIRE(applyUnifiedDiff(kQuest, kQuestPatch, "PROGRAM/quests/q.c") == kQuestFixed);
    // Insertion before the first line: empty old range at 0.
    REQUIRE(applyUnifiedDiff("b\n", "--- a/x.c\n+++ b/x.c\n@@ -0,0 +1 @@\n+a\n", "x.c") == "a\nb\n");
    // Exact context: the same hunk does not apply one line off or to LF text.
    REQUIRE(throwsLayerError([] { applyUnifiedDiff("\r\n" + kDialog, kDialogPatch, "PROGRAM/dialog.c"); },
                             "does not match"));
    std::string lf = kDialog;
    std::erase(lf, '\r');
    REQUIRE(throwsLayerError([&] { applyUnifiedDiff(lf, kDialogPatch, "PROGRAM/dialog.c"); }, "does not match"));
    REQUIRE(throwsLayerError([] { applyUnifiedDiff(kDialog, kDialogPatch, "PROGRAM/other.c"); }, "must start with"));
    REQUIRE(throwsLayerError([] { applyUnifiedDiff("a\n", "--- a/x.c\n+++ b/x.c\n@@ -1 +1,2 @@\n a\n", "x.c"); },
                             "line counts"));
    REQUIRE(throwsLayerError([] { applyUnifiedDiff("a\n", "--- a/x.c\n+++ b/x.c\n@@ -1 +1 @@\n-a\n+b", "x.c"); },
                             "line feed"));
}

void checkIndexErrors(const fs::path &root)
{
    const fs::path set = root / "bad-1";
    const auto parse = [&](const std::string &body) {
        return [=] { parseIndex("storm-script-patches\t1\n" + body, set); };
    };
    const std::string head = "set\tbad-1\tG\t1\nidentify\tPROGRAM/v.h\t" + sha256("v") + "\n";
    const std::string target = "target\tPROGRAM/dialog.c\t" + sha256("a") + "\t" + sha256("b") + "\td.patch\n";
    const std::string fix = "fix\tf\tPROGRAM/dialog.c\tGPL-3.0\thttps://example.org\tF\n";
    parse(head + target + fix)();
    REQUIRE(throwsLayerError([&] { parseIndex("storm-script-patches\t2\n", set); }, "format 1"));
    REQUIRE(throwsLayerError(parse(target + fix), "no set record"));
    REQUIRE(throwsLayerError(parse("set\tbad-1\tG\t1\n" + target + fix), "does not identify"));
    REQUIRE(throwsLayerError(parse("set\tother\tG\t1\n"), "differs from its directory"));
    REQUIRE(throwsLayerError(
        parse(head + "target\tPROGRAM/controls/m.c\t" + sha256("a") + "\t" + sha256("b") + "\tm.patch\n"),
        "not an allowed patch target"));
    REQUIRE(throwsLayerError(
        parse(head + "target\tPROGRAM/dialog.c\t" + sha256("a") + "\t" + sha256("b") + "\t../d.patch\n"),
        "target needs"));
    REQUIRE(throwsLayerError(parse(head + target), "belongs to no fix"));
    REQUIRE(throwsLayerError(parse(head + target + target + fix), "duplicate target"));
    REQUIRE(throwsLayerError(parse(head + target + fix + fix), "duplicate fix"));
    REQUIRE(throwsLayerError(parse(head + fix + target), "unknown target"));
    REQUIRE(throwsLayerError(parse(head + target + "fix\tf\tPROGRAM/dialog.c\t\thttps://x\tF\n"), "fix needs"));
    REQUIRE(throwsLayerError([&] { parseIndex("storm-script-patches\t1\r\n", set); }, "CR in index"));
}

void checkApply(const fs::path &root)
{
    Fixture f(root / "apply");
    const auto original = f.paths.at("program/dialog.c");
    const auto report = f.apply();
    REQUIRE((report.set == "test-1.0" && report.game == "Test Game" && report.version == "1.0"));
    REQUIRE((report.applied == std::vector<std::string>{"time-scale", "sea"}));
    REQUIRE((report.unapplied.empty() && report.errors.empty() && report.noticeKey().empty()));
    REQUIRE(f.resolved("program/dialog.c") == kDialogFixed);
    REQUIRE(f.resolved("program/quests/q.c") == kQuestFixed);
    REQUIRE(f.resolved("program/sea_ai/s.c") == kSeaFixed);
    REQUIRE(read(original) == kDialog); // the player's file is never written
    REQUIRE(f.paths.at("program/dialog.c") == memoryPath("PROGRAM/dialog.c"));
    REQUIRE(f.derived.size() == 3);
    REQUIRE(report.info.back().find("3 script(s), " +
                                    std::to_string(kDialogFixed.size() + kQuestFixed.size() + kSeaFixed.size()) +
                                    " bytes in memory") != std::string::npos);
}

// Patched scripts never touch the disk: nothing is written next to the game or the layer, so a full
// or trimmed cache cannot stop a running game.
void checkMemoryOnly(const fs::path &root)
{
    const auto base = root / "memory";
    Fixture f(base);
    std::set<fs::path> before;
    for (const auto &entry : fs::recursive_directory_iterator(base))
        before.insert(entry.path());
    f.apply();
    std::set<fs::path> after;
    for (const auto &entry : fs::recursive_directory_iterator(base))
        after.insert(entry.path());
    REQUIRE(after == before);
    for (const auto &[key, path] : f.paths)
        if (path.starts_with(memoryPath("")))
            REQUIRE((f.derived.contains(path) && !fs::exists(path)));
    // A new scan forgets the scripts of the previous one.
    write(f.game / "PROGRAM/version.h", "#define VERSION \"2.0\"\r\n");
    f.reindex();
    f.apply();
    REQUIRE(f.derived.empty());
}

void checkBaseMismatch(const fs::path &root)
{
    Fixture f(root / "mismatch");
    write(f.game / "PROGRAM/quests/q.c", kQuest + "\r\n// modded");
    f.reindex();
    const auto report = f.apply();
    // The two-file fix is withheld entirely: dialog.c matches but is not patched.
    REQUIRE((report.applied == std::vector<std::string>{"sea"}));
    REQUIRE((report.unapplied.size() == 1 && report.unapplied[0].fix == "time-scale"));
    REQUIRE(report.unapplied[0].reasons.size() == 1);
    REQUIRE(report.unapplied[0].reasons[0].find("PROGRAM/quests/q.c differs") != std::string::npos);
    REQUIRE((report.errors.size() == 1 && report.errors[0].find("NOT applied") != std::string::npos));
    REQUIRE(f.resolved("program/dialog.c") == kDialog);
    REQUIRE(f.resolved("program/quests/q.c") == kQuest + "\r\n// modded");
    REQUIRE(f.resolved("program/sea_ai/s.c") == kSeaFixed);
    REQUIRE(!report.noticeKey().empty());
    REQUIRE(noticeText(report, false).find("Time scale [time-scale]") != std::string::npos);
    REQUIRE(noticeText(report, true).find("Time scale") != std::string::npos);
    // Same mismatch, same notice key (shown once); a different one changes it.
    f.reindex();
    REQUIRE(f.apply().noticeKey() == report.noticeKey());
    write(f.game / "PROGRAM/quests/q.c", kQuest + "\r\n// other mod");
    f.reindex();
    REQUIRE(f.apply().noticeKey() != report.noticeKey());

    // A missing target is a mismatch too.
    fs::remove(f.game / "PROGRAM/sea_ai/s.c");
    f.reindex();
    const auto missing = f.apply();
    REQUIRE((missing.applied.empty() && missing.unapplied.size() == 2));
    REQUIRE(missing.unapplied[1].reasons[0].find("missing from the game") != std::string::npos);

    // A fix sharing a target with an unapplied fix is withheld as well.
    Fixture g(root / "shared");
    g.writeIndex("fix\talso-dialog\tPROGRAM/dialog.c\tGPL-3.0\thttps://example.org/src\tAlso dialog\n");
    write(g.game / "PROGRAM/quests/q.c", "changed");
    g.reindex();
    const auto shared = g.apply();
    REQUIRE((shared.unapplied.size() == 2 && shared.unapplied[1].fix == "also-dialog"));
    REQUIRE(shared.unapplied[1].reasons[0].find("shares a script") != std::string::npos);
    REQUIRE(g.resolved("program/dialog.c") == kDialog);
}

void checkSetSelection(const fs::path &root)
{
    Fixture f(root / "select");
    write(f.game / "PROGRAM/version.h", "#define VERSION \"2.0\"\r\n");
    f.reindex();
    const auto none = f.apply();
    REQUIRE((none.layerPresent && none.set.empty() && none.applied.empty() && none.unapplied.empty()));
    REQUIRE(f.resolved("program/dialog.c") == kDialog);
    REQUIRE(none.noticeKey().empty()); // other games are not told about fixes not meant for them
    REQUIRE(f.derived.empty());

    Fixture g(root / "ambiguous");
    fs::copy(g.layer / "sets/test-1.0", g.layer / "sets/test-copy");
    auto index = read(g.layer / "sets/test-copy/index.txt");
    index.replace(index.find("set\ttest-1.0"), 12, "set\ttest-copy");
    write(g.layer / "sets/test-copy/index.txt", index);
    REQUIRE(throwsLayerError([&] { g.apply(); }, "both identify this game"));

    // The selection rule itself, as the launcher's archive reader calls it without a resource index.
    Fixture k(root / "identify");
    const auto sets = loadLayer(k.layer);
    REQUIRE(identifyKeys(sets) == std::vector<std::string>{"program/version.h"});
    std::vector<std::string> asked;
    const auto byHash = [&](const std::string &hash) {
        return [&asked, hash](const std::string &key) {
            asked.push_back(key);
            return hash;
        };
    };
    const Set *matched = identify(sets, byHash(sha256(kIdentity)));
    REQUIRE((matched && matched->id == "test-1.0" && matched->version == "1.0"));
    REQUIRE(asked == std::vector<std::string>{"program/version.h"});
    REQUIRE(identify(sets, byHash("")) == nullptr); // the copy lacks the file
    REQUIRE(identify(sets, byHash(sha256("other"))) == nullptr);
    const auto both = loadLayer(g.layer);
    REQUIRE(identifyKeys(both) == std::vector<std::string>{"program/version.h"}); // each key once
    REQUIRE(throwsLayerError([&] { identify(both, byHash(sha256(kIdentity))); }, "both identify this game"));

    Fixture h(root / "absent");
    fs::remove_all(h.layer);
    const auto absent = h.apply();
    REQUIRE((!absent.layerPresent && absent.applied.empty()));
    REQUIRE(h.resolved("program/dialog.c") == kDialog);
}

void checkDamagedLayer(const fs::path &root)
{
    Fixture f(root / "damaged");
    // A patch that does not fit its own verified base.
    write(f.layer / "sets/test-1.0/s.patch",
          "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-x\n+y\n");
    REQUIRE(throwsLayerError([&] { f.apply(); }, "does not match the base"));
    // A patch whose result is not the declared one.
    write(f.layer / "sets/test-1.0/s.patch", "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n"
                                             "-float S = 1.0;\n+float S = 3.0;\n");
    REQUIRE(throwsLayerError([&] { f.apply(); }, "expected " + sha256(kSeaFixed)));
    fs::remove(f.layer / "sets/test-1.0/s.patch");
    REQUIRE(throwsLayerError([&] { f.apply(); }, "is missing"));
    fs::remove_all(f.layer / "sets"); // a layer with no sets patches nothing
    const auto empty = f.apply();
    REQUIRE((empty.layerPresent && empty.set.empty() && empty.unapplied.empty()));
    REQUIRE(f.resolved("program/dialog.c") == kDialog);
}

void checkCacheInvalidation(const fs::path &root)
{
    Fixture f(root / "cache");
    f.apply();
    const auto layerPrint = storm::fingerprint::tree(f.layer);

    // A new patch version for the same base: the new text.
    const std::string fixed2 = "float S = 4.0;\n";
    write(f.layer / "sets/test-1.0/s.patch",
          "--- a/PROGRAM/sea_ai/s.c\n+++ b/PROGRAM/sea_ai/s.c\n@@ -1 +1 @@\n-float S = 1.0;\n+float S = 4.0;\n");
    auto index = read(f.layer / "sets/test-1.0/index.txt");
    index.replace(index.find(sha256(kSeaFixed)), 64, sha256(fixed2));
    write(f.layer / "sets/test-1.0/index.txt", index);
    f.reindex();
    f.apply();
    REQUIRE(f.resolved("program/sea_ai/s.c") == fixed2);
    // The compiled script cache key covers the patch layer (GetPathFingerprint).
    REQUIRE(storm::fingerprint::tree(f.layer) != layerPrint);
    const auto layers = storm::android::programLayers("/cache");
    REQUIRE(std::find(layers.begin(), layers.end(), fs::path("/cache/layers/script-patches/current")) != layers.end());
}
// program/interface/ is a patch target and the UI overlay's directory: a target the
// overlay replaces is a damaged layer, never patched and overlaid at once.
void checkOverlaidTarget(const fs::path &root)
{
    Fixture f(root / "overlaid");
    const auto report = f.apply();
    REQUIRE(report.unapplied.empty());
    Fixture g(root / "overlaid2");
    REQUIRE(throwsLayerError([&] { applyLayer(g.layer, g.paths, g.derived, {"program/sea_ai/s.c"}); },
                             "replaced by the UI overlay"));
}
} // namespace

TEST_CASE("script_patches")
{
    const fs::path root = fs::temp_directory_path();
    checkSha256();
    checkWhitelist();
    checkUnifiedDiff();
    checkIndexErrors(root);
    checkApply(root);
    checkMemoryOnly(root);
    checkBaseMismatch(root);
    checkSetSelection(root);
    checkDamagedLayer(root);
    checkCacheInvalidation(root);
    checkOverlaidTarget(root);
    std::puts("script patches: all checks passed");
}
