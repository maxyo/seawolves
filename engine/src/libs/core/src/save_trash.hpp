#pragma once

// Deferred deletion of save files.
//
// The game's scripts overwrite a slot by deleting the old save and then
// writing a new one, possibly under another name. A process death between the
// two steps would lose the slot. The scripts belong to the player's game
// files, so the engine makes the delete itself recoverable.
//
// Rule
// 1. Deleting a save renames it to "<name>.storm-deleted". It disappears from
//    listings at once (FILE_SERVICE hides auxiliary names in save folders).
// 2. The delete becomes final, i.e. the file is really removed, when either
//      - a save is written successfully under the same name, or
//      - the frames in which the scripts could have run the save that replaces
//        the slot have run (kReplacingFrames) and no save failed: an overwrite
//        under another name that completed, or a plain delete by the player.
//    No amount of time makes a delete final: a process that is frozen between
//    two frames (the application in the background) decides nothing.
// 3. If writing a save fails, every pending delete is undone.
// 4. A "<name>.storm-deleted" that this session did not create is the trace of
//    a session that died before rule 2. When its folder is next listed, it is
//    restored under its name, or removed if a save with that name exists again.
//
// Consequence: a save deleted by the player comes back only if the process is
// killed within kReplacingFrames frames of the delete, and a slot overwritten
// under another name comes back beside the new save if the process is killed
// in those frames. The rule errs towards keeping data.

#include "save_file.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace storm::save
{
inline constexpr char kDeletedSuffix[] = ".storm-deleted";

inline bool hasSuffix(const std::string &name, const char *suffix)
{
    const auto n = std::strlen(suffix);
    return name.size() > n && name.compare(name.size() - n, n, suffix) == 0;
}

// Names the engine creates next to saves; never shown as saves.
inline bool isReservedName(const std::string &name)
{
    return hasSuffix(name, kTempSuffix) || hasSuffix(name, kBackupSuffix) || hasSuffix(name, kDeletedSuffix);
}

class Trash
{
  public:
    // The frames after a delete in which the scripts' own save of the slot can still start: the scripts delete the
    // slot and post the save with no delay, and a posted event runs in the second pass over the posted events after
    // its post (COMPILER::ProcessFrame: the first pass only admits it), one pass per frame.
    static constexpr uint64_t kReplacingFrames = 2;

    // Replace a delete of `path`. Returns false if nothing was deleted. A file that could not be set aside is
    // removed at once, as the game asked, and `undeferred` gets why.
    bool discard(const fs::path &path, std::string *undeferred = nullptr)
    {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec)
            return false;
        const auto deleted = withSuffix(path, kDeletedSuffix);
        forget(deleted);
        fs::remove(deleted, ec); // an older deleted save of the same name is superseded
        ec.clear();
        fs::rename(path, deleted, ec);
        if (ec)
        {
            if (undeferred)
                *undeferred = ec.message();
            ec.clear();
            return fs::remove(path, ec) && !ec;
        }
        // Leftovers of an interrupted atomic write of this name die with it.
        fs::remove(withSuffix(path, kTempSuffix), ec);
        fs::remove(withSuffix(path, kBackupSuffix), ec);
        pending_.push_back({deleted, frame_});
        return true;
    }

    // Rule 2, frames: call once per frame, before the scripts' posted events run.
    void frame()
    {
        ++frame_;
        std::error_code ec;
        std::erase_if(pending_, [&](const Entry &entry) {
            if (frame_ - entry.frame <= kReplacingFrames)
                return false;
            fs::remove(entry.deleted, ec);
            return true;
        });
    }

    // Rule 2, the save `saved` was written: the delete of that name is final.
    void commit(const fs::path &saved)
    {
        const auto deleted = withSuffix(saved, kDeletedSuffix);
        if (!isPending(deleted))
            return;
        std::error_code ec;
        fs::remove(deleted, ec);
        forget(deleted);
    }

    // Rule 3, a save could not be written: bring the deleted saves back.
    void rollback()
    {
        for (const auto &entry : pending_)
            restore(entry.deleted);
        pending_.clear();
    }

    // Rule 4: call before listing `directory`. Returns the number of restored saves.
    int recover(const fs::path &directory)
    {
        int restored = 0;
        std::error_code ec;
        std::vector<fs::path> found;
        for (auto it = fs::directory_iterator(directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            std::error_code type;
            if (it->is_regular_file(type) && !type && hasSuffix(it->path().filename().string(), kDeletedSuffix))
                found.push_back(it->path());
        }
        for (const auto &deleted : found)
        {
            if (isPending(deleted))
                continue; // deleted in this session, not final yet
            restored += restore(deleted) ? 1 : 0;
        }
        return restored;
    }

    size_t pendingCount() const
    {
        return pending_.size();
    }

  private:
    struct Entry
    {
        fs::path deleted;
        uint64_t frame; // the frame the delete was made in
    };

    static fs::path originalName(const fs::path &deleted)
    {
        auto text = deleted.string();
        text.resize(text.size() - std::strlen(kDeletedSuffix));
        return fs::path(text);
    }

    // Returns true if the save is back under its name.
    static bool restore(const fs::path &deleted)
    {
        std::error_code ec;
        const auto original = originalName(deleted);
        if (fs::exists(original, ec))
        {
            fs::remove(deleted, ec); // the name is in use again; the old version is obsolete
            return false;
        }
        fs::rename(deleted, original, ec);
        return !ec;
    }

    bool isPending(const fs::path &deleted) const
    {
        return std::any_of(pending_.begin(), pending_.end(),
                           [&](const Entry &entry) { return entry.deleted == deleted; });
    }

    void forget(const fs::path &deleted)
    {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const Entry &entry) { return entry.deleted == deleted; }),
                       pending_.end());
    }

    std::vector<Entry> pending_;
    uint64_t frame_ = 0;
};

// The engine's single instance (game thread only).
inline Trash &trash()
{
    static Trash instance;
    return instance;
}
} // namespace storm::save
