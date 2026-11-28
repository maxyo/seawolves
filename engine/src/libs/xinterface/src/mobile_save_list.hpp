#pragma once
#include "core.h"
#include "mobile_widget.hpp"
#include "shared/interface/messages.h"
#include "v_data.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <spdlog/spdlog.h>

namespace storm::mobile
{
// One record: the game keeps saves per profile folder (SAVE\<profile>\<file>). This port has
// no profiles: the list shows the records of every profile together.
// The game names save folders ASCII-case-insensitively (platform/storage.hpp child():
// "SAVE\\Player\\x" is the folder "player"), and its profile name (options
// StartGameParam.PlayerProfile, "Player") need not be spelled like the folder ("player") on
// disk. Profiles are compared the game's way, never byte for byte.
inline bool SameProfile(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (fold(a[i]) != fold(b[i]))
            return false;
    }
    return true;
}
struct SaveRecord
{
    std::string profile, file;
    bool operator==(const SaveRecord &other) const
    {
        return SameProfile(profile, other.profile) && file == other.file;
    }
};
struct SaveListSnapshot
{
    uint64_t revision = 0;
    std::string profile;            // the game's current profile (its save list below)
    std::vector<std::string> files; // the current profile's records in the game's order
    int selected = -1;              // index into files
    bool saving = false;
    // The rows: when loading every profile's records, newest first; when saving the current
    // profile's (a save goes to the profile of the game in progress).
    std::vector<SaveRecord> records;
    bool operator==(const SaveListSnapshot &) const = default;
};
// The save list of the open save_load interface, read from the game script. The platform
// host defines it, because only the host knows which interface is on screen and its
// revision (Android: xinterface.cpp); portable code here only parses and validates.
std::optional<SaveListSnapshot> ReadSaveList();
// The profile the save list makes current through the game's chooser now ("" for none), from
// the opening's profile (`game`, PlayerProfile.name; `listed`: it has a save folder), the newest
// record's profile and the game's current one (`current`, unknown while the list is unreadable).
// An open chooser is answered once: the game opened it instead of its SetCurrentProfile.
// Otherwise the opening's profile is made current again only when another one is (a record of
// it was deleted); a profile without a folder cannot be chosen, and the game made it current
// itself. Never a second SetCurrentProfile in the opening's frame: each runs LoadGameOptions.
inline std::string ProfileToChoose(bool chooserOpen, const std::string &game, bool listed, const std::string &newest,
                                   const std::optional<std::string> &current)
{
    if (chooserOpen)
        return listed ? game : newest;
    if (!listed || !current || SameProfile(*current, game))
        return {};
    return game;
}
// Makes `profile` the game's current profile through its own chooser (save_load.c
// ProcChooseProfile: SetCurrentProfile and the chooser closed); true when it is current.
// Defined by the platform host with ReadSaveList.
bool ChooseSaveProfile(const std::string &profile);
// What the save list shows of a record. The game stores a description in every save
// SFLB_DoSaveFileData appends "@hero@location" of the game in progress (mobile_save_facts). A record written
// before that has no hero; its label (the name the game gave it: the location) stands for
// the location. Published as the row's context: hero, location, gameDate (YYYY-MM-DD),
// gameTime (HH:MM), created (local YYYY-MM-DD HH:MM of the file), createdWhen (today, yesterday, year: this year, ""
// another), playMinutes; autosave = "1" for an autosave record; unreadable = "1" for a record without a readable
// description (the game cannot load it).
struct SaveFacts
{
    std::string hero, location, gameDate, gameTime, created;
    int playMinutes = -1;
};

inline std::vector<std::string> SplitSaveDescription(const std::string &text)
{
    std::vector<std::string> fields(1);
    for (const char c : text)
        if (c == '@')
            fields.emplace_back();
        else
            fields.back() += c;
    return fields;
}

// "[D days - ]H hours M min." (interface.c GetPlayTime; H counts all hours, D repeats them).
inline int SavePlayMinutes(const std::string &text)
{
    std::vector<int> numbers;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] >= '0' && text[i] <= '9')
        {
            numbers.push_back(std::atoi(text.c_str() + i));
            while (i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9')
                ++i;
        }
    if (numbers.size() < 2 || numbers.size() > 3)
        return -1;
    int hours = numbers[numbers.size() - 2];
    if (numbers.size() == 3 && hours < numbers[0] * 24)
        hours += numbers[0] * 24;
    return hours * 60 + numbers.back();
}

