#include "app_script_events.hpp"
#include "core.h"
#include "shared/interface/messages.h"
#include "v_data.h"
#include "v_file_service.h"

#include "mobile_save_facts.hpp"
#include "save_autosave.hpp"
#include "save_record.hpp"
#include "save_thumbnail.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <utility>
#include <memory>
#include <cstring>

namespace storm::mobile
{
std::optional<std::array<std::string, 4>> CurrentSaveIdentity()
{
    VDATA *result = core.Event(storm::app::script_event::AndroidSaveIdentity);
    const char *text = nullptr;
    if (!result || !result->Get(text) || !text)
    {
        static bool reported = false;
        if (!std::exchange(reported, true))
            spdlog::error("save list: no AndroidSaveIdentity handler answered; records lose hero and location");
        return {};
    }
    const auto fields = SplitSaveDescription(text);
    if (fields.size() != 4 || fields[0].empty())
        return {};
    return std::array<std::string, 4>{fields[0], fields[1], fields[2], fields[3]};
}

std::optional<MobileWidget> GameIdentityWidget(uint64_t opening)
{
    static std::pair<uint64_t, std::optional<std::array<std::string, 4>>> cached{~0ull, {}};
    if (cached.first != opening)
        cached = {opening, CurrentSaveIdentity()};
    if (!cached.second)
        return {};
    const auto &identity = *cached.second;
    MobileWidget widget{"ANDROID_GAME", "text", identity[0] + " · " + identity[1], "", -1, -1, true};
    SaveFacts facts;
    SaveStamp(identity[3], facts);
    widget.context = {{"hero", identity[0]}, {"location", identity[1]}};
    if (!facts.gameDate.empty())
        widget.context["gameDate"] = facts.gameDate, widget.context["gameTime"] = facts.gameTime;
    return widget;
}

std::optional<MobileWidget> QuickLoadWidget()
{
    auto *profile = static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
    auto *attributes = profile ? profile->GetAClass() : nullptr;
    auto *name = attributes ? attributes->FindAClass(attributes, "name") : nullptr;
    if (!name || !name->HasValue() || name->GetValue().empty())
        return {};
    auto *index = attributes->FindAClass(attributes, "QuickSaveIndex");
    const std::string file = std::string(name->GetValue()) + " QuickSave " +
                             (index && index->HasValue() ? std::string(index->GetValue()) : "1");
    if (!fio->_FileOrDirectoryExists(("SAVE\\" + std::string(name->GetValue()) + "\\" + file).c_str()))
        return {};
    return MobileWidget{"ANDROID_QUICK_LOAD", "text", file, "", -1, -1, true};
}

const SaveFactsCache::Entry &SaveFactsCache::get(const std::string &path)
{
    const bool exists = fio->_FileOrDirectoryExists(path.c_str());
    const auto stamp = exists ? fio->_GetLastWriteTime(path.c_str()) : std::filesystem::file_time_type{};
    const uint64_t written = exists ? static_cast<uint64_t>(stamp.time_since_epoch().count()) : 0;
    auto found = entries_.find(path);
    if (found != entries_.end() && found->second.written == written && found->second.present == exists)
        return found->second;
    Entry entry{written, exists};
    if (exists)
    {
        int32_t size = 0;
        const std::unique_ptr<char[]> data(static_cast<char *>(core.GetSaveData(path.c_str(), size)));
        if (data && size > 0)
        {
            const auto extension =
                save::thumbnail::Decode({reinterpret_cast<const uint8_t *>(data.get()), static_cast<size_t>(size)});
            if (extension.valid)
            {
                entry.readable = true;
                entry.facts = ParseSaveDescription(SaveDescriptionText(extension, path));
                entry.thumbnail = !extension.pixels.empty();
                if (extension.picture_problem)
                    spdlog::error("Save '{}': {}: listed without its picture, still loadable", path,
                                  extension.picture_problem);
            }
        }
        if (!entry.readable)
            spdlog::error("Save '{}' has no readable description: listed without its facts, the game cannot load it",
                          path);
        const std::time_t local_time = fio->_ToTimeT(stamp);
        std::tm local{};
        localtime_r(&local_time, &local);
        char created[24];
        std::strftime(created, sizeof(created), "%Y-%m-%d %H:%M", &local);
        entry.facts.created = created;
    }
    return entries_[path] = std::move(entry);
}

namespace
{
SaveFactsCache saveFacts;

// When a record was written, as its list names the day against the phone's date now: "today", "yesterday", "year"
// (earlier this year) or "" (another year).
std::string CreatedWhen(const std::string &created)
{
    std::tm then{};
    if (std::sscanf(created.c_str(), "%4d-%2d-%2d", &then.tm_year, &then.tm_mon, &then.tm_mday) != 3)
        return "";
    then.tm_year -= 1900;
    then.tm_mon -= 1;
    then.tm_hour = 12;
    const std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_r(&now, &today);
    today.tm_hour = 12;
    today.tm_min = today.tm_sec = 0;
    const int year = then.tm_year;
    const double days = std::difftime(std::mktime(&today), std::mktime(&then)) / 86400.0;
    if (days > -0.5 && days < 0.5)
        return "today";
    if (days >= 0.5 && days < 1.5)
        return "yesterday";
    return year == today.tm_year ? "year" : "";
}
} // namespace

std::map<std::string, std::string> SaveRowContext(const SaveListSnapshot &state, size_t index)
{
    std::map<std::string, std::string> context;
    if (index >= state.records.size())
    {
        // The record about to be written: the game in progress, no dates yet.
        static std::pair<uint64_t, std::optional<std::array<std::string, 4>>> identity{~0ull, {}};
        if (identity.first != state.revision)
            identity = {state.revision, CurrentSaveIdentity()};
        if (const auto &game = identity.second)
        {
            context["hero"] = (*game)[0];
            context["location"] = (*game)[1];
            if (const int minutes = SavePlayMinutes((*game)[2]); minutes >= 0)
                context["playMinutes"] = std::to_string(minutes);
        }
        return context;
    }
    const std::string path = "SAVE\\" + state.records[index].profile + "\\" + state.records[index].file;
    const auto &entry = saveFacts.get(path);
    const SaveFacts &facts = entry.facts;
    for (const auto &[key, value] : {std::pair<const char *, const std::string &>{"hero", facts.hero},
                                     {"location", facts.location},
                                     {"gameDate", facts.gameDate},
                                     {"gameTime", facts.gameTime},
                                     {"created", facts.created}})
        if (!value.empty())
            context[key] = value;
    if (!facts.created.empty())
        context["createdWhen"] = CreatedWhen(facts.created);
    // The autosave rotation's records and the record a game writes itself before a boarding ("<profile> AutoSave",
    // interface.c AutoSaveContinue): listed as their own group, never deleted or overwritten by the player.
    const std::string &file = state.records[index].file;
    if (storm::save::AutosaveRecordNumber(file) != 0 || file.ends_with(" AutoSave"))
        context["autosave"] = "1";
    if (entry.thumbnail)
        context["preview"] = save::thumbnail::Source(path, state.revision, entry.written);
    if (facts.playMinutes >= 0)
        context["playMinutes"] = std::to_string(facts.playMinutes);
    // The game loads only a record with a description (seadogs.c LoadGame) and lists one without it as corrupted.
    if (entry.present && !entry.readable)
        context["unreadable"] = "1";
    return context;
}

std::vector<SaveRecord> SaveRecordsOfAllProfiles()
{
    struct Found
    {
        SaveRecord record;
        std::filesystem::file_time_type written;
    };
    std::vector<Found> found;
    for (const auto &folder : fio->_GetFsPathsByMask("SAVE", nullptr, true, true, false))
    {
        const auto folderUtf8 = folder.filename().u8string();
        const std::string profile(folderUtf8.begin(), folderUtf8.end());
        // The game's profile list leaves the network save folder out (XI_FindFoldersWithoutNetsave).
        std::string lower = profile;
        for (char &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (profile.empty() || lower == "netsave")
            continue;
        for (const auto &path : fio->_GetFsPathsByMask(("SAVE\\" + profile).c_str(), nullptr, true))
        {
            const auto utf8 = path.filename().u8string();
            const std::string name(utf8.begin(), utf8.end());
            const auto ends = [&name](const std::string &suffix) {
                return name.size() >= suffix.size() &&
                       name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            std::error_code error;
            const auto written = std::filesystem::last_write_time(path, error);
            if (error || ends(".storm-tmp") || ends(".storm-bak"))
                continue;
            found.push_back({{profile, name}, written});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.written > b.written; });
    std::vector<SaveRecord> records;
    for (Found &entry : found)
        records.push_back(std::move(entry.record));
    return records;
}

std::optional<SaveRecord> ContinueRecord(uint64_t opening)
{
    static std::pair<uint64_t, std::optional<SaveRecord>> newest{~0ull, {}};
    if (newest.first != opening)
    {
        const auto records = SaveRecordsOfAllProfiles();
        newest = {opening, records.empty() ? std::nullopt : std::optional<SaveRecord>(records.front())};
    }
    return newest.second;
}

std::optional<MobileWidget> ContinueWidget(uint64_t opening)
{
    const auto newest = ContinueRecord(opening);
    if (!newest)
        return {};
    const SaveFacts &facts = saveFacts.get("SAVE\\" + newest->profile + "\\" + newest->file).facts;
    // A record whose description cannot be read is still the one «Продолжить» loads: its file name names it.
    const std::string label = facts.location.empty() ? newest->file
                              : facts.hero.empty()   ? facts.location
                                                     : facts.hero + " · " + facts.location;
    MobileWidget widget{"ANDROID_CONTINUE", "text", label, "", -1, -1, true};
    if (!facts.location.empty())
        widget.context["location"] = facts.location;
    if (!facts.hero.empty())
        widget.context["hero"] = facts.hero;
    return widget;
}
} // namespace storm::mobile
