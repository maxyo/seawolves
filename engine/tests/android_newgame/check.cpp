// The new game's "skip the tutorial" choice (android_newgame.hpp): the toggle's value, handed to the game's new-game
// start as InterfaceStates.SkipTutorial.
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
        tutorialSkip.set(false);
    }
    bool handed() const
    {
        return interface.FindAClass(&interface, "SkipTutorial") != nullptr;
    }
};
} // namespace

TEST_CASE("the toggle's value is the choice")
{
    NewGame game;
    CHECK_FALSE(tutorialSkip.selected());
    CHECK_FALSE(tutorialSkip.apply("1@7"));
    CHECK_FALSE(tutorialSkip.apply(""));
    CHECK_FALSE(game.handed());
    CHECK(tutorialSkip.apply("1"));
    CHECK((tutorialSkip.selected() && game.handed()));
    CHECK(tutorialSkip.apply("0"));
    CHECK_FALSE(game.handed());
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