inline void SaveStamp(const std::string &stamp, SaveFacts &facts);

// A description written as one `name=value` line per attribute (Caribbean Legend's SerializeAttributes; lines end in
// '\r', names in any case): the save's label locName, the place location, the game's timeStr and playtime as above.
// No hero.
inline SaveFacts ParseSaveAttributes(const std::string &text)
{
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find_first_of("\r\n", start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        if (const size_t equals = line.find('='); equals != std::string::npos && equals > 0)
        {
            std::string name = line.substr(0, equals);
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            values[name] = line.substr(equals + 1);
        }
        start = end + 1;
    }
    SaveFacts facts;
    if (!values.contains("timestr"))
        return facts;
    facts.location = !values["location"].empty() ? values["location"] : values["locname"];
    SaveStamp(values["timestr"], facts);
    facts.playMinutes = SavePlayMinutes(values["playtime"]);
    return facts;
}

inline SaveFacts ParseSaveDescription(const std::string &text)
{
    SaveFacts facts;
    const auto fields = SplitSaveDescription(text);
    if (fields.size() < 5)
        return ParseSaveAttributes(text);
    facts.location = fields.size() >= 7 && !fields[6].empty() ? fields[6] : fields[0];
    if (fields.size() >= 7)
        facts.hero = fields[5];
    SaveStamp(fields[2], facts);
    facts.playMinutes = SavePlayMinutes(fields[3]);
    return facts;
}

// The game's "12:20  01.10.1654" → gameDate "1654-10-01", gameTime "12:20".
inline void SaveStamp(const std::string &stamp, SaveFacts &facts)
{
    const size_t colon = stamp.find(':'), dot = stamp.find('.');
    if (colon != std::string::npos && colon >= 1 && dot != std::string::npos && dot >= 2 &&
        stamp.find('.', dot + 1) != std::string::npos)
    {
        const int hour = std::atoi(stamp.c_str() + (colon >= 2 ? colon - 2 : 0)),
                  minute = std::atoi(stamp.c_str() + colon + 1);
        const int day = std::atoi(stamp.c_str() + dot - 2), month = std::atoi(stamp.c_str() + dot + 1);
        const int year = std::atoi(stamp.c_str() + stamp.find('.', dot + 1) + 1);
        if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year > 0 && hour >= 0 && hour < 24 && minute >= 0 &&
            minute < 60)
        {
            char date[24], clock[16];
            std::snprintf(date, sizeof(date), "%04d-%02d-%02d", year, month, day);
            std::snprintf(clock, sizeof(clock), "%02d:%02d", hour, minute);
            facts.gameDate = date;
            facts.gameTime = clock;
        }
    }
}

// Identity is bound to the complete current list, not to a reusable visual slot.
class SaveListCapability
{
    SaveListSnapshot published_;
    std::vector<std::string> ids_;
    uint64_t next_ = 0;

