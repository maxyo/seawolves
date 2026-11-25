// The touch HUD's interact button (updateCombatState, android_combat_state.hpp): offered for the dialog target the
// game's own FindDialogCharacter finds, or for the armed boarding continuation, delivered as the ChrAction edge of one
// native frame, and fenced when the target, the mode, the bindings or the scene change.
#include "android_combat_state.hpp"
#include "core_fake.hpp"
#include "platform/android_dialog_target.hpp"

#include <catch2/catch.hpp>

static storm::test::FakeCore fake;
Core &core = fake;

using namespace storm::android;

namespace
{
constexpr entid_t player = 7;

// The controls the game created: ChrAction bound.
struct Controls final : CONTROLS
{
    char name[10] = "ChrAction";
    USER_CONTROL action{};
    Controls()
    {
        action.name = name;
    }
    int32_t GetControlsNum() override
    {
        return 1;
    }
    bool GetControlDesc(int32_t, USER_CONTROL &out) override
    {
        out = action;
        return true;
    }
};

struct Town
{
    storm::test::Scripts &scripts = fake.scripts;
    Controls controls;
    DATA &characters = scripts.variable("Characters", VAR_OBJECT, 14);
    ATTRIBUTES &hero = *characters.GetAClass(0);
    int64_t dialogIndex = 12; // what FindDialogCharacter answers; -1: nobody
    GameplayState gameplay;
    GameplaySnapshot snapshot;

    Town()
    {
        fake.Controls = &controls;
        fake.entities = {{"Player", player}};
        fake.entityAttributes = {{player, &hero}};
        fake.message = [this](entid_t, const std::vector<std::string> &arguments) -> uint64_t {
            const auto &question = arguments.at(1);
            if (question == "IsDead")
                return 0;
            REQUIRE(question == "FindDialogCharacter");
            return static_cast<uint64_t>(dialogIndex);
        };
        scripts.integer("nMainCharacterIndex", 0);
        auto &store = *characters.GetAClass(12);
        store.SetAttribute("name", "Хуан");
        store.SetAttribute("lastname", "Гарсия");
        store.CreateSubAClass(&store, "Dialog.Filename")->SetValue("Common_Store.c");
        snapshot.mode = "explore";
        snapshot.group = "PrimaryLand";
        snapshot.scene = "tavern:7";
        snapshot.capabilities = {"ChrAction"};
        gameplay.update(snapshot);
        gameplayReadEpoch = storm::ScriptStateEpoch(); // the snapshot is the current state
        combatInput.guard = [] { return true; };
    }
    ~Town()
    {
        for (const char *name : {"Characters", "nMainCharacterIndex", "LAi_boarding_process"})
            scripts.remove(name);
        fake.eventHandlers.clear();
        fake.Controls = nullptr;
        cancelAndroidCombatInput();
    }
    void mode(const char *mode)
    {
        snapshot.mode = mode;
        gameplay.update(snapshot);
    }
    void refresh()
    {
        updateCombatState(gameplay, &fake);
    }
    static storm::ui::hud::CombatAction tap(const char *phase = "tap")
    {
        return {combatInput.generation(), combatInput.actionGeneration("interact"), "interact", phase};
    }
    // The ChrAction the native frame sees after a tap was taken.
    static int frameAction()
    {
        combatInput.advance();
        return combatInput.merge("ChrAction", 0);
    }
    void armBoarding()
    {
        scripts.integer("LAi_boarding_process", 1);
        fake.eventHandlers.insert({"Control Activation", "LAi_ActivateReload"});
    }
};
} // namespace

TEST_CASE("the talk target is named and its role told by its dialog file")
{
    Town town;
    town.refresh();
    CHECK(combatInput.hud().Has("interact"));
    CHECK(combatInteractKind == "dialog");
    CHECK(combatInteractName == "Хуан Гарсия");
    CHECK(combatInteractDialog == "common_store");
    town.characters.GetAClass(12)
        ->FindAClass(town.characters.GetAClass(12), "Dialog.Filename")
        ->SetValue("Quest\\Sharlie\\OtherNPC.c");
    town.refresh();
    CHECK(combatInteractDialog == "othernpc"); // folders and extension dropped, lower case
}

TEST_CASE("a tap is the ChrAction edge of one native frame")
{
    Town town;
    town.refresh();
    CHECK_FALSE(combatInput.offer(Town::tap("down")));
    REQUIRE(combatInput.offer(Town::tap()));
    CHECK(combatInput.merge("ChrAction", 0) == 0); // not before the frame
    CHECK(Town::frameAction() == 3);
    town.refresh();
    CHECK(combatInput.merge("ChrAction", 0) == 3); // Player::Update of the same frame still sees it
    CHECK(combatInput.merge("ChrAction", 1) == 1); // a physical key keeps priority
    CHECK(Town::frameAction() == 2);
    CHECK(Town::frameAction() == 0);
}

TEST_CASE("the target walking away revokes the button and the taps aimed at it")
{
    Town town;
    town.refresh();
    const auto activity = combatInput.generation();
    const auto queued = Town::tap();
    REQUIRE(combatInput.offer(queued));
    town.dialogIndex = -1;
    town.refresh();
    CHECK_FALSE(combatInput.hud().Has("interact"));
    CHECK(combatInput.generation() == activity); // the session survives; only interact is fenced
    CHECK(Town::frameAction() != 3);
    CHECK_FALSE(combatInput.offer(queued));
    town.dialogIndex = 12;
    town.refresh();
    CHECK_FALSE(combatInput.offer(queued)); // a new availability: taps at the old one never fire
    CHECK(combatInput.offer(Town::tap()));
    CHECK(Town::frameAction() == 3);
}

