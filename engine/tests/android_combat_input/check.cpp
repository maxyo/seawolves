#include <platform/android_combat_input.hpp>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
// A tap of the HUD on `action` of the session and availability the combat state shows now.
storm::ui::hud::CombatAction packet(const CombatInput &c, const char *action, const char *phase = "tap")
{
    return {c.generation(), c.actionGeneration(action), action, phase};
}
TEST_CASE("android_combat_input")
{
    CombatControlAvailability allowed;
    REQUIRE(!allowed.contains("ChrFire"));
    std::string descriptorName = "ChrFire";
    allowed.allow(descriptorName);
    descriptorName.assign(1000, 'x'); // script callback may replace the controls table
    REQUIRE(allowed.contains("ChrFire"));
    REQUIRE(!allowed.contains(descriptorName));
    allowed.allow("UnrelatedControl");
    REQUIRE(!allowed.contains("UnrelatedControl"));
    for (const char *alias :
         {"ChrFightMode", "BOAL_UsePotion", "ChrAction", "ChrCamSpecMode", "ChrAttackForce2", "ChrAttackForce",
          "ChrAttackFast2", "ChrAttackFast", "ChrAttackBreak", "ChrAttackBreak2", "ChrParry", "ChrParry2",
          "ChrAttackFient", "ChrAttackFient2", "ChrAttackRound", "ChrAttackRound2", "ChrFire", "ChrBlock", "ChrBlock2"})
    {
        allowed.allow(alias);
        REQUIRE(allowed.contains(alias));
    }
    CombatControlAvailability nextFrame;
    REQUIRE(!nextFrame.contains("ChrFire"));
    CombatInput c;
    bool valid = true;
    c.guard = [&] { return valid; };
    c.resetActivity();
    const std::vector<CombatBinding> bindings = {{"force", "ChrAttackForce2", false},
                                                 {"fast", "ChrAttackFast2", false},
                                                 {"block", "ChrBlock", true},
                                                 {"potion", "BOAL_UsePotion", false}};
    c.update(10, "town", "player1", "melee", bindings);
    REQUIRE(c.offer(packet(c, "force")));
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 3);
    REQUIRE(c.merge("ChrAttackForce2", 0) == 3); // polling is stable
    REQUIRE(c.merge("ChrAttackForce2", 1) == 1);
    REQUIRE(c.merge("ChrAttackForce2", 3) == 3); // physical source is not reactivated
    REQUIRE(c.offer(packet(c, "force")));
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 2);
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 3);
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 2);
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 0);
    REQUIRE(c.offer(packet(c, "block", "down")));
    c.advance();
    REQUIRE(c.merge("ChrBlock", 0) == 3);
    c.advance();
    REQUIRE(c.merge("ChrBlock", 0) == 1);
    REQUIRE(c.offer(packet(c, "block", "up")));
    REQUIRE(c.merge("ChrBlock", 0) == 2);
    REQUIRE(c.offer(packet(c, "fast")));
    cancelAndroidCombatInput();
    c.advance();
    REQUIRE(c.merge("ChrAttackFast2", 0) != 3);
    REQUIRE(c.offer(packet(c, "block", "down")));
    c.advance();
    cancelAndroidCombatInput();
    REQUIRE(c.merge("ChrBlock", 0) == 2);
    const auto drainedEpoch = combatCancelEpoch.load();
    const auto drained = packet(c, "block", "down");
    cancelAndroidCombatInput();
    REQUIRE(!c.offer(drained, drainedEpoch));
    c.advance();
    REQUIRE(c.merge("ChrBlock", 0) != 3);
    REQUIRE(!c.offer(packet(c, "force", "down")));
    REQUIRE(!c.offer(packet(c, "BOAL_UsePotion")));
    const auto stale = packet(c, "force");
    c.update(11, "town", "player2", "melee", bindings);
    REQUIRE(!c.offer(stale));
    const auto oldOwner = packet(c, "force");
    c.update(11, "town", "player3", "melee", bindings);
    REQUIRE(!c.offer(oldOwner));
    auto remapped = bindings;
    remapped[0].control = "ChrAttackForce";
    const auto oldMapping = packet(c, "force");
    c.update(11, "town", "player3", "melee", remapped);
    REQUIRE(!c.offer(oldMapping));
    c.update(11, "town", "player3", "melee", bindings);
    REQUIRE(c.offer(packet(c, "block", "down")));
    c.advance();
    valid = false;
    REQUIRE(c.merge("ChrBlock", 0) == 2);
    valid = true;
    const auto oldActivity = packet(c, "force");
    c.resetActivity();
    REQUIRE(!c.offer(oldActivity));
    // Mashing faster than the frames pulse: one tap waits per action, the rest are dropped, so nothing swings on
    // after the player stopped (12 taps a second at 15 fps for 3 s).
    {
        int accepted = 0, pulses = 0;
        for (int frame = 0; frame < 45; ++frame)
        {
            if (frame % 15 < 12) // 12 taps in each second of 15 frames
                accepted += c.offer(packet(c, "force")) ? 1 : 0;
            c.advance();
            pulses += c.merge("ChrAttackForce2", 0) == 3 ? 1 : 0;
        }
        int after = 0;
        for (int frame = 0; frame < 30; ++frame)
        {
            c.advance();
            after += c.merge("ChrAttackForce2", 0) == 3 ? 1 : 0;
        }
        REQUIRE((accepted <= 45 && pulses > 0 && after <= 1)); // at most the one waiting tap acts after the last
    }
    REQUIRE((c.offer(packet(c, "force")) && !c.offer(packet(c, "force")))); // a repeat of a waiting tap is dropped
    REQUIRE(c.offer(packet(c, "block", "up")));
    REQUIRE(c.offer(packet(c, "all", "cancel")));
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) != 3);
    // Granularity: availability changes fence only the action whose availability changed.
    c.resetActivity();
    c.update(13, "town", "player3", "melee", bindings);
    const auto inFlight = packet(c, "force");
    REQUIRE(c.offer(packet(c, "fast")));
    auto noPotion = bindings;
    noPotion.pop_back();
    c.update(14, "town", "player3", "melee", noPotion); // potion gone, new revision
    REQUIRE(c.offer(inFlight));                         // stamped before the change, still valid
    c.update(15, "town", "player3", "melee", bindings); // potion back: other actions keep their generation
    c.advance();
    REQUIRE(c.merge("ChrAttackFast2", 0) == 3); // the queued fast tap survived both changes
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 3);
    c.advance();
    c.advance();
    REQUIRE(c.offer(packet(c, "block", "down")));
    c.advance();
    REQUIRE(c.merge("ChrBlock", 0) == 3);
    const auto oldBlock = packet(c, "block", "up");
    const auto queued = packet(c, "force");
    REQUIRE(c.offer(queued));
    auto noBlock = bindings;
    noBlock.erase(noBlock.begin() + 2);
    c.update(16, "town", "player3", "melee", noBlock);
    REQUIRE(c.merge("ChrBlock", 0) == 2);               // a vanished action lets go of its hold at once
    REQUIRE(c.merge("ChrAttackForce2", 0) != 2);        // … and only of its own control
    c.update(17, "town", "player3", "melee", bindings); // block back with a new generation
    REQUIRE(!c.offer(oldBlock));                        // a command stamped with the old availability is refused
    c.advance();
    REQUIRE(c.merge("ChrAttackForce2", 0) == 3); // the unrelated queued tap still fires
    const auto oldScene = packet(c, "force");
    c.update(17, "harbour", "player3", "melee", bindings);
    REQUIRE(!c.offer(oldScene)); // a new scene ends the session
    c.update(12, "", "", "unknown", {});
    REQUIRE(!c.offer(packet(c, "force")));
    std::cout << "PASS combat semantic pulses, physical merge, held release, atomic cancel, per-action availability "
                 "and scene/activity guards\n";
}