  public:
    struct Target
    {
        SaveListSnapshot owner;
        int index;
        std::string verb;
    };
    std::optional<Target> pending;
    // The rows' identity is the record list, not the game's current profile (choosing a
    // record of another profile switches it).
    bool sameList(const SaveListSnapshot &state) const
    {
        return published_.revision == state.revision && published_.saving == state.saving &&
               published_.records == state.records;
    }
    // `context(i)` gives row i its facts (mobile_save_facts.hpp SaveRowContext; i ==
    // records.size() is the new record).
    void collect(const SaveListSnapshot &state, bool enabled, std::vector<MobileWidget> &widgets,
                 const std::function<std::map<std::string, std::string>(size_t)> &context)
    {
        if (!sameList(state))
        {
            published_ = state;
            ids_.clear();
            for (size_t i = 0; i < state.records.size() + (state.saving ? 1 : 0); ++i)
                ids_.push_back(std::to_string(++next_));
        }
        // The list itself, also when there is no record (the empty state): value = record count.
        widgets.push_back(
            MobileWidget{"ANDROID_SAVES", "list", "", std::to_string(state.records.size()), -1, -1, enabled});
        const SaveRecord chosen = state.selected >= 0 && static_cast<size_t>(state.selected) < state.files.size()
                                      ? SaveRecord{state.profile, state.files[static_cast<size_t>(state.selected)]}
                                      : SaveRecord{};
        for (size_t i = 0; i < ids_.size(); ++i)
        {
            const bool fresh = i == state.records.size();
            MobileWidget row{"ANDROID_SAVE_LIST",
                             "save",
                             fresh ? "" : state.records[i].file,
                             !fresh && state.records[i] == chosen ? "1" : "0",
                             static_cast<int>(i),
                             static_cast<int>(i),
                             enabled};
            row.rowId = ids_[i];
            if (enabled)
            {
                row.actions.push_back("activate");
                if (!fresh)
                    row.actions.push_back("delete");
            }
            if (fresh)
                row.unavailableReason = "new";
            row.context = context(i);
            widgets.push_back(std::move(row));
        }
    }
    // An "activate" or "delete" of row `index` made for the row identity `row`.
    std::optional<Target> accept(const SaveListSnapshot &state, int index, const std::string &verb,
                                 const std::string &row) const
    {
        if (!sameList(state) || index < 0 || static_cast<size_t>(index) >= ids_.size() || row != ids_[index] ||
            (verb != "activate" && verb != "delete") ||
            (verb == "delete" && static_cast<size_t>(index) >= state.records.size()))
            return {};
        return Target{state, index, verb};
    }
    static bool current(const Target &target, const SaveListSnapshot &state)
    {
        return target.owner.revision == state.revision && target.owner.saving == state.saving &&
               target.owner.records == state.records && target.index >= 0 &&
               static_cast<size_t>(target.index) <= state.records.size();
    }
    // The target's place in the game's list of the current profile: its record's index, the
    // new slot (files.size()) for the new record; -1 when the record is not in that list
    // (another profile is current).
    static int slot(const Target &target, const SaveListSnapshot &state)
    {
        const auto index = static_cast<size_t>(target.index);
        if (index == target.owner.records.size())
            return state.saving ? static_cast<int>(state.files.size()) : -1;
        const SaveRecord &record = target.owner.records[index];
        if (!SameProfile(record.profile, state.profile))
            return -1;
        for (size_t i = 0; i < state.files.size(); ++i)
            if (state.files[i] == record.file)
                return static_cast<int>(i);
        return -1;
    }
};
inline SaveListCapability saveListCapability;

// How the game's save window picks a record: `Pictures`, a page of ten slot pictures SAVEIMG<n> clicked through
// eventSaveClick, scrolled by lines of five, the new record after the last; `Frames`, slots SAVEFRAME<n> carrying their
// own buttons (SAVEBUTTON<n>, DELETEBUTTON<n>) through the window's ievnt_command, g_SaveSlots of them shown from the
// record the scroll puts first, the new record SAVEFRAME_NEW above them (index -1). The host tells them apart by the
// window's nodes.
enum class SaveSlots
{
    Pictures,
    Frames
};

// The game's index of the target once selected: its place in the list, the new record's per kind.
inline int SaveIndexOf(const SaveListCapability::Target &target, const SaveListSnapshot &state, SaveSlots slots)
{
    const int index = SaveListCapability::slot(target, state);
    return slots == SaveSlots::Frames && index == static_cast<int>(state.files.size()) ? -1 : index;
}

