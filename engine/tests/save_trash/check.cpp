#include "save_trash.hpp"

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <set>

#include <catch2/catch.hpp>

namespace fs = std::filesystem;
using namespace storm::save;

static void put(const fs::path &path, const std::string &text)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

static std::string get(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

// What a save list shows: everything except the engine's reserved names.
static std::set<std::string> listed(const fs::path &dir)
{
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(dir))
        if (!isReservedName(entry.path().filename().string()))
            names.insert(entry.path().filename().string());
    return names;
}

static std::set<std::string> all(const fs::path &dir)
{
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(dir))
        names.insert(entry.path().filename().string());
    return names;
}

TEST_CASE("save_trash")
{
    const fs::path dir = fs::temp_directory_path() / "Player One";
    fs::create_directories(dir);
    using Names = std::set<std::string>;
    const auto frames = [](Trash &trash, int count) {
        for (int n = 0; n < count; ++n)
            trash.frame();
    };

    { // Overwrite under the same name that completes: the old one is gone for good.
        Trash trash;
        put(dir / "Port Royal", "old");
        put(dir / "Port Royal.storm-tmp", "torn");
        REQUIRE(trash.discard(dir / "Port Royal"));
        REQUIRE((listed(dir).empty() && all(dir) == Names{"Port Royal.storm-deleted"}));
        frames(trash, 1);
        REQUIRE((trash.recover(dir) == 0 && listed(dir).empty())); // listing mid-overwrite does not resurrect it
        put(dir / "Port Royal", "new");
        trash.commit(dir / "Port Royal");
        REQUIRE((all(dir) == Names{"Port Royal"} && get(dir / "Port Royal") == "new" && trash.pendingCount() == 0));
        fs::remove(dir / "Port Royal");
    }
    { // Process dies between the delete and the write: the next session restores the slot.
        {
            Trash dying;
            put(dir / "Tortuga", "precious");
            REQUIRE(dying.discard(dir / "Tortuga"));
        } // no commit, no frame: the session ends here
        REQUIRE(all(dir) == Names{"Tortuga.storm-deleted"});
        Trash next;
        REQUIRE(next.recover(dir) == 1);
        REQUIRE((all(dir) == Names{"Tortuga"} && get(dir / "Tortuga") == "precious"));
        REQUIRE(next.recover(dir) == 0); // idempotent
    }
    { // ...also when the dying session had already started the new file under the same name.
        {
            Trash dying;
            REQUIRE(dying.discard(dir / "Tortuga"));
            put(dir / "Tortuga.storm-tmp", "half written"); // atomic write never got to rename
        }
        Trash next;
        REQUIRE((next.recover(dir) == 1 && get(dir / "Tortuga") == "precious"));
        fs::remove(dir / "Tortuga.storm-tmp");
        // If the new file did land before the death, it wins and the old version is dropped.
        {
            Trash dying;
            REQUIRE(dying.discard(dir / "Tortuga"));
            put(dir / "Tortuga", "newer");
        }
        Trash later;
        REQUIRE((later.recover(dir) == 0 && all(dir) == Names{"Tortuga"} && get(dir / "Tortuga") == "newer"));
    }
    { // Plain delete by the player: final once the frames a replacing save could start in have run, not before.
        Trash trash;
        frames(trash, 5);
        REQUIRE(trash.discard(dir / "Tortuga"));
        frames(trash, static_cast<int>(Trash::kReplacingFrames));
        REQUIRE((all(dir) == Names{"Tortuga.storm-deleted"} && trash.pendingCount() == 1));
        REQUIRE((trash.recover(dir) == 0 && listed(dir).empty()));
        frames(trash, 1);
        REQUIRE((all(dir).empty() && trash.pendingCount() == 0));
    }
    { // The scripts delete a slot and post its save; the process is frozen for any time before the frame that
      // runs the save (the application in the background), and that save fails: the slot is back.
        Trash trash;
        put(dir / "A", "a");
        REQUIRE(trash.discard(dir / "A"));
        frames(trash, 1);                 // the frame that admits the posted save
        REQUIRE(trash.recover(dir) == 0); // a listing in between decides nothing
        frames(trash, 1);                 // the frame that runs it
        trash.rollback();
        REQUIRE((all(dir) == Names{"A"} && get(dir / "A") == "a" && trash.pendingCount() == 0));
    }
    { // A save written under another name leaves the delete pending: a save that fails after it brings the slot back.
        Trash trash;
        put(dir / "B", "b");
        REQUIRE((trash.discard(dir / "A") && trash.discard(dir / "B")));
        put(dir / "C", "c");
        trash.commit(dir / "C");
        REQUIRE((all(dir) == (Names{"A.storm-deleted", "B.storm-deleted", "C"}) && trash.pendingCount() == 2));
        put(dir / "B", "b2");
        trash.commit(dir / "B"); // B's own save: only B's delete is final
        REQUIRE((all(dir) == (Names{"A.storm-deleted", "B", "C"}) && trash.pendingCount() == 1));
        trash.rollback();
        REQUIRE((all(dir) == (Names{"A", "B", "C"}) && get(dir / "A") == "a" && get(dir / "B") == "b2"));
        fs::remove(dir / "C");
        // Delete, recreate the name, delete again before the first is final: the latest version is the one kept.
        REQUIRE(trash.discard(dir / "A"));
        put(dir / "A", "a2");
        REQUIRE((trash.discard(dir / "A") && trash.pendingCount() == 1));
        trash.rollback();
        REQUIRE(get(dir / "A") == "a2");
    }
    { // An overwrite under another name that completed: the old slot goes with the frames, as a plain delete.
        Trash trash;
        REQUIRE(trash.discard(dir / "A"));
        put(dir / "C", "c");
        trash.commit(dir / "C");
        frames(trash, static_cast<int>(Trash::kReplacingFrames) + 1);
        REQUIRE((all(dir) == (Names{"B", "C"}) && trash.pendingCount() == 0));
        fs::remove(dir / "B");
        fs::remove(dir / "C");
    }
    { // Not a file / missing: nothing happens. Reserved names are recognised exactly.
        Trash trash;
        REQUIRE((!trash.discard(dir / "missing") && !trash.discard(dir) && trash.pendingCount() == 0));
        REQUIRE((isReservedName("x.storm-deleted") && isReservedName("x.storm-bak") && isReservedName("x.storm-tmp")));
        REQUIRE((!isReservedName("x") && !isReservedName(".storm-deleted") && !isReservedName("x.storm-deleted2")));
        REQUIRE(trash.recover(dir / "no such folder") == 0);
    }
    { // A name too long to take the suffix cannot be set aside: it is deleted at once and the caller is told why.
        Trash trash;
        const std::string name(250, 'n');
        put(dir / name, "long");
        std::string undeferred;
        REQUIRE((trash.discard(dir / name, &undeferred) && !undeferred.empty()));
        REQUIRE((all(dir).empty() && trash.pendingCount() == 0));
    }
    { // Rule 5: what a session that died inside an atomic write left, at the next session's first listing.
        Bytes image;
        std::string error;
        const std::string raw(4096, 's');
        REQUIRE(buildState("ver", raw.data(), static_cast<uint32_t>(raw.size()), image, error));
        const auto save = [&](const fs::path &path) { put(path, std::string(image.begin(), image.end())); };
        save(dir / "Kept");
        save(dir / "Kept.storm-tmp");                // the write's file beside the save of its name
        put(dir / "Kept.storm-bak", "old");          // the previous version beside it
        save(dir / "New.storm-tmp");                 // a new record, complete, killed before its rename
        put(dir / "Torn.storm-tmp", "half written"); // a new record cut short
        put(dir / "Broken", "damaged");              // a damaged save: its backup is the load's to use
        save(dir / "Broken.storm-bak");
        Trash next;
        const auto done = next.sweep(dir);
        using Done = Trash::Leftover::Done;
        const auto did = [&](const char *name) {
            for (const auto &leftover : done)
                if (leftover.path.filename() == name)
                    return leftover.done;
            FAIL("nothing done with " << name);
            return Done::Kept;
        };
        CHECK(done.size() == 5);
        CHECK(did("Kept.storm-tmp") == Done::Removed);
        CHECK(did("Kept.storm-bak") == Done::Removed);
        CHECK(did("New.storm-tmp") == Done::Named);
        CHECK(did("Torn.storm-tmp") == Done::Removed);
        CHECK(did("Broken.storm-bak") == Done::Kept);
        CHECK(all(dir) == (Names{"Broken", "Broken.storm-bak", "Kept", "New"}));
        CHECK(checkFile(dir / "New", error));
        save(dir / "Late.storm-tmp");
        CHECK(next.sweep(dir).empty()); // once per folder and session: a write of this session is never touched
        for (const auto &name : all(dir))
            fs::remove(dir / name);
    }
    std::cout << "save trash: overwrite/crash/plain delete/failed write/unfinished writes PASS\n";
}
