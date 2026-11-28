// The autosave rotation (core save_autosave.hpp): three records, the oldest replaced,
// the player's records never.
#include "save_autosave.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

#include <catch2/catch.hpp>

using namespace storm::save;

namespace
{
// A profile's save folder: file name -> time of last writing.
struct Folder
{
    std::map<std::string, int> files;
    int clock = 0;

    // XINTERFACE::NextAutosaveRecord.
    std::string NextAutosave() const
    {
        AutosaveRotation<int> rotation;
        for (const auto &[name, written] : files)
            rotation.Add(name, written);
        return AutosaveRecordName(rotation.Next());
    }
    std::string Autosave()
    {
        const std::string record = NextAutosave();
        files[record] = ++clock;
        return record;
    }
    // save_load.c LaunchCustomSaveGame with XINTERFACE::NewSaveFileName: the location's name,
    // numbered until it is free.
    std::string SaveByPlayer(const std::string &location)
    {
        std::string name = location;
        for (int n = 1; files.count(name) || AutosaveRecordNumber(name) != 0; ++n)
            name = location + " " + std::to_string(n);
        files[name] = ++clock;
        return name;
    }
};

void names()
{
    REQUIRE((AutosaveRecordNumber("Автосохранение 1") == 1 &&
             AutosaveRecordNumber("Автосохранение 3") == 3)); // an autosave record is not recognised
    for (const char *other : {"Автосохранение 0", "Автосохранение 4", "Автосохранение 01", "Автосохранение 1 ",
                              "Автосохранение ", "Автосохранение", "Автосохранение 1.storm-deleted", "автосохранение 1",
                              "Бастер", "Бастер 1", "Player AutoSave", "Player QuickSave 1", "Автосохранение 12", ""})
    {
        INFO(other);
        REQUIRE(AutosaveRecordNumber(other) == 0);
    }
    REQUIRE(AutosaveRecordName(2) ==
            "Автосохранение 2"); // the record name changed: saves of earlier builds would leave the rotation
}

void rotation()
{
    Folder folder;
    const std::string town = folder.SaveByPlayer("Бастер");
    REQUIRE((folder.Autosave() == "Автосохранение 1" && folder.Autosave() == "Автосохранение 2" &&
             folder.Autosave() == "Автосохранение 3")); // the free records are not taken in order
    const std::string again = folder.SaveByPlayer("Бастер");
    REQUIRE((town == "Бастер" && again == "Бастер 1")); // the player's records took unexpected names
    const std::map<std::string, int> players{{town, folder.files[town]}, {again, folder.files[again]}};

    // Every further autosave replaces the one written longest ago.
    const char *expected[] = {"Автосохранение 1", "Автосохранение 2", "Автосохранение 3", "Автосохранение 1"};
    for (const char *record : expected)
        REQUIRE(folder.Autosave() == record); // an autosave did not replace the oldest record

    // The order is by time of writing, not by number: 2 was written last here.
    folder.files["Автосохранение 2"] = ++folder.clock;
    folder.files["Автосохранение 3"] = folder.files["Автосохранение 1"] - 1;
    REQUIRE(folder.Autosave() == "Автосохранение 3"); // the oldest record is chosen by number, not by time

    // A deleted record is the next one written.
    folder.files.erase("Автосохранение 2");
    REQUIRE(folder.Autosave() == "Автосохранение 2"); // a missing record is not filled first

    int autosaves = 0;
    for (const auto &[name, written] : folder.files)
        autosaves += AutosaveRecordNumber(name) != 0;
    REQUIRE((autosaves == kAutosaveRecords && kAutosaveRecords == 3)); // there are not exactly three autosave records
    for (const auto &[name, written] : players)
        REQUIRE(folder.files.at(name) == written); // an autosave replaced a record of the player
}

// A place called like an autosave record cannot give the player's record that name.
void reservedNames()
{
    Folder folder;
    REQUIRE(folder.SaveByPlayer("Автосохранение 1") ==
            "Автосохранение 1 1"); // a record of the player took an autosave record's name
    REQUIRE((folder.Autosave() == "Автосохранение 1" &&
             folder.files.size() == 2)); // the autosave and the player's record share a file
}

// Records of earlier builds ("Автосохранение 1|2", two rotating) stay in the rotation.
void earlierBuilds()
{
    Folder folder;
    folder.files = {{"Автосохранение 1", 5}, {"Автосохранение 2", 3}, {"Player AutoSave", 1}};
    folder.clock = 5;
    REQUIRE((folder.Autosave() == "Автосохранение 3" &&
             folder.Autosave() == "Автосохранение 2")); // records of earlier builds left the rotation
    REQUIRE(folder.files.at("Player AutoSave") == 1);   // the game's own autosave file was replaced
}
} // namespace

TEST_CASE("save_autosave")
{
    names();
    rotation();
    reservedNames();
    earlierBuilds();
    // The records no player wrote, as the save list and the save failure name them.
    REQUIRE((IsAutosaveRecord("Автосохранение 2") && IsAutosaveRecord("Player AutoSave") &&
             !IsAutosaveRecord("Автосохранение 7") && !IsAutosaveRecord("Ле Франсуа")));
    std::cout
        << "PASS: three autosave records in rotation, the oldest replaced; the player's records keep their files\n";
}