// Select through the original script so its slot cache and delete shifting stay consistent, even when the touch UI
// displays a row far beyond the native page.
inline bool SelectSaveTarget(const SaveListCapability::Target &target, SaveSlots slots)
{
    auto state = ReadSaveList();
    if (!state || !SaveListCapability::current(target, *state))
        return false;
    // A record of another profile: that profile becomes the game's current one first.
    if (static_cast<size_t>(target.index) < target.owner.records.size() &&
        !SameProfile(target.owner.records[static_cast<size_t>(target.index)].profile, state->profile))
    {
        if (!ChooseSaveProfile(target.owner.records[static_cast<size_t>(target.index)].profile))
            return false;
        state = ReadSaveList();
        if (!state)
            return false;
    }
    const int index = SaveListCapability::slot(target, *state);
    if (index < 0)
        return false;
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    int32_t page = -1;
    if (!first || !first->Get(page) || !selected)
        return false;
    std::string node;
    if (slots == SaveSlots::Frames)
    {
        node = "SAVEFRAME_NEW";
        const int count = static_cast<int>(state->files.size());
        if (index < count)
        {
            auto *shown = static_cast<VDATA *>(core.GetScriptVariable("g_SaveSlots"));
            int32_t visible = 0;
            if (!shown || !shown->Get(visible) || visible <= 0)
                return false;
            if (index < page || index >= page + visible)
            {
                // The script's page is int(pages * position) of the scroll's position.
                const int pages = count - visible + 1, wanted = std::min(index, count - visible);
                core.Event("ScrollPosChange", "fs", (wanted + .5f) / static_cast<float>(pages), "SAVESCROLL");
                if (!first->Get(page) || index < page || index >= page + visible)
                    return false;
            }
            node = "SAVEFRAME" + std::to_string(index - page + 1);
        }
        core.Event("ievnt_command", "ss", "click", node.c_str());
    }
    else
    {
        const int wanted = (index / 5) * 5;
        if (page != wanted)
        {
            const int lines = static_cast<int>(state->files.size()) / 5;
            const float position = lines ? (wanted / 5 == lines ? 1.f : (wanted / 5 + .25f) / lines) : 0.f;
            core.Event("ScrollPosChange", "fs", position, "SAVESCROLL");
            if (!first->Get(page) || page != wanted)
                return false;
        }
        // The delayed initial-selection event may have changed only this index.
        // Restoring it before dispatch prevents confirmation from targeting another save.
        node = "SAVEIMG" + std::to_string(index - page + 1);
        core.Event("eventSaveClick", "ls", ACTION_MOUSECLICK, node.c_str());
    }
    int32_t actual = -2;
    return selected->Get(actual) && actual == SaveIndexOf(target, *state, slots);
}

// The selected record's action in the game's window: "activate" loads, writes or writes over it (a question may
// follow), "delete" asks to delete it.
inline void ActOnSaveTarget(const SaveListCapability::Target &target, SaveSlots slots)
{
    if (slots == SaveSlots::Pictures)
    {
        if (target.verb == "delete")
            core.Event("eventBtnAction", "ls", ACTION_ACTIVATE, "BTN_DELETE");
        else
            core.Event("eventSelecterActivate");
        return;
    }
    auto *slot = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveSlot"));
    int32_t shown = -1;
    if (!slot || !slot->Get(shown))
    {
        spdlog::error("save_load: the window's selected slot (g_nCurrentSaveSlot) is unreadable; '{}' not sent",
                      target.verb);
        return;
    }
    const std::string number = shown < 0 ? "_NEW" : std::to_string(shown + 1);
    if (target.verb == "delete")
        core.Event("ievnt_command", "ss", "click", ("DELETEBUTTON" + number).c_str());
    else
        core.Event("ievnt_command", "ss", "activate", ("SAVEFRAME" + number).c_str());
}
} // namespace storm::mobile
namespace storm::mobile
{
inline bool RestoreSaveTarget(const SaveListCapability::Target &target, SaveSlots slots)
{
    auto state = ReadSaveList();
    if (!state || !SaveListCapability::current(target, *state))
        return false;
    const int place = SaveListCapability::slot(target, *state), index = SaveIndexOf(target, *state, slots);
    auto *selected = static_cast<VDATA *>(core.GetScriptVariable("g_nCurrentSaveIndex"));
    auto *first = static_cast<VDATA *>(core.GetScriptVariable("g_nFirstSaveIndex"));
    int32_t page = -1;
    // The new record of a window of frames stands above its page.
    if (place < 0 || !selected || !first || !first->Get(page) || (index >= 0 && (index < page || index >= page + 10)))
        return false;
    // The list's delayed first selection (save_load.c posts eventSaveClick 350-500 ms after opening) can move
    // the game's choice after the player's tap; its click handler would also take focus from the open question,
    // so the chosen index is put back directly.
    int32_t current = -1;
    if (selected->Get(current) && current != index)
        spdlog::warn("save_load: the game's selection moved to {} after the player chose {}; chosen restored", current,
                     index);
    selected->Set(static_cast<int32_t>(index));
    return true;
}
} // namespace storm::mobile
