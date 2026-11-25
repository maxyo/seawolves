#include <platform/android_sea_orders.hpp>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
storm::ui::hud::MenuStep request(const SeaOrdersState &s, const std::string &verb, const std::string &id = "")
{
    return {storm::ui::hud::MenuStep::Menu::Sea, s.revision(), verb, id};
}
SeaOrdersFrame fixture()
{
    SeaOrdersFrame f;
    f.available = true;
    f.recipients.push_back({"r11_0", "Flagship", "11", 0, true});
    f.recipients.push_back({"r12_3", "Companion", "12", 3, false});
    f.recipientId = "r11_0";
    return f;
}
TEST_CASE("android_sea_orders")
{
    SeaOrdersRefreshGate gate;
    // ready, battle, ship, character, active, shown, invalidated
    REQUIRE(gate.due(true, 10, 20, 3, false, true, false)); // nothing read yet
    // A closed menu with nothing invalidated is never asked again, however many loops pass.
    for (int i = 0; i < 10000; ++i)
        REQUIRE(!gate.due(true, 10, 20, 3, false, true, false));
    REQUIRE(gate.due(true, 10, 20, 3, false, true, true)); // SetUpdate: ships created, sunk, refreshed
    REQUIRE(!gate.due(true, 10, 20, 3, false, true, false));
    REQUIRE(gate.due(true, 11, 20, 3, false, true, false));  // battle replacement
    REQUIRE(gate.due(true, 11, 21, 3, false, true, false));  // flagship replacement
    REQUIRE(gate.due(true, 11, 21, 4, false, true, false));  // character identity
    REQUIRE(gate.due(true, 11, 21, 4, false, false, false)); // the command menu hidden
    REQUIRE(!gate.due(true, 11, 21, 4, false, false, false));
    REQUIRE(gate.due(true, 11, 21, 4, false, true, false));   // and shown again
    REQUIRE(gate.due(false, 11, 21, 4, false, true, false));  // forbidden state closes immediately
    REQUIRE(!gate.due(false, 11, 21, 4, false, true, false)); // no repeated empty snapshots
    REQUIRE(!gate.due(false, 11, 21, 4, false, true, true));  // an invalidation while not ready asks nothing
    REQUIRE(gate.due(true, 11, 21, 4, false, true, false));   // ready again
    REQUIRE(gate.due(true, 11, 21, 4, true, true, false));    // native/script menu opened
    for (int i = 0; i < 100; ++i)
        REQUIRE(gate.due(true, 11, 21, 4, true, true, false)); // active predicates never delayed
    REQUIRE(gate.due(true, 11, 21, 4, false, true, false));    // menu closed
    REQUIRE(!gate.due(true, 11, 21, 4, false, true, false));
    gate.force();
    REQUIRE(gate.due(true, 11, 21, 4, false, true, false)); // native action or close
    REQUIRE(!gate.due(true, 11, 21, 4, false, true, false));
    SeaOrdersState s;
    auto f = fixture();
    s.update("battle1:ship11", f);
    REQUIRE(s.offer(request(s, "open")));
    REQUIRE(s.take()->verb == "open");
    // A recipient can be replaced between the last snapshot and a tap.
    // The dispatch refresh must change the revision even when its visible name
    // and position did not change; the old request cannot be offered again.
    const auto staleRecipient = request(s, "open", "r12_3");
    REQUIRE(s.offer(staleRecipient));
    const auto pendingRecipient = s.take();
    const auto recipientRevision = s.revision();
    f.recipients[1].entityId = "99";
    f.recipients[1].id = "r99_3";
    s.update("battle1:ship11", f);
    REQUIRE(s.revision() != recipientRevision);
    REQUIRE(pendingRecipient->revision != s.revision());
    REQUIRE(!s.offer(staleRecipient));
    const auto oldScene = s.sceneEpoch();
    s.invalidate();
    f.active = true;
    f.mode = 32;
    f.items.push_back({"i0", "command", "Drift", "native-drift", true, 1});
    f.items.push_back({"i1", "command", "Ability", "native-ability", false, .25});
    s.update("battle1:ship11", f);
    REQUIRE(s.sceneEpoch() == oldScene); // ComState transition is not scene exit.
    REQUIRE(!s.offer(request(s, "open")));
    REQUIRE(!s.offer(request(s, "activateItem", "i1")));
    REQUIRE(!s.offer(request(s, "activateItem", "i999")));
    REQUIRE(!s.offer(request(s, "selectRecipient", "r12_99")));
    REQUIRE(!s.offer(request(s, "back", "i0")));
    REQUIRE(!s.offer(request(s, "executeScript", "BI_Attack")));
    REQUIRE(s.offer(request(s, "shown", "1")));
    REQUIRE(s.covered());
    REQUIRE(s.offer(request(s, "shown", "0")));
    REQUIRE(!s.covered());
    const auto beforeCooldown = s.revision();
    f.items[1].cooldown = .55;
    s.update("battle1:ship11", f);
    REQUIRE(s.revision() == beforeCooldown);
    const auto stale = request(s, "activateItem", "i0");
    REQUIRE(s.offer(stale));
    f.items[0].identity = "different-command-same-position-and-label";
    s.update("battle1:ship11", f);
    REQUIRE(!s.take());
    REQUIRE(!s.offer(stale));
    const auto beforeReady = s.revision();
    f.items[1].enabled = true;
    f.items[1].cooldown = 1;
    s.update("battle1:ship11", f);
    REQUIRE(s.revision() != beforeReady);
    REQUIRE(s.offer(request(s, "activateItem", "i0")));
    REQUIRE(s.offer(request(s, "activateItem", "i0")));
    REQUIRE(s.take());
    s.invalidate();
    REQUIRE(!s.take()); // one effect per revision
    for (int i = 0; i < 8; ++i)
        REQUIRE(s.offer(request(s, "selectItem", "i0")));
    s.invalidate();
    REQUIRE(!s.take()); // a new revision: all stale, no actions remain
    REQUIRE(s.offer(request(s, "close")));
    REQUIRE(s.take());
    REQUIRE(!s.covered());
    REQUIRE(s.offer(request(s, "shown", "1")));
    REQUIRE(s.covered());
    f.complete = false;
    s.update("battle1:ship11", f);
    REQUIRE(s.offer(request(s, "shown", "1")));
    REQUIRE(!s.covered());
    REQUIRE(!s.hud().complete);
    const auto old = request(s, "close");
    s.update("battle2:ship11", f);
    REQUIRE(s.sceneEpoch() != oldScene);
    REQUIRE(!s.offer(old));
    s.update({}, {});
    REQUIRE(!s.offer(request(s, "open")));
    REQUIRE(!s.covered());
    f = fixture();
    f.active = true;
    f.recipients[0].name = "";
    f.items.push_back({"i0", "command", "   ", "native-missing-note", true, 1});
    s.update("missing-labels", f);
    REQUIRE((!s.frame().complete && s.frame().active && s.frame().available));
    REQUIRE(s.frame().recipients[0].name == "\xE2\x80\xA6");
    REQUIRE(s.frame().items[0].label == "\xE2\x80\xA6");
    REQUIRE((s.defect() && std::string(s.defect()).find("text") != std::string::npos));
    REQUIRE(s.offer(request(s, "back")));
    REQUIRE(s.take()->verb == "back");
    REQUIRE(s.offer(request(s, "close")));
    REQUIRE(s.take()->verb == "close");
    f.recipients[0].name = "\xC2\x85";
    s.update("c1-control-label", f);
    REQUIRE((!s.frame().complete && s.frame().recipients[0].name == "\xE2\x80\xA6"));
    REQUIRE(s.offer(request(s, "close")));
    REQUIRE(s.take()->verb == "close");
    // The channel's state: the recipients and items as the menu has them, Back and Close while it is open.
    f = fixture();
    f.active = true;
    f.items.push_back({"i0", "command", "Drift", "native-drift", true, 1.5, "BI_Drift"});
    s.update("state", f);
    const auto shown = s.hud();
    REQUIRE((shown.known && shown.active && shown.can_back && shown.can_close && shown.complete &&
             shown.recipients.size() == f.recipients.size() && shown.items.size() == 1 &&
             shown.items[0].source == "BI_Drift" && shown.items[0].cooldown == 1.0));
    // The drawable rule is the channel's own (hud_state.h Drawable): a kind the sheet has no drawing for leaves the
    // menu to the engine.
    {
        auto odd = fixture();
        odd.active = true;
        odd.items.push_back({"i9", "teleport", "Teleport", "native-teleport", true, 1});
        s.update("odd", odd);
        REQUIRE(!s.hud().complete);
    }
    std::cout << "PASS: sea order guards, live identities, cooldown, queue, ACK, duplicate rejection; closed menu "
                 "asked only on invalidation or a gating change, active menu every iteration\n";
}