TEST_CASE("another target under the button refuses taps aimed at the old one")
{
    Town town;
    town.refresh();
    const auto aimed = Town::tap();
    town.dialogIndex = 13;
    town.refresh();
    CHECK(combatInput.hud().Has("interact"));
    CHECK_FALSE(combatInput.offer(aimed));
    CHECK(combatInput.offer(Town::tap()));
}

TEST_CASE("no interact in melee, without the binding, or when the scripts moved on")
{
    Town town;
    town.refresh();
    REQUIRE(combatInput.hud().Has("interact"));
    SECTION("melee")
    {
        town.mode("melee");
    }
    SECTION("the control locked")
    {
        town.controls.action.bLocked = true;
    }
    SECTION("the control locked by the system")
    {
        town.controls.action.bSystemLocked = true;
    }
    SECTION("the control unassigned")
    {
        town.controls.action.system_code = UNASSIGNED_CONTROL;
    }
    SECTION("the key group does not bind it")
    {
        town.snapshot.capabilities.clear();
        town.gameplay.update(town.snapshot);
    }
    SECTION("a dialogue began before the next frame: the scripts no longer give this snapshot")
    {
        gameplayReadEpoch = storm::ScriptStateEpoch() + 1;
    }
    SECTION("the hero is dead")
    {
        fake.message = [](entid_t, const std::vector<std::string> &arguments) -> uint64_t {
            return arguments.at(1) == "IsDead" ? 1 : 12;
        };
    }
    SECTION("FindDialogCharacter answers an index the scripts cannot have")
    {
        town.dialogIndex = 2147483648LL;
    }
    SECTION("the player entity is another character")
    {
        fake.entityAttributes[player] = town.characters.GetAClass(3);
    }
    const auto stale = Town::tap();
    town.refresh();
    CHECK_FALSE(combatInput.hud().Has("interact"));
    CHECK_FALSE(combatInput.offer(stale));
    CHECK_FALSE(combatInput.offer(Town::tap()));
}

TEST_CASE("leaving the screen cancels a queued tap")
{
    Town town;
    town.refresh();
    REQUIRE(combatInput.offer(Town::tap()));
    cancelAndroidCombatInput();
    CHECK(Town::frameAction() != 3);
}

TEST_CASE("after a won boarding fight the action key continues the boarding")
{
    Town town;
    town.dialogIndex = -1;
    town.refresh();
    REQUIRE_FALSE(combatInput.hud().Has("interact"));
    town.armBoarding();
    town.refresh();
    CHECK(combatInput.hud().Has("interact"));
    CHECK(combatInteractKind == "boarding");
    CHECK(combatInteractName.empty());
    REQUIRE(combatInput.offer(Town::tap()));
    CHECK(Town::frameAction() == 3);
    SECTION("fighting on: not offered")
    {
        town.mode("melee");
        town.refresh();
        CHECK_FALSE(combatInput.hud().Has("interact"));
    }
    SECTION("a quest forbids the continuation")
    {
        town.hero.CreateSubAClass(&town.hero, "GenQuest.CannotReloadBoarding");
        town.refresh();
        CHECK_FALSE(combatInput.hud().Has("interact"));
        CHECK(combatInteractKind == "dialog");
    }
    SECTION("the handler disarmed: gone with its queued taps")
    {
        Town::frameAction();
        Town::frameAction();
        REQUIRE(combatInput.offer(Town::tap()));
        fake.eventHandlers.clear();
        town.refresh();
        CHECK_FALSE(combatInput.hud().Has("interact"));
        CHECK(combatInteractKind == "dialog");
        CHECK(Town::frameAction() != 3);
    }
    SECTION("the boarding is over")
    {
        town.scripts.integer("LAi_boarding_process", 0);
        town.refresh();
        CHECK_FALSE(combatInput.hud().Has("interact"));
    }
}

TEST_CASE("the talk target keeps within a wider reach than it is taken in")
{
    const float yaw = 0.0f; // facing +z
    const auto at = [](uint64_t id, float x, float z) { return DialogCandidate{id, x, 0.0f, z, x * x + z * z, true}; };
    DialogCandidate near[] = {at(5, 0.0f, 3.0f)};
    CHECK(dialogTargetChoice(near, yaw, 0) == 5); // within 3.5 m, straight ahead
    DialogCandidate drifting[] = {at(5, 0.0f, 4.2f)};
    CHECK(dialogTargetChoice(drifting, yaw, 0) == 0);
    CHECK(dialogTargetChoice(drifting, yaw, 5) == 5); // 4.2 m: not taken, but kept
    DialogCandidate aside[] = {at(5, 1.6f, 3.0f)}; // about 28 degrees off: outside the 20 degree half-cone, inside 35
    CHECK(dialogTargetChoice(aside, yaw, 0) == 0);
    CHECK(dialogTargetChoice(aside, yaw, 5) == 5);
    DialogCandidate gone[] = {at(5, 0.0f, 4.8f)};
    CHECK(dialogTargetChoice(gone, yaw, 5) == 0); // beyond the exit reach: let go
    DialogCandidate crowd[] = {at(5, 0.0f, 4.0f), at(6, 0.0f, 2.0f)};
    CHECK(dialogTargetChoice(crowd, yaw, 5) == 5); // a closer passer-by does not steal a kept target
    CHECK(dialogTargetChoice(crowd, yaw, 0) == 6);
    DialogCandidate fighting[] = {DialogCandidate{5, 0.0f, 0.0f, 2.0f, 4.0f, false}};
    CHECK(dialogTargetChoice(fighting, yaw, 5) == 0);
    CHECK(validDialogTarget(0));
    CHECK(validDialogTarget(123));
    CHECK_FALSE(validDialogTarget(-1));
    CHECK_FALSE(validDialogTarget(2147483648LL));
}
