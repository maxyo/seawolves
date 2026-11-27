// The new game's "skip the tutorial" choice (android_newgame.hpp): taken for the screen revision it was offered on,
// and handed to the game's new-game start as InterfaceStates.SkipTutorial.
#include "android_newgame.hpp"
#include "core_fake.hpp"

#include <catch2/catch.hpp>

static storm::test::FakeCore fake;
Core &core = fake;

using storm::android::tutorialSkip;

namespace
{
struct NewGame
{
    ATTRIBUTES &interface = fake.scripts.object("InterfaceStates");
    NewGame()
    {
        fake.scripts.integer("startHeroType", 1);
        interface.SetAttribute("AndroidTutorialSkipSupported", "1");
        tutorialSkip.reset(7);
    }
    bool handed() const
    {
        return interface.FindAClass(&interface, "SkipTutorial") != nullptr;
    }
};
} // namespace

TEST_CASE("the choice is taken for the revision it was offered on")
{
    NewGame game;
    CHECK_FALSE(tutorialSkip.selected());
    CHECK_FALSE(tutorialSkip.apply(6, "1@7"));
    CHECK_FALSE(tutorialSkip.apply(7, "1@6"));
    CHECK_FALSE(tutorialSkip.apply(7, "1@7x"));
    CHECK_FALSE(game.handed());
    CHECK(tutorialSkip.apply(7, "1@7"));
    CHECK((tutorialSkip.selected() && game.handed()));
    CHECK(tutorialSkip.apply(7, "0@7"));
    CHECK_FALSE(game.handed());
}

TEST_CASE("another screen takes the choice back")
{
    NewGame game;
    tutorialSkip.apply(7, "1@7");
    tutorialSkip.reset(8);
    CHECK_FALSE(game.handed());
    CHECK_FALSE(tutorialSkip.apply(8, "1@7"));
}

TEST_CASE("offered for the hero with the tutorial when the scripts support it")
{
    NewGame game;
    CHECK(storm::android::TutorialSkipSupported());
    fake.scripts.integer("startHeroType", 2);
    CHECK_FALSE(storm::android::TutorialSkipSupported());
    fake.scripts.integer("startHeroType", 1);
    game.interface.SetAttribute("AndroidTutorialSkipSupported", "0");
    CHECK_FALSE(storm::android::TutorialSkipSupported());
}
