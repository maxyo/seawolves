#include <platform/android_land_commands.hpp>
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::android;
storm::ui::hud::MenuStep request(const LandCommandsState &s, const std::string &verb, const std::string &id = "")
{
    return {storm::ui::hud::MenuStep::Menu::Land, s.revision(), verb, id};
}
LandCommandsFrame fixture()
{
    LandCommandsFrame f;
    f.available = true;
    f.active = true;
    f.selectedItemId = "i0";
    // Identical native captions must keep different source/target identities.
    f.items = {{"i0", "command", "Открыть", "BI_OutDoor", "door:reload1", true, 1},
               {"i1", "command", "Открыть", "BI_UseBox", "box:treasure", true, 1},
               {"i2", "cancel", "Отмена", "", "cancel", true, 1}};
    return f;
}
TEST_CASE("android_land_commands")
{
    LandCommandsState s;
    auto f = fixture();
    s.update("bli:1:player:2:scene:3:target:4", f);
    REQUIRE(s.frame().items[0].source != s.frame().items[1].source);
    REQUIRE(!s.offer(request(s, "activateItem", "i1"))); // selection acknowledgement required
    REQUIRE(s.offer(request(s, "selectItem", "i1")));
    REQUIRE(!s.offer(request(s, "activateItem", "i0"))); // no overlapping actions
    REQUIRE(s.take()->id == "i1");
    s.invalidate();
    f.selectedItemId = "i1";
    s.update("bli:1:player:2:scene:3:target:4", f);
    REQUIRE(s.offer(request(s, "activateItem", "i1")));
    REQUIRE(s.take());
    s.invalidate();
    REQUIRE(!s.take());
    REQUIRE(!s.offer(request(s, "executeScript", "BI_UseBox")));
    REQUIRE(!s.offer(request(s, "selectRecipient", "i1")));
    REQUIRE(!s.offer(request(s, "open", "r0")));
    REQUIRE(!s.offer(request(s, "back", "i1")));
    // Coverage is an event, not a lease: it lasts however long the next UI frame takes.
    // No coverage words: the stock list never draws under the touch HUD (android_hud_presenter.hpp).
    REQUIRE(!s.offer(request(s, "ack", "1")));
    const auto stale = request(s, "activateItem", "i1");
    REQUIRE(s.offer(stale));
    f.items[1].identity = "box:different-chest-same-caption";
    s.update("bli:1:player:2:scene:3:target:4", f);
    REQUIRE((!s.take() && !s.offer(stale)));
    for (auto owner : {"bli:9:player:2:scene:3:target:4", "bli:9:player:7:scene:3:target:4",
                       "bli:9:player:7:scene:8:target:4", "bli:9:player:7:scene:8:target:6"})
    {
        auto old = request(s, "activateItem", "i1");
        s.update(owner, f);
        REQUIRE(!s.offer(old));
    }
    f.items[1].enabled = false;
    f.items[1].cooldown = .25;
    s.update("cooldown", f);
    REQUIRE(!s.offer(request(s, "activateItem", "i1")));
    auto revision = s.revision();
    f.items[1].cooldown = .5;
    s.update("cooldown", f);
    REQUIRE(revision == s.revision());
    f.items[1].enabled = true;
    f.items[1].cooldown = 1;
    s.update("cooldown", f);
    REQUIRE(revision != s.revision());
    auto removed = request(s, "activateItem", "i1");
    f.items.erase(f.items.begin() + 1);
    f.selectedItemId = "i0";
    s.update("cooldown", f);
    REQUIRE(!s.offer(removed));
    REQUIRE(s.offer(request(s, "activateItem", "i0")));
    REQUIRE(s.take());
    f.complete = false;
    s.update("incomplete", f);
    REQUIRE(!s.offer(request(s, "activateItem", "i0")));
    REQUIRE(s.offer(request(s, "close")));
    REQUIRE(s.take());
    f = fixture();
    f.items[0].label = "";
    s.update("missing", f);
    REQUIRE((!s.frame().complete && s.frame().available));
    REQUIRE((s.defect() && std::string(s.defect()).find("text") != std::string::npos));
    f = fixture();
    f.items[0].label = "\xc2\x85";
    s.update("invalid-utf8", f);
    REQUIRE(!s.frame().complete);
    f = fixture();
    f.active = false;
    f.items.clear();
    f.selectedItemId.clear();
    s.update("closed", f);
    REQUIRE(!s.defect());
    REQUIRE(s.offer(request(s, "open")));
    REQUIRE(s.take());
    // "open" into a root command: only one the scripts offer now; the state carries what is offered and the pictures
    f.offered = {"BI_FastReload", "BI_TalkSelf"};
    s.update("closed", f);
    REQUIRE(!s.offer(request(s, "open", "BI_SelfRepair")));
    REQUIRE(s.offer(request(s, "open", "BI_FastReload")));
    {
        auto open = s.take();
        REQUIRE((open && open->verb == "open" && open->id == "BI_FastReload"));
    }
    REQUIRE(((s.hud().offered == std::vector<std::string>{"BI_FastReload", "BI_TalkSelf"}) &&
             s.hud().here_picture.empty()));
    f = fixture();
    f.items[0].picture = "landtarget_slb:2";
    s.update("picture", f);
    REQUIRE((s.hud().items[0].source == "BI_OutDoor" && s.hud().items[0].picture == "landtarget_slb:2"));
    // the travel stage's place of the hero: its picture and name, or null
    f = fixture();
    f.herePicture = "landtarget_slb:1";
    f.hereLabel = "Верфь";
    s.update("here", f);
    REQUIRE((s.frame().complete && s.hud().here_picture == "landtarget_slb:1" && s.hud().here_label == "Верфь"));
    f.hereLabel = "";
    s.update("here-nameless", f);
    REQUIRE(!s.frame().complete);
    s.update({}, {});
    REQUIRE(!s.offer(request(s, "open")));
    // The drawable rule is the channel's own (hud_state.h Drawable): a kind the sheet has no drawing for leaves the
    // menu to the engine.
    {
        auto odd = fixture();
        odd.active = true;
        odd.items.push_back({"i9", "teleport", "Teleport", "BI_Teleport", "native-teleport", true, 1, ""});
        s.update("odd", odd);
        REQUIRE(!s.hud().complete);
    }
    std::cout << "PASS: land selection acknowledgement, duplicate-caption identity, stale target/scene/activity, "
                 "removed/disabled entries, cooldown, completeness, bounds, no coverage words, queue age and duplicate "
                 "rejection, open into an offered command, offered list and pictures in the JSON\n";
}
