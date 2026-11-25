// The new game's "skip the tutorial" choice (android_newgame.hpp): taken for the screen revision it was offered on,
// applied once to the intro's pending delayed event, and only for the stock hero and intro.
#include "android_newgame.hpp"
#include "core_fake.hpp"

#include <catch2/catch.hpp>

static storm::test::FakeCore fake;
Core &core = fake;

using storm::android::ApplyNewGameChoiceAfterFrame;
using storm::android::tutorialSkip;

namespace
{
struct NewGame
{
    ATTRIBUTES &interface = fake.scripts.object("InterfaceStates");
    ATTRIBUTES &player = fake.scripts.object("pchar");
    NewGame()
    {
        fake.scripts.integer("startHeroType", 1);
        interface.SetAttribute("AndroidTutorialSkipSupported", "1");
        tutorialSkip.reset(7);
    }
    // NewGame_continue has posted the intro's delayed event.
    ATTRIBUTES *introPosted(const char *video = "Sharlie_Start")
    {
        player.CreateSubAClass(&player, "GenQuest.VideoAfterQuest")->SetValue(video);
        return player.CreateSubAClass(&player, "PostEventQuest.questDelay.PostVideo_Start");
    }
    // The script function the delayed event will call.
    static std::string function(ATTRIBUTES *pending)
    {
        const char *name = pending->GetAttribute("function");
        return name ? name : "";
    }
};
} // namespace

TEST_CASE("the choice is taken for the revision it was offered on")
{
    NewGame game;
    CHECK_FALSE(tutorialSkip.selected);
    CHECK_FALSE(tutorialSkip.apply(6, "1@7"));
    CHECK_FALSE(tutorialSkip.apply(7, "1@6"));
    CHECK_FALSE(tutorialSkip.apply(7, "1@7x"));
    CHECK_FALSE(tutorialSkip.selected);
    CHECK(tutorialSkip.apply(7, "1@7"));
    CHECK(tutorialSkip.selected);
    CHECK(tutorialSkip.apply(7, "0@7"));
    CHECK_FALSE(tutorialSkip.selected);
    tutorialSkip.apply(7, "1@7");
    tutorialSkip.reset(8);
    CHECK_FALSE(tutorialSkip.selected);
    CHECK_FALSE(tutorialSkip.apply(8, "1@7"));
}

TEST_CASE("the choice waits for the intro and redirects it once")
{
    NewGame game;
    tutorialSkip.apply(7, "1@7");
    ApplyNewGameChoiceAfterFrame();
    CHECK(tutorialSkip.selected); // the new game's init has not posted the intro yet
    auto *pending = game.introPosted();
    ApplyNewGameChoiceAfterFrame();
    CHECK_FALSE(tutorialSkip.selected);
    CHECK(NewGame::function(pending) == "AndroidStartWithoutTutorial");
    pending->SetAttribute("function", "");
    ApplyNewGameChoiceAfterFrame();
    CHECK(NewGame::function(pending).empty());
}

TEST_CASE("without the choice the intro runs")
{
    NewGame game;
    auto *pending = game.introPosted();
    ApplyNewGameChoiceAfterFrame();
    CHECK(NewGame::function(pending).empty());
}

TEST_CASE("another hero or another intro keeps the intro, and the choice is spent")
{
    NewGame game;
    SECTION("another hero")
    {
        auto *pending = game.introPosted();
        tutorialSkip.apply(7, "1@7");
        fake.scripts.integer("startHeroType", 2);
        ApplyNewGameChoiceAfterFrame();
        CHECK_FALSE(tutorialSkip.selected);
        CHECK(NewGame::function(pending).empty());
    }
    SECTION("another intro")
    {
        auto *pending = game.introPosted("OtherIntro");
        tutorialSkip.apply(7, "1@7");
        ApplyNewGameChoiceAfterFrame();
        CHECK_FALSE(tutorialSkip.selected);
        CHECK(NewGame::function(pending).empty());
    }
    SECTION("no intro video")
    {
        auto *pending = game.player.CreateSubAClass(&game.player, "PostEventQuest.questDelay.PostVideo_Start");
        tutorialSkip.apply(7, "1@7");
        ApplyNewGameChoiceAfterFrame();
        CHECK_FALSE(tutorialSkip.selected);
        CHECK(NewGame::function(pending).empty());
    }
}

TEST_CASE("scripts without the skip drop the choice")
{
    NewGame game;
    auto *pending = game.introPosted();
    SECTION("not supported")
    {
        game.interface.SetAttribute("AndroidTutorialSkipSupported", "0");
    }
    SECTION("not declared")
    {
        game.interface.DeleteAttributeClassX(game.interface.GetAttributeClass("AndroidTutorialSkipSupported"));
    }
    tutorialSkip.apply(7, "1@7");
    ApplyNewGameChoiceAfterFrame();
    CHECK_FALSE(tutorialSkip.selected);
    CHECK(NewGame::function(pending).empty());
}
