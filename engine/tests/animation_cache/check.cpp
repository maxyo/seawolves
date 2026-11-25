#include "core.h"
#include "animation_service_imp.h"
#include "an_file.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

#include <catch2/catch.hpp>

TestCore core;
TestFiles files;
TestFiles *fio = &files;

template <typename T> static void write(std::ofstream &stream, const T &value)
{
    stream.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

static void frame(AnimationServiceImp &service, uint32_t real, uint32_t game = 0)
{
    core.realDeltaTime = real;
    core.deltaTime = game;
    service.RunStart();
}

static std::unique_ptr<Animation> create(AnimationServiceImp &service, const char *name)
{
    auto result = std::unique_ptr<Animation>(service.CreateAnimation(name));
    REQUIRE((result && result->GetNumBones() == 1)); // production AN loader created a one-bone animation
    return result;
}

TEST_CASE("animation_cache")
{
    files.fixture = (std::filesystem::temp_directory_path() / "fixture.an").string();
    {
        std::ofstream fixture(files.fixture, std::ios::binary);
        write(fixture, ANFILE::HEADER{2, 1, 15.0f});
        write(fixture, int32_t{-1});
        write(fixture, glm::vec3{0.0f});
        write(fixture, glm::vec3{0.0f});
        write(fixture, glm::vec3{0.0f});
        write(fixture, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
        write(fixture, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
        REQUIRE(static_cast<bool>(fixture)); // wrote AN fixture
    }
    {
        AnimationServiceImp service;
        auto first = create(service, "sailor");
        auto second = create(service, "SAILOR");
        REQUIRE((files.iniReads == 1 && files.animationReads == 1)); // shared active name is loaded once
        first.reset();
        REQUIRE(service.TrimCaches() == 0); // trim preserves data with a remaining live reference
        second.reset();
        frame(service, 1, 1000);
        auto reused = create(service, "sailor");
        REQUIRE(files.animationReads == 1); // one idle frame retains data across a transition
        reused.reset();

        // High frame rates must not get the old artificial minimum age of 10ms per frame.
        for (unsigned i = 0; i < 5000; ++i)
            frame(service, 1, 1000);
        auto fastFrames = create(service, "sailor");
        REQUIRE(files.animationReads == 1); // five real seconds at 1000fps retains cached data
        fastFrames.reset();
        frame(service, 29'999, 0);
        auto nearExpiry = create(service, "sailor");
        REQUIRE(files.animationReads == 1); // real-time cache survives until its 30-second deadline
        // Reuse and release without an intervening frame must restart the full idle window.
        nearExpiry.reset();
        frame(service, 1, 1000);
        auto resetAge = create(service, "sailor");
        REQUIRE(files.animationReads == 1); // reacquiring resets idle age immediately
        resetAge.reset();
        frame(service, 30'000, 0);
        auto expired = create(service, "sailor");
        REQUIRE((files.iniReads == 2 && files.animationReads == 2)); // real expiry reloads while game time is paused
        frame(service, 60'000);
        auto stillActive = create(service, "sailor");
        REQUIRE(files.animationReads == 2); // normal expiry preserves active data

        // A just-released entry has zero downtime: pressure must use references, not age.
        auto idle = create(service, "guard");
        idle.reset();
        REQUIRE(service.TrimCaches() == 1); // pressure frees a just-released entry before a frame
        REQUIRE(service.TrimCaches() == 0); // repeated pressure is safe and keeps active data
        auto reloaded = create(service, "guard");
        REQUIRE(files.animationReads == 4); // trimmed entry is genuinely reloaded from AN data
        reloaded.reset();
        stillActive.reset();
        expired.reset();
        REQUIRE(service.TrimCaches() == 2); // pressure frees all remaining idle entries
        frame(service, 1000);
        REQUIRE(service.TrimCaches() == 0); // frame stepping tolerates empty cache slots
    }
    {
        AnimationInfo info("overflow");
        info.AddDowntime(std::numeric_limits<uint32_t>::max() - 5);
        info.AddDowntime(20);
        REQUIRE(info.GetDowntime() == std::numeric_limits<uint32_t>::max()); // idle age saturates without wrapping
        info.AddRef();
        REQUIRE((info.GetDowntime() == 0 && !info.IsUnused())); // acquire clears age and protects data
        info.AddDowntime(std::numeric_limits<uint32_t>::max());
        REQUIRE(info.GetDowntime() == 0); // active data does not age
        info.RelRef();
        info.AddDowntime(0);
        REQUIRE((info.GetDowntime() == 0 && info.IsUnused())); // zero elapsed time does not age an idle entry
    }
    std::filesystem::remove(files.fixture);
    std::cout << "PASS: animation cache reuse, real-time expiry, reference-safe pressure and age saturation\n";
}
