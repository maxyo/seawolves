// The load watchdog (core load_watch.hpp) and the description of records written without one
// (xinterface save_description.hpp): a load the player asked for either applies its record or
// fails aloud, at its first refusal or after the deadline of running frames.
#include "load_watch.hpp"
#include "save_description.hpp"
#include <iostream>

#include <catch2/catch.hpp>
using storm::LoadWatch;
static void Write(const std::filesystem::path &file, uint32_t offset, uint32_t size, size_t length)
{
    std::string bytes(length < 40 ? 40 : length, '\0');
    std::memcpy(bytes.data(), "ver 1.7.3", 9);
    std::memcpy(bytes.data() + 32, &offset, 4);
    std::memcpy(bytes.data() + 36, &size, 4);
    bytes.resize(length);
    std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
TEST_CASE("load_watch")
{
    const double step = 0.5, deadline = LoadWatch::kTimeoutSeconds;
    // Nothing asked: nothing fails, refusals of other reads (save lists) are not failures.
    LoadWatch watch;
    watch.Refused("SAVE\\player\\x", "unreadable");
    REQUIRE((!watch.Poll(100.0) && !watch.Armed()));
    // A load that applies its record in time never fails.
    watch.Requested("SAVE\\player\\Автосохранение 1");
    for (int i = 0; i < 10; ++i)
        REQUIRE(!watch.Poll(step));
    watch.Loaded();
    REQUIRE((!watch.Armed() && !watch.Poll(deadline * 2)));
    // The description guard refuses the record: the failure comes at once, with that reason, once.
    watch.Requested("SAVE\\player\\Автосохранение 1");
    REQUIRE((watch.Watching("SAVE/Player/Автосохранение 1") && !watch.Watching("SAVE\\player\\Автосохранение 2")));
    watch.Refused("SAVE/Player/Автосохранение 1", "the record has no readable description");
    auto failure = watch.Poll(step);
    REQUIRE((failure && failure->record == "SAVE\\player\\Автосохранение 1" &&
             failure->reason == "the record has no readable description"));
    REQUIRE((!watch.Armed() && !watch.Watching("SAVE\\player\\Автосохранение 1") && !watch.Poll(step)));
    // A refusal of another record (the list reading descriptions meanwhile) does not fail the load.
    watch.Requested("SAVE\\player\\a");
    watch.Refused("SAVE\\player\\b", "unreadable");
    REQUIRE(!watch.Poll(step));
    // The first refusal is the reason; later ones do not replace it.
    watch.Refused("SAVE\\player\\a", "first");
    watch.Refused("SAVE\\player\\a", "second");
    failure = watch.Poll(step);
    REQUIRE((failure && failure->reason == "first"));
    // A load that never starts fails at the deadline of running frames, not before.
    watch.Requested("SAVE\\player\\a");
    double waited = 0;
    while (waited + step < deadline)
    {
        REQUIRE(!watch.Poll(step));
        waited += step;
    }
    failure = watch.Poll(step);
    REQUIRE((failure && failure->record == "SAVE\\player\\a" &&
             failure->reason.find("did not start") != std::string::npos));
    // A frame spanning a suspension of the app (or a negative clock step) counts kMaxFrameSeconds at most.
    watch.Requested("SAVE\\player\\a");
    REQUIRE((!watch.Poll(3600.0) && !watch.Poll(-5.0)));
    REQUIRE(watch.Waited() == LoadWatch::kMaxFrameSeconds);
    // A new request starts a new wait and forgets an old refusal.
    watch.Refused("SAVE\\player\\a", "old");
    watch.Requested("SAVE\\player\\a");
    REQUIRE((!watch.Poll(step) && watch.Waited() == step));
    watch.Loaded();
    // Records written without a description are described by their file name; a record with
    // a section (whole or damaged) or a header that cannot be read is not.
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    Write(dir / "undescribed", 0, 0, 4096);
    auto described = storm::UndescribedRecordDescription(dir / "undescribed", "SAVE\\player\\Автосохранение 1");
    REQUIRE((described && *described == "Автосохранение 1"));
    Write(dir / "described", 4000, 60, 4096);
    REQUIRE(!storm::UndescribedRecordDescription(dir / "described", "SAVE\\player\\x"));
    Write(dir / "short", 0, 0, 20);
    REQUIRE(!storm::UndescribedRecordDescription(dir / "short", "SAVE\\player\\x"));
    REQUIRE(!storm::UndescribedRecordDescription(dir / "missing", "SAVE\\player\\x"));
    REQUIRE(!storm::UndescribedRecordDescription(dir / "undescribed", "SAVE\\player\\"));
    std::cout << "PASS load watchdog: refusal at once, deadline of running frames, suspension cap, foreign refusals "
                 "ignored; undescribed records named by file\n";
}
